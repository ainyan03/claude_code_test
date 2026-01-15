#ifndef FLEXIMG_OPERATIONS_TRANSFORM_H
#define FLEXIMG_OPERATIONS_TRANSFORM_H

#include "../core/common.h"
#include "../core/types.h"
#include "../image/viewport.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
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

// ========================================================================
// copyRowDDA - DDA行転写テンプレート
// ========================================================================
//
// アフィン変換のDDA描画で、1行分のピクセルを転写します。
// BytesPerPixel に応じて最適化されたコピーを行います。
//

template<size_t BytesPerPixel>
inline void copyRowDDA(
    uint8_t* dstRow,
    const uint8_t* srcData,
    int32_t srcStride,
    int32_t srcX_fixed,
    int32_t srcY_fixed,
    int32_t fixedInvA,
    int32_t fixedInvC,
    int count
) {
    for (int i = 0; i < count; i++) {
        uint32_t sx = static_cast<uint32_t>(srcX_fixed) >> INT_FIXED16_SHIFT;
        uint32_t sy = static_cast<uint32_t>(srcY_fixed) >> INT_FIXED16_SHIFT;

        const uint8_t* srcPixel = srcData + sy * srcStride + sx * BytesPerPixel;

        if constexpr (BytesPerPixel == 8) {
            reinterpret_cast<uint32_t*>(dstRow)[0] =
                reinterpret_cast<const uint32_t*>(srcPixel)[0];
            reinterpret_cast<uint32_t*>(dstRow)[1] =
                reinterpret_cast<const uint32_t*>(srcPixel)[1];
        } else if constexpr (BytesPerPixel == 4) {
            *reinterpret_cast<uint32_t*>(dstRow) =
                *reinterpret_cast<const uint32_t*>(srcPixel);
        } else if constexpr (BytesPerPixel == 2) {
            *reinterpret_cast<uint16_t*>(dstRow) =
                *reinterpret_cast<const uint16_t*>(srcPixel);
        } else if constexpr (BytesPerPixel == 1) {
            *dstRow = *srcPixel;
        } else {
            std::memcpy(dstRow, srcPixel, BytesPerPixel);
        }

        dstRow += BytesPerPixel;
        srcX_fixed += fixedInvA;
        srcY_fixed += fixedInvC;
    }
}

// copyRowDDA の関数ポインタ型
using CopyRowDDAFunc = void(*)(
    uint8_t* dstRow,
    const uint8_t* srcData,
    int32_t srcStride,
    int32_t srcX_fixed,
    int32_t srcY_fixed,
    int32_t fixedInvA,
    int32_t fixedInvC,
    int count
);

// ========================================================================
// copyRowDDABilinear_RGBA8888 - バイリニア補間付きDDA行転写（RGBA8888専用）
// ========================================================================
//
// アフィン変換のDDA描画で、1行分のピクセルをバイリニア補間で転写します。
// RGBA8888フォーマット専用。
//
// 注意:
// - 半ピクセルオフセット（coeff >> 1）は不要。小数部を補間重みとして使用。
// - 有効範囲は srcSize - 1 で計算すること（sx+1, sy+1 が範囲内に収まるように）
//

