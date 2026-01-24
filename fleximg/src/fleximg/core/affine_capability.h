#ifndef FLEXIMG_AFFINE_CAPABILITY_H
#define FLEXIMG_AFFINE_CAPABILITY_H

#include "common.h"
#include "types.h"
#include <cmath>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// AffineCapability - アフィン変換機能を提供する Mixin クラス
// ========================================================================
//
// Node とは独立した Mixin として設計されており、多重継承で使用します。
// アフィン変換のローカル行列と、それを操作するセッターを提供します。
//
// 使用例:
//   class SourceNode : public Node, public AffineCapability { ... };
//   class CompositeNode : public Node, public AffineCapability { ... };
//
// セッターの動作:
// - setRotation(), setScale(), setRotationScale(): a,b,c,d のみ変更（tx,ty は維持）
// - setTranslation(): tx,ty のみ変更（a,b,c,d は維持）
// - setMatrix(): 全要素を設定
//

class AffineCapability {
public:
    AffineCapability() = default;
    virtual ~AffineCapability() = default;

    // ========================================
    // 行列アクセサ
    // ========================================

    void setMatrix(const AffineMatrix& m) { localMatrix_ = m; }
    const AffineMatrix& matrix() const { return localMatrix_; }

    // ========================================
    // 便利なセッター（AffineNode と同一API）
    // ========================================

    // 回転を設定（a,b,c,d のみ変更、tx,ty は維持）
    void setRotation(float radians) {
        float c = std::cos(radians);
        float s = std::sin(radians);
        localMatrix_.a = c;  localMatrix_.b = -s;
        localMatrix_.c = s;  localMatrix_.d = c;
    }

    // スケールを設定（a,b,c,d のみ変更、tx,ty は維持）
    void setScale(float sx, float sy) {
        localMatrix_.a = sx; localMatrix_.b = 0;
        localMatrix_.c = 0;  localMatrix_.d = sy;
    }

    // 平行移動を設定（tx,ty のみ変更、a,b,c,d は維持）
    void setTranslation(float tx, float ty) {
        localMatrix_.tx = tx;
        localMatrix_.ty = ty;
    }

    // 回転+スケールを設定（a,b,c,d のみ変更、tx,ty は維持）
    void setRotationScale(float radians, float sx, float sy) {
        float c = std::cos(radians);
        float s = std::sin(radians);
        localMatrix_.a = c * sx;  localMatrix_.b = -s * sy;
        localMatrix_.c = s * sx;  localMatrix_.d = c * sy;
    }

    // ========================================
    // ユーティリティ
    // ========================================

    // ローカル変換が設定されているか（単位行列でないか）
    bool hasLocalTransform() const {
        return localMatrix_.a != 1.0f || localMatrix_.b != 0.0f ||
               localMatrix_.c != 0.0f || localMatrix_.d != 1.0f ||
               localMatrix_.tx != 0.0f || localMatrix_.ty != 0.0f;
    }

protected:
    AffineMatrix localMatrix_;  // ローカル変換行列（デフォルトは単位行列）
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_AFFINE_CAPABILITY_H
