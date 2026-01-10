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

        // 入力をRGBA8_Straightに変換（同じフォーマットならムーブ）
        ImageBuffer working = std::move(input.buffer).toFormat(PixelFormatIDs::RGBA8_Straight);
        ViewPort workingView = working.view();

        // 出力バッファ作成（入力と同じサイズ）
        ImageBuffer blurred(working.width(), working.height(), PixelFormatIDs::RGBA8_Straight);
        ViewPort blurredView = blurred.view();

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::BoxBlur].recordAlloc(
            blurred.totalBytes(), blurred.width(), blurred.height());
#endif

        // フィルタ適用
        filters::boxBlur(blurredView, workingView, radius_);

        // マージン分を切り出し（要求範囲のみ抽出）
        int margin = radius_;
        if (margin > 0) {
            // 入力画像内での切り出し開始位置
            // 新座標系: origin はバッファ内基準点位置
            // startX = input.origin.x - request.origin.x
            int startX = static_cast<int>(input.origin.x - request.origin.x);
            int startY = static_cast<int>(input.origin.y - request.origin.y);

            if (startX >= 0 && startY >= 0 &&
                startX + request.width <= blurred.width() &&
                startY + request.height <= blurred.height()) {

                ImageBuffer cropped(request.width, request.height, PixelFormatIDs::RGBA8_Straight);
                ViewPort croppedView = cropped.view();
#ifdef FLEXIMG_DEBUG_PERF_METRICS
                PerfMetrics::instance().nodes[NodeType::BoxBlur].recordAlloc(
                    cropped.totalBytes(), cropped.width(), cropped.height());
#endif
                view_ops::copy(croppedView, 0, 0, blurredView, startX, startY,
                              request.width, request.height);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
                auto& metrics = PerfMetrics::instance().nodes[NodeType::BoxBlur];
                metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock::now() - start).count();
                metrics.count++;
#endif

                // 要求と同じ基準点位置を返す
                return RenderResult(std::move(cropped), request.origin);
            }
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::BoxBlur];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(blurred), input.origin);
    }

private:
    int radius_ = 5;
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_BOX_BLUR_NODE_H
