#ifndef FLEXIMG_COMPOSITE_NODE_H
#define FLEXIMG_COMPOSITE_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include "../image/pixel_format.h"
#include "../operations/canvas_utils.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// CompositeNode - 合成ノード
// ========================================================================
//
// 複数の入力画像を合成して1つの出力を生成します。
// - 入力: コンストラクタで指定（デフォルト2）
// - 出力: 1ポート
//
// 合成方式:
// - デフォルト: 8bit Straight形式（省メモリ、4バイト/ピクセル）
// - FLEXIMG_ENABLE_PREMUL定義時: 16bit Premultiplied形式（高精度、8バイト/ピクセル）
//
// 合成順序（under合成）:
// - 入力ポート0が最前面（最初に描画）
// - 入力ポート1以降が順に背面に合成
// - 既に不透明なピクセルは後のレイヤー処理をスキップ
//
// 使用例:
//   CompositeNode composite(3);  // 3入力
//   fg >> composite;             // ポート0（最前面）
//   mid.connectTo(composite, 1); // ポート1（中間）
//   bg.connectTo(composite, 2);  // ポート2（最背面）
//   composite >> sink;
//

class CompositeNode : public Node {
public:
    explicit CompositeNode(int inputCount = 2) {
        initPorts(inputCount, 1);  // 入力N、出力1
    }

    // ========================================
    // 入力管理
    // ========================================

    // 入力数を変更（既存接続は維持）
    void setInputCount(int count) {
        if (count < 1) count = 1;
        inputs_.resize(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            if (inputs_[static_cast<size_t>(i)].owner == nullptr) {
                inputs_[static_cast<size_t>(i)] = Port(this, i);
            }
        }
    }

    int inputCount() const {
        return static_cast<int>(inputs_.size());
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "CompositeNode"; }

    // ========================================
    // Template Method フック
    // ========================================

    // onPullPrepare: 全上流ノードにPrepareRequestを伝播
    PrepareResponse onPullPrepare(const PrepareRequest& request) override;

    // onPullFinalize: 全上流ノードに終了を伝播
    void onPullFinalize() override;

    // onPullProcess: 複数の上流から画像を取得してunder合成
    RenderResponse onPullProcess(const RenderRequest& request) override;

protected:
    int nodeTypeForMetrics() const override { return NodeType::Composite; }
};

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// CompositeNode - Template Method フック実装
// ============================================================================

PrepareResponse CompositeNode::onPullPrepare(const PrepareRequest& request) {
    PrepareResponse merged;
    merged.status = PrepareStatus::Prepared;
    int validUpstreamCount = 0;

    // AABB和集合計算用（基準点からの相対座標）
    float minX = 0, minY = 0, maxX = 0, maxY = 0;

    // 全上流へ伝播し、結果をマージ（AABB和集合）
    int numInputs = inputCount();
    for (int i = 0; i < numInputs; ++i) {
        Node* upstream = upstreamNode(i);
        if (upstream) {
            // 各上流に同じリクエストを伝播
            // 注意: アフィン行列は共有されるため、各上流で同じ変換が適用される
            PrepareResponse result = upstream->pullPrepare(request);
            if (!result.ok()) {
                return result;  // エラーを伝播
            }

            // 各結果のAABBを基準点からの相対座標に変換
            float left = -fixed_to_float(result.origin.x);
            float top = -fixed_to_float(result.origin.y);
            float right = left + static_cast<float>(result.width);
            float bottom = top + static_cast<float>(result.height);

            if (validUpstreamCount == 0) {
                // 最初の結果でベースを初期化
                merged.preferredFormat = result.preferredFormat;
                minX = left;
                minY = top;
                maxX = right;
                maxY = bottom;
            } else {
                // 和集合（各辺のmin/max）
                if (left < minX) minX = left;
                if (top < minY) minY = top;
                if (right > maxX) maxX = right;
                if (bottom > maxY) maxY = bottom;
            }
            ++validUpstreamCount;
        }
    }

    if (validUpstreamCount > 0) {
        // 和集合結果をPrepareResponseに設定
        merged.width = static_cast<int16_t>(std::ceil(maxX - minX));
        merged.height = static_cast<int16_t>(std::ceil(maxY - minY));
        merged.origin.x = float_to_fixed(-minX);
        merged.origin.y = float_to_fixed(-minY);

        // フォーマット決定:
        // - 上流が1つのみ → パススルー（merged.preferredFormatはそのまま）
        // - 上流が複数 → 合成フォーマットを使用
        if (validUpstreamCount > 1) {
#ifdef FLEXIMG_ENABLE_PREMUL
            merged.preferredFormat = PixelFormatIDs::RGBA16_Premultiplied;
#else
            merged.preferredFormat = PixelFormatIDs::RGBA8_Straight;
#endif
        }
    } else {
        // 上流がない場合はサイズ0を返す
        // width/height/originはデフォルト値（0）のまま
    }

    // 準備処理
    RenderRequest screenInfo;
    screenInfo.width = request.width;
    screenInfo.height = request.height;
    screenInfo.origin = request.origin;
    prepare(screenInfo);

    return merged;
}

