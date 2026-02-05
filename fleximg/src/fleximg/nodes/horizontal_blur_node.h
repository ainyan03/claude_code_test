#ifndef FLEXIMG_HORIZONTAL_BLUR_NODE_H
#define FLEXIMG_HORIZONTAL_BLUR_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include <algorithm>  // for std::min, std::max
#include <cstring>    // for std::memcpy

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// HorizontalBlurNode - 水平方向ブラーフィルタノード
// ========================================================================
//
// 入力画像に水平方向のボックスブラー（平均化フィルタ）を適用します。
// - radius: ブラー半径（0-127、カーネルサイズ = 2 * radius + 1）
// - passes: ブラー適用回数（1-3、デフォルト1）
//
// マルチパス処理:
// - passes=3で3回水平ブラーを適用（ガウシアン近似）
// - 各パスは独立してブラー処理を行う（パイプライン方式）
// - pull型: 上流にマージン付き要求、下流には元サイズで返却
// - push型: 入力を拡張して下流に配布
//
// メモリ消費量:
// - 水平ブラーはスキャンライン処理のため、メモリ消費は少ない
// - 1行分のバッファ: width * 4 bytes 程度
//
// 使用例:
//   HorizontalBlurNode hblur;
//   hblur.setRadius(6);
//   hblur.setPasses(3);  // ガウシアン近似
//   src >> hblur >> sink;
//
// VerticalBlurNodeと組み合わせて2次元ガウシアン近似:
//   src >> hblur(r=6, p=3) >> vblur(r=6, p=3) >> sink;
//

class HorizontalBlurNode : public Node {
public:
    HorizontalBlurNode() {
        initPorts(1, 1);
    }

    // ========================================
    // パラメータ設定
    // ========================================

    // パラメータ上限
    static constexpr int kMaxRadius = 127;  // 実用上十分、メモリ消費も許容範囲
    static constexpr int kMaxPasses = 3;    // ガウシアン近似に十分

    void setRadius(int radius) {
        radius_ = (radius < 0) ? 0 : (radius > kMaxRadius) ? kMaxRadius : radius;
    }

    void setPasses(int passes) {
        passes_ = (passes < 1) ? 1 : (passes > kMaxPasses) ? kMaxPasses : passes;
    }

    int radius() const { return radius_; }
    int passes() const { return passes_; }
    int kernelSize() const { return radius_ * 2 + 1; }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "HorizontalBlurNode"; }

    // getDataRange: 上流データ範囲をブラー分拡張して返す
    DataRange getDataRange(const RenderRequest& request) const override {
        if (radius_ == 0 || passes_ == 0) {
            // パススルー時は上流の範囲をそのまま返す
            Node* upstream = upstreamNode(0);
            if (upstream) {
                return upstream->getDataRange(request);
            }
            return DataRange();
        }

        // 上流への拡張リクエストを作成（左方向に拡大）
        int totalMargin = radius_ * passes_;
        RenderRequest inputReq;
        inputReq.width = static_cast<int16_t>(request.width + totalMargin * 2);
        inputReq.height = 1;
        inputReq.origin.x = request.origin.x - to_fixed(totalMargin);
        inputReq.origin.y = request.origin.y;

        Node* upstream = upstreamNode(0);
        if (!upstream) {
            return DataRange();
        }

        // 上流のデータ範囲を取得
        DataRange upstreamRange = upstream->getDataRange(inputReq);
        if (!upstreamRange.hasData()) {
            return DataRange();
        }

        // ブラー処理後の範囲を計算
        // upstreamRangeはinputReq座標系 → request座標系への変換: X - totalMargin
        // さらにブラー処理による両側拡張: -totalMargin / +totalMargin
        // 結果: startX - 2*totalMargin, endX
        int16_t blurredStartX = static_cast<int16_t>(upstreamRange.startX - totalMargin * 2);
        int16_t blurredEndX = static_cast<int16_t>(upstreamRange.endX);

        // request範囲にクランプ
        if (blurredStartX < 0) blurredStartX = 0;
        if (blurredEndX > request.width) blurredEndX = request.width;

        if (blurredStartX >= blurredEndX) {
            return DataRange();
        }

        return DataRange{blurredStartX, blurredEndX};
    }

