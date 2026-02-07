#ifndef FLEXIMG_RENDERER_NODE_H
#define FLEXIMG_RENDERER_NODE_H

#include "../core/node.h"
#include "../core/types.h"
#include "../core/perf_metrics.h"
#include "../core/format_metrics.h"
#include "../core/render_context.h"
#include "../image/render_types.h"
#include "../image/image_buffer_entry_pool.h"
#include <algorithm>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// RendererNode - パイプライン発火点ノード
// ========================================================================
//
// パイプライン実行の発火点となるノードです。
// - 入力: 1ポート（上流の処理ノード）
// - 出力: 1ポート（下流のSinkNode/DistributorNode）
// - 仮想スクリーンサイズと pivot を持つ（pivot は独立して機能）
// - タイル分割処理を制御
//
// 使用例:
//   SourceNode src;
//   RendererNode renderer;
//   SinkNode sink(output);
//
//   src >> renderer >> sink;
//
//   renderer.setVirtualScreen(320, 240);
//   renderer.setPivotCenter();  // 明示的に設定が必要
//   renderer.setTileConfig({64, 64});
//   renderer.exec();
//

class RendererNode : public Node {
public:
    RendererNode() {
        initPorts(1, 1);  // 1入力・1出力
    }

    // ========================================
    // 設定API
    // ========================================

    // 仮想スクリーン設定
    // サイズを指定。pivot は setPivot() または setPivotCenter() で別途設定
    void setVirtualScreen(int width, int height) {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    // pivot設定（スクリーン座標でワールド原点の表示位置を指定）
    void setPivot(int_fixed x, int_fixed y) {
        pivotX_ = x;
        pivotY_ = y;
    }
    void setPivot(float x, float y) {
        pivotX_ = float_to_fixed(x);
        pivotY_ = float_to_fixed(y);
    }

    // 中央をpivotに設定（幾何学的中心）
    void setPivotCenter() {
        pivotX_ = to_fixed(virtualWidth_) >> 1;
        pivotY_ = to_fixed(virtualHeight_) >> 1;
    }

    // アクセサ
    std::pair<float, float> getPivot() const {
        return {fixed_to_float(pivotX_), fixed_to_float(pivotY_)};
    }

    // タイル設定
    void setTileConfig(const TileConfig& config) {
        tileConfig_ = config;
    }

    void setTileConfig(int tileWidth, int tileHeight) {
        tileConfig_ = TileConfig(tileWidth, tileHeight);
    }

    // アロケータ設定
    // パイプライン内の各ノードがImageBuffer確保時に使用するアロケータを設定
    // nullptrの場合はデフォルトアロケータを使用
    void setAllocator(core::memory::IAllocator* allocator) {
        pipelineAllocator_ = allocator;
    }

    // デバッグ用チェッカーボード
    void setDebugCheckerboard(bool enabled) {
        debugCheckerboard_ = enabled;
    }

    // デバッグ用DataRange可視化
    // 有効時、getDataRangeの範囲外をマゼンタ、AABB差分を青で塗りつぶし
    void setDebugDataRange(bool enabled) {
        debugDataRange_ = enabled;
    }

    // アクセサ
    int virtualWidth() const { return virtualWidth_; }
    int virtualHeight() const { return virtualHeight_; }
    const TileConfig& tileConfig() const { return tileConfig_; }

    const char* name() const override { return "RendererNode"; }

    // ========================================
    // 実行API
    // ========================================

    // 簡易API（prepare → execute → finalize）
    // 戻り値: PrepareStatus（Success = 0、エラー = 非0）
    PrepareStatus exec() {
        FLEXIMG_METRICS_SCOPE(NodeType::Renderer);

        PrepareStatus result = execPrepare();
        if (result != PrepareStatus::Prepared) {
            // エラー時も状態をリセット
            execFinalize();
            return result;
        }
        execProcess();
        execFinalize();
        return PrepareStatus::Prepared;
    }

    // 詳細API
    // 戻り値: PrepareStatus（Success = 0、エラー = 非0）
    PrepareStatus execPrepare();
    void execProcess();

    void execFinalize() {
        // 上流へ終了を伝播（プル型）
        Node* upstream = upstreamNode(0);
        if (upstream) {
            upstream->pullFinalize();
        }

        // 下流へ終了を伝播（プッシュ型）
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushFinalize();
        }

        // エントリプールを一括解放
        entryPool_.releaseAll();
    }

