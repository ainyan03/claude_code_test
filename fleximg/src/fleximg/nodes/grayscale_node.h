#ifndef FLEXIMG_GRAYSCALE_NODE_H
#define FLEXIMG_GRAYSCALE_NODE_H

#include "filter_node_base.h"
#include "../operations/filters.h"
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// GrayscaleNode - グレースケールフィルタノード
// ========================================================================
//
// 入力画像をグレースケールに変換します。
// パラメータなし。
//
// 使用例:
//   GrayscaleNode grayscale;
//   src >> grayscale >> sink;
//

class GrayscaleNode : public FilterNodeBase {
public:
    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "GrayscaleNode"; }

protected:
    int nodeTypeForMetrics() const override { return NodeType::Grayscale; }

    RenderResult process(RenderResult&& input,
                        const RenderRequest& request) override {
        (void)request;  // このフィルタでは未使用

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
#endif

        // 入力をRGBA8_Straightに変換（メトリクス記録付き）
        ImageBuffer working = convertFormat(std::move(input.buffer), PixelFormatIDs::RGBA8_Straight);
        ViewPort workingView = working.view();

        // インプレース編集（dst==src）
        filters::grayscale(workingView, workingView);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::Grayscale];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(working), input.origin);
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_GRAYSCALE_NODE_H
