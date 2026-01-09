#ifndef FLEXIMG_TRANSFORM_NODE_H
#define FLEXIMG_TRANSFORM_NODE_H

#include "../node.h"
#include "../common.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// TransformNode - アフィン変換ノード
// ========================================================================
//
// 入力画像に対してアフィン変換（回転・拡縮・平行移動）を適用します。
// - 入力: 1ポート
// - 出力: 1ポート
//
// 使用例:
//   TransformNode transform;
//   transform.setMatrix(AffineMatrix::rotation(0.5f));
//   src >> transform >> sink;
//

class TransformNode : public Node {
public:
    TransformNode() {
        initPorts(1, 1);  // 入力1、出力1
    }

    // ========================================
    // 変換設定
    // ========================================

    void setMatrix(const AffineMatrix& m) { matrix_ = m; }
    const AffineMatrix& matrix() const { return matrix_; }

    // 便利なセッター
    void setRotation(float radians) {
        float c = std::cos(radians);
        float s = std::sin(radians);
        matrix_.a = c;  matrix_.b = -s;
        matrix_.c = s;  matrix_.d = c;
        matrix_.tx = 0; matrix_.ty = 0;
    }

    void setScale(float sx, float sy) {
        matrix_.a = sx; matrix_.b = 0;
        matrix_.c = 0;  matrix_.d = sy;
        matrix_.tx = 0; matrix_.ty = 0;
    }

    void setTranslation(float tx, float ty) {
        matrix_.a = 1;  matrix_.b = 0;
        matrix_.c = 0;  matrix_.d = 1;
        matrix_.tx = tx; matrix_.ty = ty;
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "TransformNode"; }

private:
    AffineMatrix matrix_;  // 恒等行列がデフォルト
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_TRANSFORM_NODE_H
