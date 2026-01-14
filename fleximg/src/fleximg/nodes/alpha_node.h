#ifndef FLEXIMG_ALPHA_NODE_H
#define FLEXIMG_ALPHA_NODE_H

#include "filter_node_base.h"

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
    AlphaNode() { params_.value1 = 1.0f; }  // デフォルト: 変化なし

    // ========================================
    // パラメータ設定
    // ========================================

    void setScale(float scale) { params_.value1 = scale; }
    float scale() const { return params_.value1; }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "AlphaNode"; }

protected:
    filters::LineFilterFunc getFilterFunc() const override {
        return &filters::alpha_line;
    }
    int nodeTypeForMetrics() const override { return NodeType::Alpha; }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_ALPHA_NODE_H
