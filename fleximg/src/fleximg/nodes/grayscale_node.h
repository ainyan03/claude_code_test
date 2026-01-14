#ifndef FLEXIMG_GRAYSCALE_NODE_H
#define FLEXIMG_GRAYSCALE_NODE_H

#include "filter_node_base.h"

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
    filters::LineFilterFunc getFilterFunc() const override {
        return &filters::grayscale_line;
    }
    int nodeTypeForMetrics() const override { return NodeType::Grayscale; }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_GRAYSCALE_NODE_H