protected:
    int nodeTypeForMetrics() const override { return NodeType::HorizontalBlur; }

    // ========================================
    // Template Method フック
    // ========================================

    // onPullPrepare: AABBをX方向に拡張
    PrepareResponse onPullPrepare(const PrepareRequest& request) override;

    // onPullProcess: 水平ブラー処理
    RenderResponse& onPullProcess(const RenderRequest& request) override;

    // onPushProcess: 水平ブラー処理（push型）
    void onPushProcess(RenderResponse& input, const RenderRequest& request) override;

private:
    int radius_ = 5;
    int passes_ = 1;  // 1-3の範囲、デフォルト1

    // 水平方向ブラー処理（共通）
    void applyHorizontalBlur(const ViewPort& srcView, int inputOffset, ImageBuffer& output);

    // ブラー済みピクセルを書き込み
    void writeBlurredPixel(uint8_t* row, int x, uint32_t sumR, uint32_t sumG,
                           uint32_t sumB, uint32_t sumA) {
        int off = x * 4;
        uint32_t ks = static_cast<uint32_t>(kernelSize());
        if (sumA > 0) {
            row[off]     = static_cast<uint8_t>(sumR / sumA);
            row[off + 1] = static_cast<uint8_t>(sumG / sumA);
            row[off + 2] = static_cast<uint8_t>(sumB / sumA);
            row[off + 3] = static_cast<uint8_t>(sumA / ks);
        } else {
            row[off] = row[off + 1] = row[off + 2] = row[off + 3] = 0;
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// HorizontalBlurNode - Template Method フック実装
// ============================================================================

PrepareResponse HorizontalBlurNode::onPullPrepare(const PrepareRequest& request) {
    // 上流へ伝播
    Node* upstream = upstreamNode(0);
    if (!upstream) {
        // 上流なし: サイズ0を返す
        PrepareResponse result;
        result.status = PrepareStatus::Prepared;
        return result;
    }

    PrepareResponse upstreamResult = upstream->pullPrepare(request);
    if (!upstreamResult.ok()) {
        return upstreamResult;
    }

    // radius=0の場合はパススルー
    if (radius_ == 0) {
        return upstreamResult;
    }

    // 水平ぼかしはX方向に radius * passes 分拡張する
    // AABBの幅を拡張し、originのXをシフト（左方向に拡大）
    int expansion = radius_ * passes_;
    upstreamResult.width = static_cast<int16_t>(upstreamResult.width + expansion * 2);
    upstreamResult.origin.x = upstreamResult.origin.x - to_fixed(expansion);

    return upstreamResult;
}

RenderResponse& HorizontalBlurNode::onPullProcess(const RenderRequest& request) {
    Node* upstream = upstreamNode(0);
    if (!upstream) return makeEmptyResponse(request.origin);

    // radius=0またはpasses=0の場合は処理をスキップしてスルー出力
    if (radius_ == 0 || passes_ == 0) {
        return upstream->pullProcess(request);
    }

    // マージンを計算して上流への要求を拡大
    int totalMargin = radius_ * passes_;  // 片側のマージン
    RenderRequest inputReq;
    inputReq.width = static_cast<int16_t>(request.width + totalMargin * 2);  // 両側にマージンを追加
    inputReq.height = 1;
    inputReq.origin.x = request.origin.x - to_fixed(totalMargin);  // 左側にマージン分拡張
    inputReq.origin.y = request.origin.y;

    // 上流のデータ範囲を取得して出力バッファサイズを最適化
    DataRange upstreamRange = upstream->getDataRange(inputReq);
    if (!upstreamRange.hasData()) {
        return makeEmptyResponse(request.origin);
    }

    RenderResponse& input = upstream->pullProcess(inputReq);
    if (!input.isValid()) return makeEmptyResponse(request.origin);

    // ImageBufferSetの場合はconsolidate()して単一バッファに変換
    consolidateIfNeeded(input);

    FLEXIMG_METRICS_SCOPE(NodeType::HorizontalBlur);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    auto& metrics = PerfMetrics::instance().nodes[NodeType::HorizontalBlur];
    metrics.requestedPixels += static_cast<uint64_t>(request.width) * 1;
    metrics.usedPixels += static_cast<uint64_t>(inputReq.width) * 1;
#endif

    // RGBA8_Straightに変換
    ImageBuffer buffer = convertFormat(ImageBuffer(input.single()),
                                       PixelFormatIDs::RGBA8_Straight);

    // 上流から返されたoriginを保存
    Point currentOrigin = input.origin;

    // passes回、水平ブラーを適用（各パスで拡張＋origin調整）
    for (int pass = 0; pass < passes_; pass++) {
        ViewPort srcView = buffer.view();
        int inputWidth = srcView.width;
        int outputWidth = inputWidth + radius_ * 2;

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        if (pass == 0) {
            metrics.recordAlloc(static_cast<size_t>(outputWidth) * 4, outputWidth, 1);
        }
#endif

        // 出力バッファを確保
        ImageBuffer output(outputWidth, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized);

        // 水平方向スライディングウィンドウでブラー処理
        // inputOffset = -radius (出力を左に拡張)
        applyHorizontalBlur(srcView, -radius_, output);

        // origin.xを左に拡張した分だけ減らす（ワールド座標で左に移動）
        currentOrigin.x = currentOrigin.x - to_fixed(radius_);

        buffer = std::move(output);
    }

    // ブラー処理後の範囲を計算
    // upstreamRangeはinputReq座標系 → request座標系への変換: X - totalMargin
    // さらにブラー処理による両側拡張: -totalMargin / +totalMargin
    // 結果: startX - 2*totalMargin, endX
    int16_t blurredStartX = static_cast<int16_t>(upstreamRange.startX - totalMargin * 2);
    int16_t blurredEndX = static_cast<int16_t>(upstreamRange.endX);
    // request範囲にクランプ
    if (blurredStartX < 0) blurredStartX = 0;
    if (blurredEndX > request.width) blurredEndX = request.width;

    if (blurredStartX >= blurredEndX) {
        return makeEmptyResponse(request.origin);
    }

    int16_t outputWidth = blurredEndX - blurredStartX;

    // origin座標を基準にクロップ位置を計算
    int_fixed offsetX = currentOrigin.x - request.origin.x;
    int cropOffset = from_fixed(offsetX);

    // 出力バッファを確保（必要幅のみ、ゼロ初期化）
    // 出力バッファ左端のワールド座標 = リクエスト左端 + blurredStartX
    int_fixed outputOriginX = request.origin.x + to_fixed(blurredStartX);
    ImageBuffer output(outputWidth, 1, PixelFormatIDs::RGBA8_Straight,
                      InitPolicy::Zero);
    const uint8_t* srcRow = static_cast<const uint8_t*>(buffer.view().data);
    uint8_t* dstRow = static_cast<uint8_t*>(output.view().data);

    // クロップ範囲を計算（境界チェック付き）
    // cropOffset = ブラー後バッファ左端 - リクエスト左端（ワールド座標差）
    // blurredStartX = 出力範囲の開始位置（リクエスト座標系）
    // ブラー後バッファ内での位置 = blurredStartX - cropOffset
    int srcStartX = std::max(0, blurredStartX - cropOffset);
    int dstStartX = std::max(0, cropOffset - blurredStartX);
    int copyWidth = std::min(static_cast<int>(buffer.width()) - srcStartX,
                             static_cast<int>(outputWidth) - dstStartX);

    // 有効な範囲をコピー（範囲外は既にゼロ初期化済み）
    if (copyWidth > 0) {
        std::memcpy(dstRow + dstStartX * 4,
                   srcRow + srcStartX * 4,
                   static_cast<size_t>(copyWidth) * 4);
    }

    return makeResponse(std::move(output), Point{outputOriginX, request.origin.y});
}

void HorizontalBlurNode::onPushProcess(RenderResponse& input, const RenderRequest& request) {
    // radius=0またはpasses=0の場合はスルー
    if (radius_ == 0 || passes_ == 0) {
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushProcess(input, request);
        }
        return;
    }

    if (!input.isValid()) {
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushProcess(input, request);
        }
        return;
    }

    // ImageBufferSetの場合はconsolidate()して単一バッファに変換
    consolidateIfNeeded(input);

    FLEXIMG_METRICS_SCOPE(NodeType::HorizontalBlur);

    // RGBA8_Straightに変換
    ImageBuffer buffer = convertFormat(ImageBuffer(input.single()),
                                       PixelFormatIDs::RGBA8_Straight);
    Point currentOrigin = input.origin;

    // passes回、水平ブラーを適用
    for (int pass = 0; pass < passes_; pass++) {
        ViewPort srcView = buffer.view();
        int inputWidth = srcView.width;
        int outputWidth = inputWidth + radius_ * 2;

        // 出力バッファを確保
        ImageBuffer output(outputWidth, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized);

        // 水平方向スライディングウィンドウでブラー処理
        // push型では inputOffset = -radius
        applyHorizontalBlur(srcView, -radius_, output);

        // origin.xを左に拡張した分だけ減らす（ワールド座標で左に移動）
        currentOrigin.x = currentOrigin.x - to_fixed(radius_);

        buffer = std::move(output);
    }

    // 下流にpush
    Node* downstream = downstreamNode(0);
    if (downstream) {
        RenderRequest outReq = request;
        outReq.width = static_cast<int16_t>(buffer.width());
        RenderResponse& resp = makeResponse(std::move(buffer), currentOrigin);
        downstream->pushProcess(resp, outReq);
    }
}