    // パフォーマンス計測結果を取得
    const PerfMetrics& getPerfMetrics() const {
        return PerfMetrics::instance();
    }

    void resetPerfMetrics() {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().reset();
        FormatMetrics::instance().reset();
#endif
    }

protected:
    // タイル処理（派生クラスでカスタマイズ可能）
    // 注: exec()全体の時間はnodes[NodeType::Renderer]に記録される
    //     各ノードの合計との差分がオーバーヘッド（タイル管理、データ受け渡し等）
    virtual void processTile(int tileX, int tileY) {
        RenderRequest request = createTileRequest(tileX, tileY);

        // 上流からプル
        Node* upstream = upstreamNode(0);
        if (!upstream) {
            context_.resetScanlineResources();
            return;
        }

        RenderResponse& result = upstream->pullProcess(request);

        // デバッグ: DataRange可視化
        if (debugDataRange_) {
            applyDataRangeDebug(upstream, request, result);
        }

        // 下流へプッシュ（有効なデータがなくても常に転送）
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushProcess(result, request);
        }

        // タイル処理完了後にResponseプールをリセット
        context_.resetScanlineResources();
    }

    // デバッグ用: DataRange可視化処理（resultを直接変更）
    void applyDataRangeDebug(Node* upstream, const RenderRequest& request,
                             RenderResponse& result);

private:
    int virtualWidth_ = 0;
    int virtualHeight_ = 0;
    int_fixed pivotX_ = 0;
    int_fixed pivotY_ = 0;
    TileConfig tileConfig_;
    bool debugCheckerboard_ = false;
    bool debugDataRange_ = false;
    core::memory::IAllocator* pipelineAllocator_ = nullptr;  // パイプライン用アロケータ
    ImageBufferEntryPool entryPool_;  // RenderResponse用エントリプール
    RenderContext context_;  // レンダリングコンテキスト（allocator + entryPool を統合）

    // タイルサイズ取得
    // 注: パイプライン上のリクエストは必ずスキャンライン（height=1）
    //     これにより各ノードの最適化が可能になる
    int effectiveTileWidth() const {
        return tileConfig_.isEnabled() ? tileConfig_.tileWidth : virtualWidth_;
    }

    int effectiveTileHeight() const {
        // スキャンライン必須（height=1）
        // TileConfig の tileHeight は無視される
        return 1;
    }

    // タイル数取得
    int calcTileCountX() const {
        int tw = effectiveTileWidth();
        return (tw > 0) ? (virtualWidth_ + tw - 1) / tw : 1;
    }

    int calcTileCountY() const {
        int th = effectiveTileHeight();
        return (th > 0) ? (virtualHeight_ + th - 1) / th : 1;
    }

    // スクリーン全体のRenderRequestを作成
    RenderRequest createScreenRequest() const {
        RenderRequest req;
        req.width = static_cast<int16_t>(virtualWidth_);
        req.height = static_cast<int16_t>(virtualHeight_);
        // スクリーン左上（座標0,0）のワールド座標
        req.origin.x = -pivotX_;
        req.origin.y = -pivotY_;
        return req;
    }

    // タイル用のRenderRequestを作成
    RenderRequest createTileRequest(int tileX, int tileY) const {
        int tw = effectiveTileWidth();
        int th = effectiveTileHeight();
        int tileLeft = tileX * tw;
        int tileTop = tileY * th;

        // タイルサイズ（端の処理）
        int tileW = std::min(tw, virtualWidth_ - tileLeft);
        int tileH = std::min(th, virtualHeight_ - tileTop);

        RenderRequest req;
        req.width = static_cast<int16_t>(tileW);
        req.height = static_cast<int16_t>(tileH);
        // タイル左上のワールド座標 = スクリーン座標 - ワールド原点のスクリーン座標
        req.origin.x = to_fixed(tileLeft) - pivotX_;
        req.origin.y = to_fixed(tileTop) - pivotY_;
        return req;
    }
};

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// RendererNode - 実行API実装
// ============================================================================

