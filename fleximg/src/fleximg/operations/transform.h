#ifndef FLEXIMG_OPERATIONS_TRANSFORM_H
#define FLEXIMG_OPERATIONS_TRANSFORM_H

#include "../core/common.h"
#include "../core/types.h"
#include "../image/viewport.h"
#include <cstdint>
#include <utility>

namespace FLEXIMG_NAMESPACE {
namespace transform {

// ========================================================================
// calcValidRange - DDA有効範囲計算
// ========================================================================
//
// アフィン変換のDDA描画で、ソース画像の有効範囲に入る出力ピクセル(dx)の
// 開始・終了位置を事前計算します。
//
// 【背景】
// DDAループでは各出力ピクセル dx に対して、ソース座標を計算します:
//   srcX_fixed = coeff * dx + base + (coeff >> 1)
//   srcIdx = srcX_fixed >> 16
//
// (coeff >> 1) は中心サンプリング補正です。dx=0 のとき、ソースの
// 先頭ではなく 0.5 ピクセル目（中心）をサンプリングするためです。
//
// 【この関数の役割】
// srcIdx が [0, srcSize) に収まる dx の範囲を返します。
// 範囲外のピクセルはスキップでき、不要な境界チェックを削減できます。
//
// パラメータ:
// - coeff: DDA係数（固定小数点 Q16.16）
// - base: 行ベース座標（固定小数点 Q16.16）
// - srcSize: ソース画像のサイズ
// - canvasSize: 出力キャンバスサイズ（dxの上限）
//
// 戻り値:
// - {dxStart, dxEnd}: 有効範囲（dxStart > dxEnd なら有効ピクセルなし）
//

inline std::pair<int, int> calcValidRange(
    int32_t coeff, int32_t base, int srcSize, int canvasSize
) {
    constexpr int BITS = INT_FIXED16_SHIFT;

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

} // namespace transform
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_OPERATIONS_TRANSFORM_H
