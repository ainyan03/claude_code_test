#include "transform.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace FLEXIMG_NAMESPACE {
namespace transform {

// ========================================================================
// affine - アフィン変換
// ========================================================================

void affine(ViewPort& dst, float dstOriginX, float dstOriginY,
            const ViewPort& src, float srcOriginX, float srcOriginY,
            const AffineMatrix& matrix) {
    if (!dst.isValid() || !src.isValid()) return;

    int outW = dst.width;
    int outH = dst.height;

    // 逆行列を計算（出力→入力の座標変換）
    float det = matrix.a * matrix.d - matrix.b * matrix.c;
    if (std::abs(det) < 1e-10f) {
        // 特異行列の場合は何もしない
        return;
    }

    float invDet = 1.0f / det;
    float invA = matrix.d * invDet;
    float invB = -matrix.b * invDet;
    float invC = -matrix.c * invDet;
    float invD = matrix.a * invDet;
    float invTx = (-matrix.d * matrix.tx + matrix.b * matrix.ty) * invDet;
    float invTy = (matrix.c * matrix.tx - matrix.a * matrix.ty) * invDet;

    // 固定小数点の小数部ビット数
    constexpr int FIXED_POINT_BITS = 16;
    constexpr int32_t FIXED_POINT_SCALE = 1 << FIXED_POINT_BITS;

    // 固定小数点形式に変換
    int32_t fixedInvA  = std::lround(invA * FIXED_POINT_SCALE);
    int32_t fixedInvB  = std::lround(invB * FIXED_POINT_SCALE);
    int32_t fixedInvC  = std::lround(invC * FIXED_POINT_SCALE);
    int32_t fixedInvD  = std::lround(invD * FIXED_POINT_SCALE);
    int32_t fixedInvTx = std::lround(invTx * FIXED_POINT_SCALE);
    int32_t fixedInvTy = std::lround(invTy * FIXED_POINT_SCALE);

    // 座標変換の式:
    // dst座標(dx, dy) → 基準相対座標(dx - dstOriginX, dy - dstOriginY)
    // 逆変換 → 入力の基準相対座標(rx, ry)
    // → src座標(rx + srcOriginX, ry + srcOriginY)
    //
    // 整理:
    // sx = invA*(dx-dstOriginX) + invB*(dy-dstOriginY) + invTx + srcOriginX
    //    = invA*dx + invB*dy + (invTx - invA*dstOriginX - invB*dstOriginY + srcOriginX)

    int32_t dstOriginXInt = std::lround(dstOriginX);
    int32_t dstOriginYInt = std::lround(dstOriginY);
    int32_t srcOriginXInt = std::lround(srcOriginX);
    int32_t srcOriginYInt = std::lround(srcOriginY);

    fixedInvTx += -(dstOriginXInt * fixedInvA)
                - (dstOriginYInt * fixedInvB)
                + (srcOriginXInt << FIXED_POINT_BITS);
    fixedInvTy += -(dstOriginXInt * fixedInvC)
                - (dstOriginYInt * fixedInvD)
                + (srcOriginYInt << FIXED_POINT_BITS);

    // 有効描画範囲の事前計算
    auto calcValidRange = [](
        int32_t coeff, int32_t base, int minVal, int maxVal, int canvasSize
    ) -> std::pair<int, int> {
        constexpr int BITS = 16;
        constexpr int32_t SCALE = 1 << BITS;
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
    const int inputStride = src.stride / srcBpp;
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
                    const uint16_t* srcPixel = srcData + sy * inputStride + sx * 4;
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