PrepareStatus RendererNode::execPrepare() {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    // メトリクスをリセット
    PerfMetrics::instance().reset();
    FormatMetrics::instance().reset();
#endif

    // アロケータ未設定ならDefaultAllocatorを使用
    if (!pipelineAllocator_) {
        pipelineAllocator_ = &core::memory::DefaultAllocator::instance();
    }

    // コンテキストを設定（一括設定でループを1回に削減）
    context_.setup(pipelineAllocator_, &entryPool_);

    // ========================================
    // Step 1: 下流へ準備を伝播（AABB取得用）
    // ========================================
    Node* downstream = downstreamNode(0);
    if (!downstream) {
        return PrepareStatus::NoDownstream;
    }

    PrepareRequest pushReq;
    pushReq.hasPushAffine = false;
    pushReq.context = &context_;

    PrepareResponse pushResult = downstream->pushPrepare(pushReq);
    if (!pushResult.ok()) {
        return pushResult.status;
    }

    // ========================================
    // Step 2: virtualScreenサイズを設定
    // ========================================
    // pivot は独立して機能（setPivot() で設定済み、上書きしない）
    // virtualScreenサイズは未設定の場合のみ自動設定
    if (virtualWidth_ == 0 || virtualHeight_ == 0) {
        virtualWidth_ = pushResult.width;
        virtualHeight_ = pushResult.height;
    }

    // ========================================
    // Step 3: 上流へ準備を伝播
    // ========================================
    Node* upstream = upstreamNode(0);
    if (!upstream) {
        return PrepareStatus::NoUpstream;
    }

    RenderRequest screenInfo = createScreenRequest();
    PrepareRequest pullReq;
    pullReq.width = screenInfo.width;
    pullReq.height = screenInfo.height;
    pullReq.origin = screenInfo.origin;
    pullReq.hasAffine = false;
    pullReq.context = &context_;
    // 下流が希望するフォーマットを上流に伝播
    pullReq.preferredFormat = pushResult.preferredFormat;

    PrepareResponse pullResult = upstream->pullPrepare(pullReq);
    if (!pullResult.ok()) {
        return pullResult.status;
    }

    // 上流情報は将来の最適化に活用
    (void)pullResult;

    return PrepareStatus::Prepared;
}

void RendererNode::execProcess() {
    int tileCountX = calcTileCountX();
    int tileCountY = calcTileCountY();

    for (int ty = 0; ty < tileCountY; ++ty) {
        for (int tx = 0; tx < tileCountX; ++tx) {
            // デバッグ用チェッカーボード: 市松模様でタイルをスキップ
            if (debugCheckerboard_ && ((tx + ty) % 2 == 1)) {
                continue;
            }
            processTile(tx, ty);
        }
    }
}

