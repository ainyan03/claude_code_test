#ifndef FLEXIMG_BRIGHTNESS_NODE_H
#define FLEXIMG_BRIGHTNESS_NODE_H

#include "filter_node_base.h"

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

    void setAmount(float amount) { params_.value1 = amount; }
    float amount() const { return params_.value1; }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "BrightnessNode"; }

protected:
    filters::LineFilterFunc getFilterFunc() const override {
        return &filters::brightness_line;
    }
    int nodeTypeForMetrics() const override { return NodeType::Brightness; }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_BRIGHTNESS_NODE_H
