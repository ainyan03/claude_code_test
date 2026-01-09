#ifndef FLEXIMG_SOURCE_NODE_H
#define FLEXIMG_SOURCE_NODE_H

#include "../node.h"
#include "../viewport.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// SourceNode - 画像入力ノード（終端）
// ========================================================================
//
// パイプラインの入力端点となるノードです。
// - 入力ポート: 0
// - 出力ポート: 1
// - 外部のViewPortを参照
//

class SourceNode : public Node {
public:
    // コンストラクタ
    SourceNode() {
        initPorts(0, 1);  // 入力0、出力1
    }

    SourceNode(const ViewPort& vp, float originX = 0, float originY = 0)
        : source_(vp), originX_(originX), originY_(originY) {
        initPorts(0, 1);
    }

    // ソース設定
    void setSource(const ViewPort& vp) { source_ = vp; }
    void setOrigin(float x, float y) { originX_ = x; originY_ = y; }

    // アクセサ
    const ViewPort& source() const { return source_; }
    float originX() const { return originX_; }
    float originY() const { return originY_; }

    const char* name() const override { return "SourceNode"; }

private:
    ViewPort source_;
    float originX_ = 0;  // 画像内の基準点X（ピクセル座標）
    float originY_ = 0;  // 画像内の基準点Y（ピクセル座標）
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_SOURCE_NODE_H
