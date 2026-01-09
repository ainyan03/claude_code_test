#ifndef FLEXIMG_OPERATIONS_TRANSFORM_H
#define FLEXIMG_OPERATIONS_TRANSFORM_H

#include "../common.h"
#include "../viewport.h"
#include <cmath>
#include <cstdint>

namespace FLEXIMG_NAMESPACE {
namespace transform {

// ========================================================================
// 固定小数点逆行列
// ========================================================================
//
// アフィン変換の逆行列を固定小数点形式で保持します。
// renderer側で事前計算し、範囲計算とDDA処理の両方で使用することで
// 一貫性を保証します。
//

constexpr int FIXED_POINT_BITS = 16;
constexpr int32_t FIXED_POINT_SCALE = 1 << FIXED_POINT_BITS;

struct FixedPointInverseMatrix {
    int32_t a, b, c, d;   // 2x2回転・スケール部分の逆行列
    int32_t tx, ty;       // 平行移動部分の逆変換
    bool valid;           // 逆行列が存在するか（非特異行列か）

    FixedPointInverseMatrix() : a(0), b(0), c(0), d(0), tx(0), ty(0), valid(false) {}

    // AffineMatrixから固定小数点逆行列を計算
    static FixedPointInverseMatrix fromMatrix(const AffineMatrix& matrix) {
        FixedPointInverseMatrix result;

        float det = matrix.a * matrix.d - matrix.b * matrix.c;
        if (std::abs(det) < 1e-10f) {
            // 特異行列
            result.valid = false;
            return result;
        }

        float invDet = 1.0f / det;
        float invA = matrix.d * invDet;
        float invB = -matrix.b * invDet;
        float invC = -matrix.c * invDet;
        float invD = matrix.a * invDet;
        float invTx = (-matrix.d * matrix.tx + matrix.b * matrix.ty) * invDet;
        float invTy = (matrix.c * matrix.tx - matrix.a * matrix.ty) * invDet;

        result.a  = std::lround(invA * FIXED_POINT_SCALE);
        result.b  = std::lround(invB * FIXED_POINT_SCALE);
        result.c  = std::lround(invC * FIXED_POINT_SCALE);
        result.d  = std::lround(invD * FIXED_POINT_SCALE);
        result.tx = std::lround(invTx * FIXED_POINT_SCALE);
        result.ty = std::lround(invTy * FIXED_POINT_SCALE);
        result.valid = true;

        return result;
    }
};

// ========================================================================
// アフィン変換（純関数）
// ========================================================================
//
// 基準点座標系でのアフィン変換を行います。
//
// 座標系の考え方:
// - 各バッファは「基準点からの相対座標」で管理される
// - dstOrigin: dstバッファ内での基準点位置
// - srcOrigin: srcバッファ内での基準点位置
// - matrix: 基準点を中心とした変換行列
//
// 変換の流れ:
// 1. dstの各ピクセル座標を基準点相対座標に変換
// 2. 逆行列で入力の基準点相対座標を計算
// 3. srcバッファ座標に変換してサンプリング
//

// ------------------------------------------------------------------------
// affine - アフィン変換
// ------------------------------------------------------------------------
//
// 入力画像に対してアフィン変換を適用し、出力バッファに書き込みます。
// 出力バッファは事前にゼロクリアされていることを前提とします。
//
// パラメータ:
// - dst: 出力バッファ（事前確保、ゼロクリア済み）
// - dstOriginX/Y: 出力バッファ内での基準点位置
// - src: 入力バッファ
// - srcOriginX/Y: 入力バッファ内での基準点位置
// - invMatrix: 事前計算された固定小数点逆行列
//
void affine(ViewPort& dst, float dstOriginX, float dstOriginY,
            const ViewPort& src, float srcOriginX, float srcOriginY,
            const FixedPointInverseMatrix& invMatrix);

} // namespace transform
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_OPERATIONS_TRANSFORM_H
