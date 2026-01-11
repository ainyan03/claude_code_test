#ifndef FLEXIMG_BOX_BLUR_NODE_H
#define FLEXIMG_BOX_BLUR_NODE_H

#include "filter_node_base.h"
#include "../operations/filters.h"
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// BoxBlurNode - ボックスブラーフィルタノード
// ========================================================================
//
// 入力画像にボックスブラー（平均化フィルタ）を適用します。
// - radius: ブラー半径（カーネルサイズ = 2 * radius + 1）
//
// 使用例:
//   BoxBlurNode blur;
//   blur.setRadius(5);  // 半径5ピクセル
//   src >> blur >> sink;
//

class BoxBlurNode : public FilterNodeBase {
public:
    // ========================================
    // パラメータ設定
    // ========================================

    void setRadius(int radius) { radius_ = radius; }
    int radius() const { return radius_; }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "BoxBlurNode"; }

protected:
    int computeInputMargin() const override { return radius_; }
    int nodeTypeForMetrics() const override { return NodeType::BoxBlur; }

    RenderResult process(RenderResult&& input,
                        const RenderRequest& request) override {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
#endif

        // 入力をRGBA8_Straightに変換（読み取り専用なので参照優先）
        ImageBuffer working = convertFormat(std::move(input.buffer),
                                            PixelFormatIDs::RGBA8_Straight,
                                            FormatConversion::PreferReference);

        // inputReq を再計算（FilterNodeBase が上流に要求したサイズ）
        RenderRequest inputReq = request.expand(radius_);

        // 作業バッファ（inputReq サイズ）
        ImageBuffer blurred(inputReq.width, inputReq.height, PixelFormatIDs::RGBA8_Straight,
                            InitPolicy::Uninitialized);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::BoxBlur].recordAlloc(
            blurred.totalBytes(), blurred.width(), blurred.height());
#endif

        // srcOffset 計算: inputのinputReq座標系での位置
        int srcOffsetX = from_fixed8(inputReq.origin.x - input.origin.x);
        int srcOffsetY = from_fixed8(inputReq.origin.y - input.origin.y);

        // 透明拡張ブラー（入力範囲外は透明として処理）
        ViewPort blurredView = blurred.view();
        ViewPort workingView = working.view();
        filters::boxBlurWithPadding(blurredView, workingView,
                                    srcOffsetX, srcOffsetY, radius_);

        // request サイズに切り出し（inputReq の中央 radius 分内側）
        ImageBuffer output(request.width, request.height, PixelFormatIDs::RGBA8_Straight,
                           InitPolicy::Uninitialized);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::BoxBlur].recordAlloc(
            output.totalBytes(), output.width(), output.height());
#endif

        ViewPort outputView = output.view();
        view_ops::copy(outputView, 0, 0, blurredView,
                      radius_, radius_, request.width, request.height);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::BoxBlur];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(output), request.origin);
    }

private:
    int radius_ = 5;
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_BOX_BLUR_NODE_H
