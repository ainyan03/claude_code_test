#include "transform.h"
#include "../types.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace FLEXIMG_NAMESPACE {
namespace transform {

// ========================================================================
// affine - アフィン変換【非推奨・削除予定】
// ========================================================================
// 代替: AffineNode::applyAffine() (affine_node.h)
// 制限: tx/ty が整数精度のため、サブピクセル平行移動に非対応

void affine(ViewPort& dst, int_fixed8 dstOriginX, int_fixed8 dstOriginY,
            const ViewPort& src, int_fixed8 srcOriginX, int_fixed8 srcOriginY,
            const FixedPointInverseMatrix& invMatrix) {
    if (!dst.isValid() || !src.isValid()) return;
    if (!invMatrix.valid) return;

    int outW = dst.width;
    int outH = dst.height;

    // 固定小数点逆行列の回転/スケール成分
    int32_t fixedInvA = invMatrix.a;
    int32_t fixedInvB = invMatrix.b;
    int32_t fixedInvC = invMatrix.c;
    int32_t fixedInvD = invMatrix.d;

    // 原点座標を整数化（固定小数点から変換）
    int32_t dstOriginXInt = from_fixed8(dstOriginX);
    int32_t dstOriginYInt = from_fixed8(dstOriginY);
    int32_t srcOriginXInt = from_fixed8(srcOriginX);
    int32_t srcOriginYInt = from_fixed8(srcOriginY);

    // ========================================================================
    // 逆変換オフセットの計算
    // ========================================================================
    //
    // 逆変換の数式: srcPos = R^(-1) * dstPos + invT
    //   invT = -R^(-1) * T = -(invA*tx + invB*ty, invC*tx + invD*ty)
    //
    // 重要: tx/tyは整数として保持し、固定小数点係数と乗算する。
    // これにより、回転係数の量子化精度に関係なく、平行移動が正確に計算される。
    // （低精度でも回転中心が安定する理由）
    //
    // 整数キャンセル方式:
    // - dstOrigin位置での計算誤差を最小化するため、dstOriginを基準に展開
    // - srcOriginを加算してソース座標系に変換

    // 平行移動の逆変換: -(tx * invA + ty * invB)
    int32_t invTxInt = -(invMatrix.tx * fixedInvA + invMatrix.ty * fixedInvB);
    int32_t invTyInt = -(invMatrix.tx * fixedInvC + invMatrix.ty * fixedInvD);

    // DDA用オフセット: 逆変換 + 整数キャンセル + srcOrigin
    int32_t fixedInvTx = invTxInt
                        - (dstOriginXInt * fixedInvA)
                        - (dstOriginYInt * fixedInvB)
                        + (srcOriginXInt << FIXED_POINT_BITS);
    int32_t fixedInvTy = invTyInt
                        - (dstOriginXInt * fixedInvC)
                        - (dstOriginYInt * fixedInvD)
                        + (srcOriginYInt << FIXED_POINT_BITS);

    // 有効描画範囲の事前計算
    auto calcValidRange = [](
        int32_t coeff, int32_t base, int minVal, int maxVal, int canvasSize
    ) -> std::pair<int, int> {
        constexpr int BITS = FIXED_POINT_BITS;
        constexpr int32_t SCALE = FIXED_POINT_SCALE;
        int32_t coeffHalf = coeff >> 1;

        if (coeff == 0) {
            int val = base >> BITS;
            if (base < 0 && (base & (SCALE - 1)) != 0) val--;
            return (val >= minVal && val <= maxVal)
                ? std::make_pair(0, canvasSize - 1)
                : std::make_pair(1, 0);
        }

        float baseWithHalf = static_cast<float>(base + coeffHalf);
        float minThreshold = static_cast<float>(minVal) * SCALE;
        float maxThreshold = static_cast<float>(maxVal + 1) * SCALE;
        float dxForMin = (minThreshold - baseWithHalf) / coeff;
        float dxForMax = (maxThreshold - baseWithHalf) / coeff;

        int dxStart, dxEnd;
        if (coeff > 0) {
            dxStart = static_cast<int>(std::ceil(dxForMin));
            dxEnd = static_cast<int>(std::ceil(dxForMax)) - 1;
        } else {
            dxStart = static_cast<int>(std::ceil(dxForMax));
            dxEnd = static_cast<int>(std::ceil(dxForMin)) - 1;
        }
        return {dxStart, dxEnd};
    };

    // ピクセルスキャン（DDAアルゴリズム）
    size_t srcBpp = getBytesPerPixel(src.formatID);
    // stride を uint16_t 単位で計算（16bit版用）
    const int inputStride16 = src.stride / sizeof(uint16_t);
    const int32_t rowOffsetX = fixedInvB >> 1;
    const int32_t rowOffsetY = fixedInvD >> 1;
    const int32_t dxOffsetX = fixedInvA >> 1;
    const int32_t dxOffsetY = fixedInvC >> 1;

    // 16bit RGBA用
    if (srcBpp == 8) {
        for (int dy = 0; dy < outH; dy++) {
            int32_t rowBaseX = fixedInvB * dy + fixedInvTx + rowOffsetX;
            int32_t rowBaseY = fixedInvD * dy + fixedInvTy + rowOffsetY;

            auto [xStart, xEnd] = calcValidRange(fixedInvA, rowBaseX, 0, src.width - 1, outW);
            auto [yStart, yEnd] = calcValidRange(fixedInvC, rowBaseY, 0, src.height - 1, outW);
            int dxStart = std::max({0, xStart, yStart});
            int dxEnd = std::min({outW - 1, xEnd, yEnd});

            if (dxStart > dxEnd) continue;

            int32_t srcX_fixed = fixedInvA * dxStart + rowBaseX + dxOffsetX;
            int32_t srcY_fixed = fixedInvC * dxStart + rowBaseY + dxOffsetY;

            uint16_t* dstRow = static_cast<uint16_t*>(dst.pixelAt(dxStart, dy));
            const uint16_t* srcData = static_cast<const uint16_t*>(src.data);

            for (int dx = dxStart; dx <= dxEnd; dx++) {
                uint32_t sx = static_cast<uint32_t>(srcX_fixed) >> FIXED_POINT_BITS;
                uint32_t sy = static_cast<uint32_t>(srcY_fixed) >> FIXED_POINT_BITS;

                if (sx < static_cast<uint32_t>(src.width) && sy < static_cast<uint32_t>(src.height)) {
                    const uint16_t* srcPixel = srcData + sy * inputStride16 + sx * 4;
                    dstRow[0] = srcPixel[0];
                    dstRow[1] = srcPixel[1];
                    dstRow[2] = srcPixel[2];
                    dstRow[3] = srcPixel[3];
                }

                dstRow += 4;
                srcX_fixed += fixedInvA;
                srcY_fixed += fixedInvC;
            }
        }
    }
    // 8bit RGBA用
    else if (srcBpp == 4) {
        for (int dy = 0; dy < outH; dy++) {
            int32_t rowBaseX = fixedInvB * dy + fixedInvTx + rowOffsetX;
            int32_t rowBaseY = fixedInvD * dy + fixedInvTy + rowOffsetY;

            auto [xStart, xEnd] = calcValidRange(fixedInvA, rowBaseX, 0, src.width - 1, outW);
            auto [yStart, yEnd] = calcValidRange(fixedInvC, rowBaseY, 0, src.height - 1, outW);
            int dxStart = std::max({0, xStart, yStart});
            int dxEnd = std::min({outW - 1, xEnd, yEnd});

            if (dxStart > dxEnd) continue;

            int32_t srcX_fixed = fixedInvA * dxStart + rowBaseX + dxOffsetX;
            int32_t srcY_fixed = fixedInvC * dxStart + rowBaseY + dxOffsetY;

            uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(dxStart, dy));
            const uint8_t* srcData = static_cast<const uint8_t*>(src.data);
            const int stride8 = src.stride;

            for (int dx = dxStart; dx <= dxEnd; dx++) {
                uint32_t sx = static_cast<uint32_t>(srcX_fixed) >> FIXED_POINT_BITS;
                uint32_t sy = static_cast<uint32_t>(srcY_fixed) >> FIXED_POINT_BITS;

                if (sx < static_cast<uint32_t>(src.width) && sy < static_cast<uint32_t>(src.height)) {
                    const uint8_t* srcPixel = srcData + sy * stride8 + sx * 4;
                    dstRow[0] = srcPixel[0];
                    dstRow[1] = srcPixel[1];
                    dstRow[2] = srcPixel[2];
                    dstRow[3] = srcPixel[3];
                }

                dstRow += 4;
                srcX_fixed += fixedInvA;
                srcY_fixed += fixedInvC;
            }
        }
    }
}

} // namespace transform
} // namespace FLEXIMG_NAMESPACE