void CompositeNode::onPullFinalize() {
    finalize();
    int numInputs = inputCount();
    for (int i = 0; i < numInputs; ++i) {
        Node* upstream = upstreamNode(i);
        if (upstream) {
            upstream->pullFinalize();
        }
    }
}

// onPullProcess: 複数の上流から画像を取得してunder合成
// under合成: 手前から奥へ処理し、不透明な部分は後のレイヤーをスキップ
// 最適化: height=1 前提（パイプラインは常にスキャンライン単位で処理）
RenderResponse CompositeNode::onPullProcess(const RenderRequest& request) {
    int numInputs = inputCount();
    if (numInputs == 0) return RenderResponse();

    // 各上流のデータ範囲を収集し、和集合を計算
    int16_t canvasStartX = request.width;  // 和集合の開始X
    int16_t canvasEndX = 0;                // 和集合の終了X
    for (int i = 0; i < numInputs; i++) {
        Node* upstream = upstreamNode(i);
        if (!upstream) continue;
        DataRange range = upstream->getDataRange(request);
        if (range.hasData()) {
            if (range.startX < canvasStartX) canvasStartX = range.startX;
            if (range.endX > canvasEndX) canvasEndX = range.endX;
        }
    }

    // 有効なデータがない場合は空を返す
    if (canvasStartX >= canvasEndX) {
        return RenderResponse();
    }

    int16_t canvasWidth = canvasEndX - canvasStartX;

    // バッファ内基準点位置（固定小数点 Q16.16）
    // canvasStartX分だけシフト
    int_fixed canvasOriginX = request.origin.x - to_fixed(canvasStartX);
    int_fixed canvasOriginY = request.origin.y;

    // キャンバスを作成（height=1、必要幅のみ確保）
#ifdef FLEXIMG_ENABLE_PREMUL
    // 16bit Premultiplied形式: 8バイト/ピクセル（高精度）
    constexpr size_t bytesPerPixel = 8;
    PixelFormatID canvasFormat = PixelFormatIDs::RGBA16_Premultiplied;
#else
    // 8bit Straight形式: 4バイト/ピクセル（省メモリ）
    constexpr size_t bytesPerPixel = 4;
    PixelFormatID canvasFormat = PixelFormatIDs::RGBA8_Straight;
#endif
    ImageBuffer canvasBuf(canvasWidth, 1, canvasFormat,
                          InitPolicy::Uninitialized, allocator());
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    PerfMetrics::instance().nodes[NodeType::Composite].recordAlloc(
        canvasBuf.totalBytes(), canvasBuf.width(), canvasBuf.height());
#endif
    uint8_t* canvasRow = static_cast<uint8_t*>(canvasBuf.view().pixelAt(0, 0));

    // 有効範囲を追跡（バッファ座標系、0〜canvasWidth）
    int validStartX = canvasWidth;  // 左端（右端で初期化）
    int validEndX = 0;              // 右端（左端で初期化）

    // under合成: 入力を順に評価して合成
    // 入力ポート0が最前面、以降が背面
    bool isFirstContent = true;
    for (int i = 0; i < numInputs; i++) {
        Node* upstream = upstreamNode(i);
        if (!upstream) continue;

        // 範囲外の上流はスキップ（prepare時のAABBで判定）
        DataRange range = upstream->getDataRange(request);
        if (!range.hasData()) continue;

        // 上流を評価（計測対象外）
        RenderResponse inputResult = upstream->pullProcess(request);

        // 空入力はスキップ
        if (!inputResult.isValid()) continue;

        // ここからCompositeNode自身の処理を計測
        FLEXIMG_METRICS_SCOPE(NodeType::Composite);

        // X方向オフセット計算（Y方向は不要、height=1前提）
        // canvasOriginXはcanvasStartX分シフト済みなので、dstStartXはバッファ座標系
        int offsetX = from_fixed(canvasOriginX - inputResult.origin.x);
        int srcStartX = std::max(0, -offsetX);
        int dstStartX = std::max(0, offsetX);
        int copyWidth = std::min(inputResult.view().width - srcStartX,
                                 static_cast<int>(canvasWidth) - dstStartX);
        if (copyWidth <= 0) continue;

        const void* srcRow = inputResult.view().pixelAt(srcStartX, 0);
        uint8_t* dstRow = canvasRow + static_cast<size_t>(dstStartX) * bytesPerPixel;
        PixelFormatID srcFmt = inputResult.view().formatID;

        // 今回の描画範囲
        int curEndX = dstStartX + copyWidth;

        if (isFirstContent) {
            // 初回: 変換コピーのみ（余白ゼロクリア不要、cropViewで切り捨て）
            FLEXIMG_NAMESPACE::convertFormat(srcRow, srcFmt, dstRow, canvasFormat, copyWidth);
            validStartX = dstStartX;
            validEndX = curEndX;
            isFirstContent = false;
        } else {
            // 2回目以降: 範囲拡張があれば拡張部分をゼロクリア
            if (dstStartX < validStartX) {
                std::memset(canvasRow + static_cast<size_t>(dstStartX) * bytesPerPixel, 0,
                            static_cast<size_t>(validStartX - dstStartX) * bytesPerPixel);
                validStartX = dstStartX;
            }
            if (curEndX > validEndX) {
                std::memset(canvasRow + static_cast<size_t>(validEndX) * bytesPerPixel, 0,
                            static_cast<size_t>(curEndX - validEndX) * bytesPerPixel);
                validEndX = curEndX;
            }

            // under合成
#ifdef FLEXIMG_ENABLE_PREMUL
            // 16bit Premultiplied形式でのunder合成
            if (srcFmt->blendUnderPremul) {
                srcFmt->blendUnderPremul(dstRow, srcRow, copyWidth, nullptr);
            } else if (srcFmt->toPremul) {
                // blendUnderPremulがない場合、一時バッファでPremul変換してからブレンド
                ImageBuffer tempBuf(copyWidth, 1, PixelFormatIDs::RGBA16_Premultiplied,
                                    InitPolicy::Uninitialized, allocator());
                srcFmt->toPremul(tempBuf.view().pixelAt(0, 0), srcRow, copyWidth, nullptr);
                PixelFormatIDs::RGBA16_Premultiplied->blendUnderPremul(
                    dstRow, tempBuf.view().pixelAt(0, 0), copyWidth, nullptr);
            }
#else
            // 8bit Straight形式でのunder合成
            if (srcFmt->blendUnderStraight) {
                srcFmt->blendUnderStraight(dstRow, srcRow, copyWidth, nullptr);
            } else if (srcFmt->toStraight) {
                // blendUnderStraightがない場合、一時バッファでStraight変換してからブレンド
                ImageBuffer tempBuf(copyWidth, 1, PixelFormatIDs::RGBA8_Straight,
                                    InitPolicy::Uninitialized, allocator());
                srcFmt->toStraight(tempBuf.view().pixelAt(0, 0), srcRow, copyWidth, nullptr);
                PixelFormatIDs::RGBA8_Straight->blendUnderStraight(
                    dstRow, tempBuf.view().pixelAt(0, 0), copyWidth, nullptr);
            }
#endif
            // 対応関数がない場合はスキップ
        }
    }

    // 全ての入力が空だった場合（事前判定で回避されるはずだが念のため）
    if (validStartX >= validEndX) {
        return RenderResponse();
    }

    // 余白をゼロクリアしてバッファを返す
    if (validStartX > 0) {
        std::memset(canvasRow, 0, static_cast<size_t>(validStartX) * bytesPerPixel);
    }
    if (validEndX < canvasWidth) {
        std::memset(canvasRow + static_cast<size_t>(validEndX) * bytesPerPixel, 0,
                    static_cast<size_t>(canvasWidth - validEndX) * bytesPerPixel);
    }
    return RenderResponse(std::move(canvasBuf), Point{canvasOriginX, canvasOriginY});
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_COMPOSITE_NODE_H
