#ifndef FLEXIMG_OPERATIONS_TRANSFORM_H
#define FLEXIMG_OPERATIONS_TRANSFORM_H

#include "../common.h"
#include "../viewport.h"
#include <cmath>
#include <cstdint>

namespace FLEXIMG_NAMESPACE {
namespace transform {

// ========================================================================
// 固定小数点定数
// ========================================================================

constexpr int FIXED_POINT_BITS = 16;
constexpr int32_t FIXED_POINT_SCALE = 1 << FIXED_POINT_BITS;

// ========================================================================
// FixedPointInverseMatrix - 固定小数点逆行列
// ========================================================================
//
// アフィン変換の逆行列を固定小数点形式で保持します。
//
// 設計のポイント:
// - 回転/スケール成分(a,b,c,d)は固定小数点で保持
// - 平行移動成分(tx,ty)は整数で保持
// - DDA処理時に tx * fixedInvA のように整数演算で逆変換を計算
// - これにより、低精度(3bit等)でも回転中心が安定する
//
// 使用方法:
// 1. Rendererで fromMatrix() を呼び出して逆行列を作成
// 2. 範囲計算と transform::affine() の両方で同じ逆行列を使用
//

struct FixedPointInverseMatrix {
    int32_t a, b, c, d;   // 2x2逆行列（固定小数点、FIXED_POINT_BITS精度）
    int32_t tx, ty;       // 平行移動成分（整数）
    bool valid;           // 逆行列が存在するか

    FixedPointInverseMatrix() : a(0), b(0), c(0), d(0), tx(0), ty(0), valid(false) {}

    // AffineMatrixから固定小数点逆行列を作成
    static FixedPointInverseMatrix fromMatrix(const AffineMatrix& matrix) {
        FixedPointInverseMatrix result;

        float det = matrix.a * matrix.d - matrix.b * matrix.c;
        if (std::abs(det) < 1e-10f) {
            result.valid = false;
            return result;
        }

        float invDet = 1.0f / det;

        // 回転/スケール逆行列を固定小数点化
        result.a = std::lround(matrix.d * invDet * FIXED_POINT_SCALE);
        result.b = std::lround(-matrix.b * invDet * FIXED_POINT_SCALE);
        result.c = std::lround(-matrix.c * invDet * FIXED_POINT_SCALE);
        result.d = std::lround(matrix.a * invDet * FIXED_POINT_SCALE);

        // 平行移動は整数で保持（DDA時に係数と乗算して使用）
        result.tx = std::lround(matrix.tx);
        result.ty = std::lround(matrix.ty);
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