// ============================================================================
// HorizontalBlurNode - private ヘルパーメソッド実装
// ============================================================================

// 水平方向ブラー処理（共通）
// inputOffset: 出力x=0に対応する入力のカーネル中心位置
void HorizontalBlurNode::applyHorizontalBlur(const ViewPort& srcView, int inputOffset, ImageBuffer& output) {
    const uint8_t* srcRow = static_cast<const uint8_t*>(srcView.data);
    uint8_t* dstRow = static_cast<uint8_t*>(output.view().data);
    int inputWidth = srcView.width;
    int outputWidth = output.width();

    // 初期ウィンドウの合計（出力x=0に対応）
    uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;

    for (auto kx = static_cast<int_fast16_t>(-radius_); kx <= radius_; kx++) {
        auto srcX = static_cast<int_fast16_t>(inputOffset + kx);
        if (srcX >= 0 && srcX < inputWidth) {
            auto off = srcX * 4;
            uint32_t a = srcRow[off + 3];
            sumR += srcRow[off] * a;
            sumG += srcRow[off + 1] * a;
            sumB += srcRow[off + 2] * a;
            sumA += a;
        }
    }
    writeBlurredPixel(dstRow, 0, sumR, sumG, sumB, sumA);

    // スライディング: x = 1 to outputWidth-1
    for (int_fast16_t x = 1; x < outputWidth; x++) {
        // 出ていくピクセル
        auto oldSrcX = static_cast<int_fast16_t>(inputOffset + x - 1 - radius_);
        if (oldSrcX >= 0 && oldSrcX < inputWidth) {
            auto off = oldSrcX * 4;
            uint32_t a = srcRow[off + 3];
            sumR -= srcRow[off] * a;
            sumG -= srcRow[off + 1] * a;
            sumB -= srcRow[off + 2] * a;
            sumA -= a;
        }

        // 入ってくるピクセル
        auto newSrcX = static_cast<int_fast16_t>(inputOffset + x + radius_);
        if (newSrcX >= 0 && newSrcX < inputWidth) {
            auto off = newSrcX * 4;
            uint32_t a = srcRow[off + 3];
            sumR += srcRow[off] * a;
            sumG += srcRow[off + 1] * a;
            sumB += srcRow[off + 2] * a;
            sumA += a;
        }

        writeBlurredPixel(dstRow, x, sumR, sumG, sumB, sumA);
    }
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_HORIZONTAL_BLUR_NODE_H
