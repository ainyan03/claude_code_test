#include "blend.h"
#include "../core/types.h"
#include "../image/pixel_format.h"
#include <cstring>
#include <algorithm>

namespace FLEXIMG_NAMESPACE {
namespace blend {

// ========================================================================
// first - 透明キャンバスへの最初の描画
// ========================================================================

void first(ViewPort& dst, int_fixed dstOriginX, int_fixed dstOriginY,
           const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY) {
    if (!dst.isValid() || !src.isValid()) return;

    // 基準点を一致させるためのオフセット計算（固定小数点演算）
    int offsetX = from_fixed(dstOriginX - srcOriginX);
    int offsetY = from_fixed(dstOriginY - srcOriginY);

    // クリッピング範囲を計算
    int srcStartX = std::max(0, -offsetX);
    int srcStartY = std::max(0, -offsetY);
    int dstStartX = std::max(0, offsetX);
    int dstStartY = std::max(0, offsetY);
    int copyWidth = std::min(src.width - srcStartX, dst.width - dstStartX);
    int copyHeight = std::min(src.height - srcStartY, dst.height - dstStartY);

    if (copyWidth <= 0 || copyHeight <= 0) return;

    // フォーマットが同じならmemcpy
    if (src.formatID == dst.formatID) {
        auto bpp = getBytesPerPixel(src.formatID);
        for (int y = 0; y < copyHeight; y++) {
            const void* srcRow = src.pixelAt(srcStartX, srcStartY + y);
            void* dstRow = dst.pixelAt(dstStartX, dstStartY + y);
            std::memcpy(dstRow, srcRow, copyWidth * bpp);
        }
        return;
    }

    // RGBA8_Straight → RGBA16_Premultiplied 変換
    if (src.formatID == PixelFormatIDs::RGBA8_Straight &&
        dst.formatID == PixelFormatIDs::RGBA16_Premultiplied) {
        for (int y = 0; y < copyHeight; y++) {
            const uint8_t* srcRow = static_cast<const uint8_t*>(
                src.pixelAt(srcStartX, srcStartY + y));
            uint16_t* dstRow = static_cast<uint16_t*>(
                dst.pixelAt(dstStartX, dstStartY + y));

            for (int x = 0; x < copyWidth; x++) {
                uint16_t r = srcRow[x * 4 + 0];
                uint16_t g = srcRow[x * 4 + 1];
                uint16_t b = srcRow[x * 4 + 2];
                uint16_t a8 = srcRow[x * 4 + 3];

                // DESIGN_PIXEL_FORMAT.md に従った変換
                // A_tmp = A8 + 1 (範囲: 1-256)
                // A16 = 255 * A_tmp (範囲: 255-65280)
                // R16 = R8 * A_tmp (範囲: 0-65280)
                uint16_t a_tmp = a8 + 1;
                uint16_t a16 = 255 * a_tmp;
                uint16_t r16 = r * a_tmp;
                uint16_t g16 = g * a_tmp;
                uint16_t b16 = b * a_tmp;

                dstRow[x * 4 + 0] = r16;
                dstRow[x * 4 + 1] = g16;
                dstRow[x * 4 + 2] = b16;
                dstRow[x * 4 + 3] = a16;
            }
        }
        return;
    }

    // その他のフォーマット組み合わせ（未対応）
}

// ========================================================================
// onto - 既存画像への合成
// ========================================================================

void onto(ViewPort& dst, int_fixed dstOriginX, int_fixed dstOriginY,
          const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY) {
    if (!dst.isValid() || !src.isValid()) return;

    // 基準点を一致させるためのオフセット計算（固定小数点演算）
    int offsetX = from_fixed(dstOriginX - srcOriginX);
    int offsetY = from_fixed(dstOriginY - srcOriginY);

    // ループ範囲を事前計算
    int_fast32_t yStart = std::max(0, -offsetY);
    int_fast32_t yEnd = std::min<int_fast32_t>(src.height, dst.height - offsetY);
    int_fast32_t xStart = std::max(0, -offsetX);
    int_fast32_t xEnd = std::min<int_fast32_t>(src.width, dst.width - offsetX);

    if (yStart >= yEnd || xStart >= xEnd) return;

    // 閾値定数
    constexpr uint16_t ALPHA_TRANS_MAX = RGBA16Premul::ALPHA_TRANSPARENT_MAX;
    constexpr uint16_t ALPHA_OPAQUE_MIN = RGBA16Premul::ALPHA_OPAQUE_MIN;

    // RGBA8_Straight → RGBA16_Premultiplied への変換ブレンド
    if (src.formatID == PixelFormatIDs::RGBA8_Straight &&
        dst.formatID == PixelFormatIDs::RGBA16_Premultiplied) {
        for (int y = yStart; y < yEnd; y++) {
            const uint8_t* srcRow = static_cast<const uint8_t*>(src.pixelAt(0, y));
            uint16_t* dstRow = static_cast<uint16_t*>(dst.pixelAt(0, y + offsetY));

            for (int x = xStart; x < xEnd; x++) {
                const uint8_t* srcPixel8 = srcRow + x * 4;
                uint16_t* dstPixel = dstRow + (x + offsetX) * 4;

                uint16_t a8 = srcPixel8[3];
                uint16_t dstA = dstPixel[3];

                // dst に描画があり src が透明 → スキップ
                if (dstA > ALPHA_TRANS_MAX && a8 == 0) continue;

                // DESIGN_PIXEL_FORMAT.md に従った変換
                // A_tmp = A8 + 1 (範囲: 1-256)
                // A16 = 255 * A_tmp (範囲: 255-65280)
                // R16 = R8 * A_tmp (範囲: 0-65280)
                uint16_t a_tmp = a8 + 1;
                uint16_t srcA = 255 * a_tmp;
                uint16_t srcR = srcPixel8[0] * a_tmp;
                uint16_t srcG = srcPixel8[1] * a_tmp;
                uint16_t srcB = srcPixel8[2] * a_tmp;

                // 半透明ブレンド（dst に描画があり src が半透明の場合）
                if (dstA > ALPHA_TRANS_MAX && srcA < ALPHA_OPAQUE_MIN) {
                    uint16_t invSrcA = ALPHA_OPAQUE_MIN - srcA;
                    srcR += (dstPixel[0] * invSrcA) >> 16;
                    srcG += (dstPixel[1] * invSrcA) >> 16;
                    srcB += (dstPixel[2] * invSrcA) >> 16;
                    srcA += (dstA * invSrcA) >> 16;
                }

                dstPixel[0] = srcR;
                dstPixel[1] = srcG;
                dstPixel[2] = srcB;
                dstPixel[3] = srcA;
            }
        }
        return;
    }

    // 16bit Premultiplied 同士のブレンド
    for (int y = yStart; y < yEnd; y++) {
        const uint16_t* srcRow = static_cast<const uint16_t*>(src.pixelAt(0, y));
        uint16_t* dstRow = static_cast<uint16_t*>(dst.pixelAt(0, y + offsetY));

        for (int x = xStart; x < xEnd; x++) {
            const uint16_t* srcPixel = srcRow + x * 4;
            uint16_t* dstPixel = dstRow + (x + offsetX) * 4;

            uint16_t srcA = srcPixel[3];
            uint16_t dstA = dstPixel[3];

            // dst に描画があり src が透明 → スキップ
            if (dstA > ALPHA_TRANS_MAX && srcA <= ALPHA_TRANS_MAX) continue;

            uint16_t srcR = srcPixel[0];
            uint16_t srcG = srcPixel[1];
            uint16_t srcB = srcPixel[2];

            // 半透明ブレンド（dst に描画があり src が半透明の場合）
            if (dstA > ALPHA_TRANS_MAX && srcA < ALPHA_OPAQUE_MIN) {
                uint16_t invSrcA = ALPHA_OPAQUE_MIN - srcA;
                srcR += (dstPixel[0] * invSrcA) >> 16;
                srcG += (dstPixel[1] * invSrcA) >> 16;
                srcB += (dstPixel[2] * invSrcA) >> 16;
                srcA += (dstA * invSrcA) >> 16;
            }

            dstPixel[0] = srcR;
            dstPixel[1] = srcG;
            dstPixel[2] = srcB;
            dstPixel[3] = srcA;
        }
    }
}

} // namespace blend
} // namespace FLEXIMG_NAMESPACE
