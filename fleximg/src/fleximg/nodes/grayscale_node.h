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

        // 入力をRGBA8_Straightに変換（同じフォーマットならムーブ）
        ImageBuffer working = std::move(input.buffer).toFormat(PixelFormatIDs::RGBA8_Straight);
        ViewPort workingView = working.view();

        // 出力バッファ作成（全ピクセル上書きするため初期化スキップ）
        ImageBuffer output(working.width(), working.height(), PixelFormatIDs::RGBA8_Straight,
                           InitPolicy::Uninitialized);
        ViewPort outputView = output.view();

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::Grayscale].recordAlloc(
            output.totalBytes(), output.width(), output.height());
#endif

        // フィルタ適用
        filters::grayscale(outputView, workingView);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::Grayscale];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(output), input.origin);
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_GRAYSCALE_NODE_H