inline void copyRowDDABilinear_RGBA8888(
    uint8_t* dstRow,
    const uint8_t* srcData,
    int32_t srcStride,
    int32_t srcX_fixed,
    int32_t srcY_fixed,
    int32_t fixedInvA,
    int32_t fixedInvC,
    int count
) {
    constexpr int BPP = 4;  // RGBA8888 = 4 bytes per pixel

    for (int i = 0; i < count; i++) {
        // 整数部（ピクセル座標）
        int32_t sx = srcX_fixed >> INT_FIXED16_SHIFT;
        int32_t sy = srcY_fixed >> INT_FIXED16_SHIFT;

        // 小数部を 0-255 に正規化（補間の重み）
        // Q16.16 の下位16ビットを8ビットに縮小
        uint32_t fx = (static_cast<uint32_t>(srcX_fixed) >> 8) & 0xFF;
        uint32_t fy = (static_cast<uint32_t>(srcY_fixed) >> 8) & 0xFF;

        // 4点のポインタを取得
        // 有効範囲計算で sx+1, sy+1 が範囲内であることが保証されている前提
        const uint8_t* p00 = srcData + sy * srcStride + sx * BPP;
        const uint8_t* p10 = p00 + BPP;           // (sx+1, sy)
        const uint8_t* p01 = p00 + srcStride;    // (sx, sy+1)
        const uint8_t* p11 = p01 + BPP;           // (sx+1, sy+1)

        // バイリニア補間（各チャンネル）
        // top    = p00 * (256 - fx) + p10 * fx
        // bottom = p01 * (256 - fx) + p11 * fx
        // result = top * (256 - fy) + bottom * fy
        uint32_t ifx = 256 - fx;
        uint32_t ify = 256 - fy;

        for (int c = 0; c < 4; c++) {
            uint32_t top    = p00[c] * ifx + p10[c] * fx;
            uint32_t bottom = p01[c] * ifx + p11[c] * fx;
            dstRow[c] = static_cast<uint8_t>((top * ify + bottom * fy) >> 16);
        }

        dstRow += BPP;
        srcX_fixed += fixedInvA;
        srcY_fixed += fixedInvC;
    }
}

// ========================================================================
// applyAffineDDA - DDAによるアフィン変換転写
// ========================================================================
//
// 事前計算済みのパラメータを使用して、DDAによるアフィン変換転写を行います。
//
// パラメータ:
// - dst: 出力ViewPort
// - src: 入力ViewPort
// - fixedInvTx/Ty: 逆変換オフセット（process時に計算された最終値）
// - invMatrix: 逆行列（2x2部分）
// - rowOffsetX/Y: 行オフセット（invB/D >> 1）
// - dxOffsetX/Y: dx オフセット（invA/C >> 1）
//

inline void applyAffineDDA(
    ViewPort& dst,
    const ViewPort& src,
    int32_t fixedInvTx,
    int32_t fixedInvTy,
    const Matrix2x2_fixed16& invMatrix,
    int32_t rowOffsetX,
    int32_t rowOffsetY,
    int32_t dxOffsetX,
    int32_t dxOffsetY
) {
    if (!dst.isValid() || !src.isValid()) return;
    if (!invMatrix.valid) return;

    const int outW = dst.width;
    const int outH = dst.height;

    // BytesPerPixel に応じて関数ポインタを選択
    CopyRowDDAFunc copyRow = nullptr;
    switch (getBytesPerPixel(src.formatID)) {
        case 8: copyRow = &copyRowDDA<8>; break;
        case 4: copyRow = &copyRowDDA<4>; break;
        case 3: copyRow = &copyRowDDA<3>; break;
        case 2: copyRow = &copyRowDDA<2>; break;
        case 1: copyRow = &copyRowDDA<1>; break;
        default: return;
    }

    const int32_t fixedInvA = invMatrix.a;
    const int32_t fixedInvB = invMatrix.b;
    const int32_t fixedInvC = invMatrix.c;
    const int32_t fixedInvD = invMatrix.d;

    const int32_t srcStride = src.stride;
    const uint8_t* srcData = static_cast<const uint8_t*>(src.data);

    for (int dy = 0; dy < outH; dy++) {
        int32_t rowBaseX = fixedInvB * dy + fixedInvTx + rowOffsetX;
        int32_t rowBaseY = fixedInvD * dy + fixedInvTy + rowOffsetY;

        auto [xStart, xEnd] = calcValidRange(fixedInvA, rowBaseX, src.width, outW);
        auto [yStart, yEnd] = calcValidRange(fixedInvC, rowBaseY, src.height, outW);
        int dxStart = std::max({0, xStart, yStart});
        int dxEnd = std::min({outW - 1, xEnd, yEnd});

        if (dxStart > dxEnd) continue;

        int32_t srcX_fixed = fixedInvA * dxStart + rowBaseX + dxOffsetX;
        int32_t srcY_fixed = fixedInvC * dxStart + rowBaseY + dxOffsetY;
        int count = dxEnd - dxStart + 1;

        uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(dxStart, dy));

        copyRow(dstRow, srcData, srcStride,
                srcX_fixed, srcY_fixed, fixedInvA, fixedInvC, count);
    }
}

} // namespace transform
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_OPERATIONS_TRANSFORM_H
