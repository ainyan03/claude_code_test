#ifndef FLEXIMG_OPERATIONS_TRANSFORM_H
#define FLEXIMG_OPERATIONS_TRANSFORM_H

#include "../common.h"
#include "../types.h"
#include "../viewport.h"
#include <cmath>
#include <cstdint>
#include <utility>

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
// calcValidRange - DDA有効範囲計算
// ========================================================================
//
// DDAアルゴリズムで描画可能な dx の範囲を事前計算します。
//
// DDAループでの座標計算:
//   srcX_fixed = coeff * dx + base + (coeff >> 1)
//   srcIdx = srcX_fixed >> FIXED_POINT_BITS
//
// この関数は srcIdx が [0, srcSize) に入る dx の範囲を返します。
//
// パラメータ:
// - coeff: DDA係数（固定小数点 Q0.16）
// - base: 行ベース座標（固定小数点 Q0.16）
// - srcSize: ソース画像のサイズ
// - canvasSize: 出力キャンバスサイズ（dxの上限）
//
// 戻り値:
// - {dxStart, dxEnd}: 有効範囲（dxStart > dxEnd なら有効ピクセルなし）
//

inline std::pair<int, int> calcValidRange(
    int32_t coeff, int32_t base, int srcSize, int canvasSize
) {
    constexpr int BITS = FIXED_POINT_BITS;

    // DDAでは (coeff >> 1) のオフセットが加算される
    int32_t baseWithHalf = base + (coeff >> 1);

    if (coeff == 0) {
        // 係数ゼロ：全 dx で同じ srcIdx
        int srcIdx = baseWithHalf >> BITS;
        return (srcIdx >= 0 && srcIdx < srcSize)
            ? std::make_pair(0, canvasSize - 1)
            : std::make_pair(1, 0);
    }

    // srcIdx の有効範囲: [0, srcSize)
    // srcIdx = (coeff * dx + baseWithHalf) >> BITS
    //
    // 条件: 0 <= srcIdx < srcSize
    // → 0 <= (coeff * dx + baseWithHalf) >> BITS < srcSize
    //
    // 整数右シフトは切り捨て（負方向）なので:
    // → 0 <= coeff * dx + baseWithHalf < srcSize << BITS
    //
    // coeff > 0 の場合:
    //   dx >= -baseWithHalf / coeff → dx >= ceil(-baseWithHalf / coeff)
    //   dx < (srcSize << BITS) - baseWithHalf) / coeff
    //
    // coeff < 0 の場合: 不等式の向きが逆転

    int64_t minBound = -static_cast<int64_t>(baseWithHalf);
    int64_t maxBound = (static_cast<int64_t>(srcSize) << BITS) - baseWithHalf;

    int dxStart, dxEnd;
    if (coeff > 0) {
        // dx >= ceil(minBound / coeff) かつ dx < maxBound / coeff
        // → dx >= ceil(minBound / coeff) かつ dx <= floor((maxBound - 1) / coeff)
        if (minBound >= 0) {
            dxStart = static_cast<int>((minBound + coeff - 1) / coeff);
        } else {
            // 負の除算: ceil(a/b) = -(-a / b) for a < 0, b > 0
            dxStart = static_cast<int>(-(-minBound / coeff));
        }
        if (maxBound > 0) {
            dxEnd = static_cast<int>((maxBound - 1) / coeff);
        } else {
            // maxBound <= 0 → 有効範囲なし
            return {1, 0};
        }
    } else {
        // coeff < 0: 不等式の向きが逆転
        // dx <= floor(minBound / coeff) かつ dx > (maxBound - 1) / coeff
        int32_t negCoeff = -coeff;
        if (minBound <= 0) {
            dxEnd = static_cast<int>((-minBound) / negCoeff);
        } else {
            dxEnd = static_cast<int>(-(minBound + negCoeff - 1) / negCoeff);
        }
        if (maxBound <= 0) {
            dxStart = static_cast<int>((-maxBound + 1 + negCoeff - 1) / negCoeff);
        } else {
            // maxBound > 0 かつ coeff < 0 → 全dx有効の可能性
            dxStart = static_cast<int>(-((maxBound - 1) / negCoeff));
        }
    }

    return {dxStart, dxEnd};
}

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
// affine - アフィン変換【非推奨・削除予定】
// ------------------------------------------------------------------------
//
// !! この関数は AffineNode::applyAffine() に置き換えられました !!
// !! 次バージョンで削除予定です !!
//
// 制限事項（旧実装）:
// - tx/ty が整数精度のため、サブピクセル平行移動に非対応
//
// 代替: AffineNode (affine_node.h)
// - tx/ty を Q24.8 固定小数点で保持
// - サブピクセル精度の平行移動に対応
//
// 旧仕様（参考用に残存）:
// 入力画像に対してアフィン変換を適用し、出力バッファに書き込みます。
// 出力バッファは事前にゼロクリアされていることを前提とします。
//
// パラメータ:
// - dst: 出力バッファ（事前確保、ゼロクリア済み）
// - dstOriginX/Y: 出力バッファ内での基準点位置（固定小数点 Q24.8）
// - src: 入力バッファ
// - srcOriginX/Y: 入力バッファ内での基準点位置（固定小数点 Q24.8）
// - invMatrix: 事前計算された固定小数点逆行列
//
void affine(ViewPort& dst, int_fixed8 dstOriginX, int_fixed8 dstOriginY,
            const ViewPort& src, int_fixed8 srcOriginX, int_fixed8 srcOriginY,
            const FixedPointInverseMatrix& invMatrix);

} // namespace transform
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_OPERATIONS_TRANSFORM_H
