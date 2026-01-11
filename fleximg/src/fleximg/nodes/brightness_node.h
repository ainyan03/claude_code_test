#ifndef FLEXIMG_BRIGHTNESS_NODE_H
#define FLEXIMG_BRIGHTNESS_NODE_H

#include "filter_node_base.h"
#include "../operations/filters.h"
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// BrightnessNode - 明るさ調整フィルタノード
// ========================================================================
//
// 入力画像の明るさを調整します。
// - amount: 明るさ調整量（-1.0〜1.0、0.0で変化なし）
//
// 使用例:
//   BrightnessNode brightness;
//   brightness.setAmount(0.2f);  // 20%明るく
//   src >> brightness >> sink;
//

class BrightnessNode : public FilterNodeBase {
public:
    // ========================================
    // パラメータ設定
    // ========================================

    void setAmount(float amount) { amount_ = amount; }
    float amount() const { return amount_; }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "BrightnessNode"; }

protected:
    int nodeTypeForMetrics() const override { return NodeType::Brightness; }

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
        filters::brightness(workingView, workingView, amount_);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::Brightness];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(working), input.origin);
    }

private:
    float amount_ = 0.0f;
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_BRIGHTNESS_NODE_H