// デバッグ用: DataRange可視化処理
// - getDataRange()の範囲外: マゼンタ（データがないはずの領域）
// - AABBとgetDataRangeの差分: 青（AABBでは含まれるがgetDataRangeで除外された領域）
// - バッファ境界: オレンジ（バッファの開始/終了位置）
void RendererNode::applyDataRangeDebug(Node* upstream,
                                       const RenderRequest& request,
                                       RenderResponse& result) {
    // 正確な範囲を取得（スキャンライン単位）
    DataRange exactRange = upstream->getDataRange(request);

    // AABBベースの範囲上限を取得
    DataRange aabbRange = upstream->getDataRangeBounds(request);

    // フルサイズのバッファを作成（ゼロ初期化で未定義領域を透明に）
    ImageBuffer debugBuffer(request.width, 1, PixelFormatIDs::RGBA8_Straight,
                           InitPolicy::Zero, pipelineAllocator_);
    uint8_t* dst = static_cast<uint8_t*>(debugBuffer.data());

    // デバッグ色定義（RGBA）
    constexpr uint8_t MAGENTA[] = {255, 0, 255, 255};    // 完全に範囲外
    constexpr uint8_t BLUE[] = {0, 100, 255, 255};       // AABBでは範囲内だがgetDataRangeで範囲外
    constexpr uint8_t GREEN[] = {0, 255, 100, 128};      // getDataRange境界マーカー（半透明）
    constexpr uint8_t ORANGE[] = {255, 140, 0, 200};     // バッファ境界マーカー（半透明）

    // まず全体をデバッグ色で初期化
    for (int_fast16_t x = 0; x < request.width; ++x) {
        const uint8_t* color;
        if (x >= exactRange.startX && x < exactRange.endX) {
            // 正確な範囲内: 後で実データで上書き
            color = nullptr;
        } else if (x >= aabbRange.startX && x < aabbRange.endX) {
            // AABBでは範囲内だがgetDataRangeでは範囲外: 青
            color = BLUE;
        } else {
            // 完全に範囲外: マゼンタ
            color = MAGENTA;
        }

        if (color) {
            uint8_t* p = dst + x * 4;
            p[0] = color[0];
            p[1] = color[1];
            p[2] = color[2];
            p[3] = color[3];
        }
    }

    // 実データをコピー（単一バッファ・フォーマット変換対応）
    if (result.isValid()) {
        const ImageBuffer& buf = result.buffer();

        if (buf.width() > 0) {
            // request.originのピクセル位置
            int requestOriginPixelX = from_fixed(request.origin.x);

            // バッファの開始/終了位置（request座標系）
            int bufStartX = buf.startX() - requestOriginPixelX;
            int bufEndX = buf.endX() - requestOriginPixelX;

            // フォーマット変換の準備
            PixelFormatID srcFormat = buf.formatID();
            FormatConverter converter;
            bool needConvert = (srcFormat != PixelFormatIDs::RGBA8_Straight);
            if (needConvert) {
                converter = resolveConverter(srcFormat, PixelFormatIDs::RGBA8_Straight);
            }

            // バッファの内容をコピー
            const uint8_t* src = static_cast<const uint8_t*>(buf.data());
            int srcBytesPerPixel = getBytesPerPixel(srcFormat);

            for (int i = 0; i < buf.width(); ++i) {
                int dstX = bufStartX + i;
                if (dstX >= 0 && dstX < request.width) {
                    uint8_t* p = dst + dstX * 4;

                    if (needConvert && converter.func) {
                        converter.func(p, src + i * srcBytesPerPixel, 1, &converter.ctx);
                    } else if (!needConvert) {
                        const uint8_t* s = src + i * 4;
                        p[0] = s[0];
                        p[1] = s[1];
                        p[2] = s[2];
                        p[3] = s[3];
                    }
                }
            }

            // バッファ境界マーカーを追加（半透明オレンジ）
            auto addBufferBoundary = [&](int x) {
                if (x >= 0 && x < request.width) {
                    uint8_t* p = dst + x * 4;
                    int alpha = ORANGE[3];
                    int invAlpha = 255 - alpha;
                    p[0] = static_cast<uint8_t>((p[0] * invAlpha + ORANGE[0] * alpha) / 255);
                    p[1] = static_cast<uint8_t>((p[1] * invAlpha + ORANGE[1] * alpha) / 255);
                    p[2] = static_cast<uint8_t>((p[2] * invAlpha + ORANGE[2] * alpha) / 255);
                    p[3] = 255;
                }
            };
            addBufferBoundary(bufStartX);
            if (bufEndX > bufStartX) {
                addBufferBoundary(bufEndX - 1);
            }
        }
    }

    // getDataRange境界マーカーを追加（半透明緑で上書き）
    auto addMarker = [&](int16_t x) {
        if (x >= 0 && x < request.width) {
            uint8_t* p = dst + x * 4;
            // アルファブレンド（50%）
            p[0] = static_cast<uint8_t>((p[0] + GREEN[0]) / 2);
            p[1] = static_cast<uint8_t>((p[1] + GREEN[1]) / 2);
            p[2] = static_cast<uint8_t>((p[2] + GREEN[2]) / 2);
            p[3] = 255;
        }
    };
    addMarker(exactRange.startX);
    if (exactRange.endX > 0) addMarker(static_cast<int16_t>(exactRange.endX - 1));

    // resultをクリアして新しいデバッグバッファを設定
    result.clear();
    debugBuffer.setOrigin(request.origin);
    result.addBuffer(std::move(debugBuffer));
    result.origin = request.origin;
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_RENDERER_NODE_H
