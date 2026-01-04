#include "filters.h"
#include "pixel_format_registry.h"
#include <algorithm>

namespace ImageTransform {

// ========================================================================
// フィルタ実装（ViewPortベース）
// ========================================================================

// 明るさ調整フィルタ（8bit Straight形式で処理）
ViewPort BrightnessFilter::apply(const ViewPort& input) const {
    // 入力が要求形式でない場合は変換
    ViewPort working;
    if (input.formatID != PixelFormatIDs::RGBA8_Straight) {
        working = ViewPort(input.width, input.height, PixelFormatIDs::RGBA8_Straight);
        PixelFormatRegistry& registry = PixelFormatRegistry::getInstance();
        // 行ごとに変換（ストライドの違いを吸収）
        for (int y = 0; y < input.height; y++) {
            const void* srcRow = input.getPixelPtr<uint8_t>(0, y);
            void* dstRow = working.getPixelPtr<uint8_t>(0, y);
            registry.convert(srcRow, input.formatID,
                           dstRow, PixelFormatIDs::RGBA8_Straight,
                           input.width);
        }
    } else {
        // 入力が既に8bit Straight形式の場合、そのまま参照
        working = ViewPort(input);
    }

    // 8bit Straight形式での処理
    ViewPort output(working.width, working.height, PixelFormatIDs::RGBA8_Straight);
    int adjustment = static_cast<int>(params_.brightness * 255.0f);

    for (int y = 0; y < working.height; y++) {
        const uint8_t* srcRow = working.getPixelPtr<uint8_t>(0, y);
        uint8_t* dstRow = output.getPixelPtr<uint8_t>(0, y);

        for (int x = 0; x < working.width; x++) {
            int pixelOffset = x * 4;
            // RGB各チャンネルに明るさ調整を適用（ストレート形式なので直接加算）
            for (int c = 0; c < 3; c++) {
                int value = static_cast<int>(srcRow[pixelOffset + c]) + adjustment;
                dstRow[pixelOffset + c] = static_cast<uint8_t>(std::max(0, std::min(255, value)));
            }
            // Alphaはそのままコピー
            dstRow[pixelOffset + 3] = srcRow[pixelOffset + 3];
        }
    }

    return output;
}

// グレースケールフィルタ（8bit Straight形式で処理）
ViewPort GrayscaleFilter::apply(const ViewPort& input) const {
    // 入力が要求形式でない場合は変換
    ViewPort working;
    if (input.formatID != PixelFormatIDs::RGBA8_Straight) {
        working = ViewPort(input.width, input.height, PixelFormatIDs::RGBA8_Straight);
        PixelFormatRegistry& registry = PixelFormatRegistry::getInstance();
        // 行ごとに変換（ストライドの違いを吸収）
        for (int y = 0; y < input.height; y++) {
            const void* srcRow = input.getPixelPtr<uint8_t>(0, y);
            void* dstRow = working.getPixelPtr<uint8_t>(0, y);
            registry.convert(srcRow, input.formatID,
                           dstRow, PixelFormatIDs::RGBA8_Straight,
                           input.width);
        }
    } else {
        // 入力が既に8bit Straight形式の場合、そのまま参照
        working = ViewPort(input);
    }

    // 8bit Straight形式での処理
    ViewPort output(working.width, working.height, PixelFormatIDs::RGBA8_Straight);

    for (int y = 0; y < working.height; y++) {
        const uint8_t* srcRow = working.getPixelPtr<uint8_t>(0, y);
        uint8_t* dstRow = output.getPixelPtr<uint8_t>(0, y);

        for (int x = 0; x < working.width; x++) {
            int pixelOffset = x * 4;
            // グレースケール変換（平均法、ストレート形式で正しく処理）
            uint8_t gray = static_cast<uint8_t>(
                (static_cast<uint16_t>(srcRow[pixelOffset]) +
                 static_cast<uint16_t>(srcRow[pixelOffset + 1]) +
                 static_cast<uint16_t>(srcRow[pixelOffset + 2])) / 3
            );
            dstRow[pixelOffset] = gray;       // R
            dstRow[pixelOffset + 1] = gray;   // G
            dstRow[pixelOffset + 2] = gray;   // B
            dstRow[pixelOffset + 3] = srcRow[pixelOffset + 3];  // Alphaはそのままコピー
        }
    }

    return output;
}

// ボックスブラーフィルタ（8bit Straight形式で処理）
ViewPort BoxBlurFilter::apply(const ViewPort& input) const {
    // 入力が要求形式でない場合は変換
    ViewPort working;
    if (input.formatID != PixelFormatIDs::RGBA8_Straight) {
        working = ViewPort(input.width, input.height, PixelFormatIDs::RGBA8_Straight);
        PixelFormatRegistry& registry = PixelFormatRegistry::getInstance();
        for (int y = 0; y < input.height; y++) {
            const void* srcRow = input.getPixelPtr<uint8_t>(0, y);
            void* dstRow = working.getPixelPtr<uint8_t>(0, y);
            registry.convert(srcRow, input.formatID,
                           dstRow, PixelFormatIDs::RGBA8_Straight,
                           input.width);
        }
    } else {
        working = ViewPort(input);
    }

    int width = working.width;
    int height = working.height;
    int radius = params_.radius;

    // 中間バッファ（水平ブラー結果）
    ViewPort temp(width, height, PixelFormatIDs::RGBA8_Straight);

    // パス1: 水平方向のブラー
    for (int y = 0; y < height; y++) {
        const uint8_t* srcRow = working.getPixelPtr<uint8_t>(0, y);
        uint8_t* dstRow = temp.getPixelPtr<uint8_t>(0, y);

        for (int x = 0; x < width; x++) {
            uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            int count = 0;

            int xStart = std::max(0, x - radius);
            int xEnd = std::min(width - 1, x + radius);

            for (int nx = xStart; nx <= xEnd; nx++) {
                int pixelOffset = nx * 4;
                sumR += srcRow[pixelOffset];
                sumG += srcRow[pixelOffset + 1];
                sumB += srcRow[pixelOffset + 2];
                sumA += srcRow[pixelOffset + 3];
                count++;
            }

            int outOffset = x * 4;
            dstRow[outOffset] = sumR / count;
            dstRow[outOffset + 1] = sumG / count;
            dstRow[outOffset + 2] = sumB / count;
            dstRow[outOffset + 3] = sumA / count;
        }
    }

    // パス2: 垂直方向のブラー
    ViewPort output(width, height, PixelFormatIDs::RGBA8_Straight);
    for (int y = 0; y < height; y++) {
        uint8_t* dstRow = output.getPixelPtr<uint8_t>(0, y);

        for (int x = 0; x < width; x++) {
            uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            int count = 0;

            int yStart = std::max(0, y - radius);
            int yEnd = std::min(height - 1, y + radius);

            for (int ny = yStart; ny <= yEnd; ny++) {
                const uint8_t* tmpRow = temp.getPixelPtr<uint8_t>(0, ny);
                int pixelOffset = x * 4;
                sumR += tmpRow[pixelOffset];
                sumG += tmpRow[pixelOffset + 1];
                sumB += tmpRow[pixelOffset + 2];
                sumA += tmpRow[pixelOffset + 3];
                count++;
            }

            int outOffset = x * 4;
            dstRow[outOffset] = sumR / count;
            dstRow[outOffset + 1] = sumG / count;
            dstRow[outOffset + 2] = sumB / count;
            dstRow[outOffset + 3] = sumA / count;
        }
    }

    return output;
}

// アルファ調整フィルタ（Premultiplied形式で処理）
ViewPort AlphaFilter::apply(const ViewPort& input) const {
    // 入力が要求形式でない場合は変換
    ViewPort working;
    if (input.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
        working = ViewPort(input.width, input.height, PixelFormatIDs::RGBA16_Premultiplied);
        PixelFormatRegistry& registry = PixelFormatRegistry::getInstance();
        // 行ごとに変換（ストライドの違いを吸収）
        for (int y = 0; y < input.height; y++) {
            const void* srcRow = input.getPixelPtr<uint8_t>(0, y);
            void* dstRow = working.getPixelPtr<uint8_t>(0, y);
            registry.convert(srcRow, input.formatID,
                           dstRow, PixelFormatIDs::RGBA16_Premultiplied,
                           input.width);
        }
    } else {
        working = ViewPort(input);
    }

    // Premultiplied形式での処理
    // RGB値もアルファに応じてスケールされるため、すべてのチャンネルに乗算を適用
    ViewPort output(working.width, working.height, PixelFormatIDs::RGBA16_Premultiplied);
    uint32_t alphaScale = static_cast<uint32_t>(params_.alpha * 65536.0f);  // 16.16固定小数点

    for (int y = 0; y < working.height; y++) {
        const uint16_t* srcRow = working.getPixelPtr<uint16_t>(0, y);
        uint16_t* dstRow = output.getPixelPtr<uint16_t>(0, y);

        for (int x = 0; x < working.width; x++) {
            int pixelOffset = x * 4;
            // RGBA全チャンネルにアルファ乗算を適用（Premultiplied形式）
            for (int c = 0; c < 4; c++) {
                uint32_t value = srcRow[pixelOffset + c];
                dstRow[pixelOffset + c] = static_cast<uint16_t>((value * alphaScale) >> 16);
            }
        }
    }

    return output;
}

} // namespace ImageTransform
