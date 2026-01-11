#ifndef FLEXIMG_ALPHA_NODE_H
#define FLEXIMG_ALPHA_NODE_H

#include "filter_node_base.h"
#include "../operations/filters.h"
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// AlphaNode - アルファ調整フィルタノード
// ========================================================================
//
// 入力画像のアルファ値をスケールします。
// - scale: アルファスケール（0.0〜1.0、1.0で変化なし）
//
// 使用例:
//   AlphaNode alpha;
//   alpha.setScale(0.5f);  // 50%の不透明度
//   src >> alpha >> sink;
//

class AlphaNode : public FilterNodeBase {
public:
    // ========================================
    // パラメータ設定
    // ========================================

    void setScale(float scale) { scale_ = scale; }
    float scale() const { return scale_; }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "AlphaNode"; }

protected:
    int nodeTypeForMetrics() const override { return NodeType::Alpha; }

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
        filters::alpha(workingView, workingView, scale_);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::Alpha];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(working), input.origin);
    }

private:
    float scale_ = 1.0f;
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_ALPHA_NODE_H
