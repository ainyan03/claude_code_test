#include "filters.h"
#include "../image_buffer.h"
#include <algorithm>
#include <cstdint>

namespace FLEXIMG_NAMESPACE {
namespace filters {

// ========================================================================
// brightness - 明るさ調整
// ========================================================================

void brightness(ViewPort& dst, const ViewPort& src, float amount) {
    if (!dst.isValid() || !src.isValid()) return;
    if (dst.width != src.width || dst.height != src.height) return;

    int adjustment = static_cast<int>(amount * 255.0f);

    for (int y = 0; y < src.height; y++) {
        const uint8_t* srcRow = static_cast<const uint8_t*>(src.pixelAt(0, y));
        uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(0, y));

        for (int x = 0; x < src.width; x++) {
            int pixelOffset = x * 4;
            // RGB各チャンネルに明るさ調整を適用
            for (int c = 0; c < 3; c++) {
                int value = static_cast<int>(srcRow[pixelOffset + c]) + adjustment;
                dstRow[pixelOffset + c] = static_cast<uint8_t>(std::max(0, std::min(255, value)));
            }
            // Alphaはそのままコピー
            dstRow[pixelOffset + 3] = srcRow[pixelOffset + 3];
        }
    }
}

// ========================================================================
// grayscale - グレースケール変換
// ========================================================================

void grayscale(ViewPort& dst, const ViewPort& src) {
    if (!dst.isValid() || !src.isValid()) return;
    if (dst.width != src.width || dst.height != src.height) return;

    for (int y = 0; y < src.height; y++) {
        const uint8_t* srcRow = static_cast<const uint8_t*>(src.pixelAt(0, y));
        uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(0, y));

        for (int x = 0; x < src.width; x++) {
            int pixelOffset = x * 4;
            // グレースケール変換（平均法）
            uint8_t gray = static_cast<uint8_t>(
                (static_cast<uint16_t>(srcRow[pixelOffset]) +
                 static_cast<uint16_t>(srcRow[pixelOffset + 1]) +
                 static_cast<uint16_t>(srcRow[pixelOffset + 2])) / 3
            );
            dstRow[pixelOffset]     = gray;  // R
            dstRow[pixelOffset + 1] = gray;  // G
            dstRow[pixelOffset + 2] = gray;  // B
            dstRow[pixelOffset + 3] = srcRow[pixelOffset + 3];  // Alpha
        }
    }
}

// ========================================================================
// boxBlur - ボックスブラー（2パス）
// ========================================================================

void boxBlur(ViewPort& dst, const ViewPort& src, int radius) {
    if (!dst.isValid() || !src.isValid()) return;
    if (dst.width != src.width || dst.height != src.height) return;
    if (radius <= 0) {
        // 半径0の場合は単純コピー
        view_ops::copy(dst, 0, 0, src, 0, 0, src.width, src.height);
        return;
    }

    int width = src.width;
    int height = src.height;

    // 中間バッファ（水平ブラー結果）
    ImageBuffer temp(width, height, src.formatID);
    ViewPort tempView = temp.view();

    // パス1: 水平方向のブラー
    for (int y = 0; y < height; y++) {
        const uint8_t* srcRow = static_cast<const uint8_t*>(src.pixelAt(0, y));
        uint8_t* dstRow = static_cast<uint8_t*>(tempView.pixelAt(0, y));

        for (int x = 0; x < width; x++) {
            uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;

            int xStart = std::max(0, x - radius);
            int xEnd = std::min(width - 1, x + radius);
            int count = xEnd - xStart + 1;

            for (int nx = xStart; nx <= xEnd; nx++) {
                int pixelOffset = nx * 4;
                sumR += srcRow[pixelOffset];
                sumG += srcRow[pixelOffset + 1];
                sumB += srcRow[pixelOffset + 2];
                sumA += srcRow[pixelOffset + 3];
            }

            int outOffset = x * 4;
            dstRow[outOffset]     = sumR / count;
            dstRow[outOffset + 1] = sumG / count;
            dstRow[outOffset + 2] = sumB / count;
            dstRow[outOffset + 3] = sumA / count;
        }
    }

    // パス2: 垂直方向のブラー
    for (int y = 0; y < height; y++) {
        uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(0, y));

        int yStart = std::max(0, y - radius);
        int yEnd = std::min(height - 1, y + radius);
        int count = yEnd - yStart + 1;

        for (int x = 0; x < width; x++) {
            uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            int pixelOffset = x * 4;

            for (int ny = yStart; ny <= yEnd; ny++) {
                const uint8_t* tmpRow = static_cast<const uint8_t*>(tempView.pixelAt(0, ny));
                sumR += tmpRow[pixelOffset];
                sumG += tmpRow[pixelOffset + 1];
                sumB += tmpRow[pixelOffset + 2];
                sumA += tmpRow[pixelOffset + 3];
            }

            dstRow[pixelOffset]     = sumR / count;
            dstRow[pixelOffset + 1] = sumG / count;
            dstRow[pixelOffset + 2] = sumB / count;
            dstRow[pixelOffset + 3] = sumA / count;
        }
    }
}

// ========================================================================
// alpha - アルファ調整
// ========================================================================

void alpha(ViewPort& dst, const ViewPort& src, float scale) {
    if (!dst.isValid() || !src.isValid()) return;
    if (dst.width != src.width || dst.height != src.height) return;

    uint32_t alphaScale = static_cast<uint32_t>(scale * 256.0f);

    for (int y = 0; y < src.height; y++) {
        const uint8_t* srcRow = static_cast<const uint8_t*>(src.pixelAt(0, y));
        uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(0, y));

        for (int x = 0; x < src.width; x++) {
            int pixelOffset = x * 4;
            // RGBはそのまま、Alphaのみスケール
            dstRow[pixelOffset]     = srcRow[pixelOffset];      // R
            dstRow[pixelOffset + 1] = srcRow[pixelOffset + 1];  // G
            dstRow[pixelOffset + 2] = srcRow[pixelOffset + 2];  // B
            uint32_t a = srcRow[pixelOffset + 3];
            dstRow[pixelOffset + 3] = static_cast<uint8_t>((a * alphaScale) >> 8);  // A
        }
    }
}

} // namespace filters
} // namespace FLEXIMG_NAMESPACE
