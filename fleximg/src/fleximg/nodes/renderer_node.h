#ifndef FLEXIMG_RENDERER_NODE_H
#define FLEXIMG_RENDERER_NODE_H

#include "../core/node.h"
#include "../core/types.h"
#include "../core/perf_metrics.h"
#include "../core/format_metrics.h"
#include "../image/render_types.h"
#include <algorithm>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// RendererNode - パイプライン発火点ノード
// ========================================================================
//
// パイプライン実行の発火点となるノードです。
// - 入力: 1ポート（上流の処理ノード）
// - 出力: 1ポート（下流のSinkNode/DistributorNode）
// - 仮想スクリーンサイズと基準点を持つ
// - タイル分割処理を制御
//
// 使用例:
//   SourceNode src;
//   AffineNode affine;
//   RendererNode renderer;
//   SinkNode sink(output, 960, 540);
//
//   src >> affine >> renderer >> sink;
//
//   renderer.setVirtualScreen(1920, 1080, 960, 540);
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
    void setVirtualScreen(int width, int height, int_fixed originX, int_fixed originY) {
        virtualWidth_ = width;
        virtualHeight_ = height;
        originX_ = originX;
        originY_ = originY;
    }

    void setVirtualScreen(int width, int height) {
        setVirtualScreen(width, height,
                         to_fixed(width / 2),
                         to_fixed(height / 2));
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

    // アクセサ
    int virtualWidth() const { return virtualWidth_; }
    int virtualHeight() const { return virtualHeight_; }
    int_fixed originX() const { return originX_; }
    int_fixed originY() const { return originY_; }
    float originXf() const { return fixed_to_float(originX_); }
    float originYf() const { return fixed_to_float(originY_); }
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
        if (!upstream) return;

        RenderResponse result = upstream->pullProcess(request);

        // 下流へプッシュ（有効なデータがなくても常に転送）
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushProcess(std::move(result), request);
        }
    }

private:
    int virtualWidth_ = 0;
    int virtualHeight_ = 0;
    int_fixed originX_ = 0;
    int_fixed originY_ = 0;
    TileConfig tileConfig_;
    bool debugCheckerboard_ = false;
    core::memory::IAllocator* pipelineAllocator_ = nullptr;  // パイプライン用アロケータ

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
        req.origin.x = originX_;
        req.origin.y = originY_;
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
        req.origin.x = originX_ - to_fixed(tileLeft);
        req.origin.y = originY_ - to_fixed(tileTop);
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

    // ========================================
    // Step 1: 下流へ準備を伝播（AABB取得用）
    // ========================================
    Node* downstream = downstreamNode(0);
    if (!downstream) {
        return PrepareStatus::NoDownstream;
    }

    PrepareRequest pushReq;
    pushReq.hasPushAffine = false;
    pushReq.allocator = pipelineAllocator_;

    PrepareResponse pushResult = downstream->pushPrepare(pushReq);
    if (!pushResult.ok()) {
        return pushResult.status;
    }

    // ========================================
    // Step 2: virtualScreenを自動設定（未設定の場合）
    // ========================================
    if (virtualWidth_ == 0 || virtualHeight_ == 0) {
        // 下流から返されたAABBでvirtualScreenを設定
        virtualWidth_ = pushResult.width;
        virtualHeight_ = pushResult.height;
        originX_ = pushResult.origin.x;
        originY_ = pushResult.origin.y;
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
    pullReq.allocator = pipelineAllocator_;
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

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_RENDERER_NODE_H
