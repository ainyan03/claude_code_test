#include "filters.h"
#include <algorithm>

namespace ImageTransform {

// ========================================================================
// フィルタ実装
// ========================================================================

// 明るさ調整フィルタ
Image16 BrightnessFilter16::apply(const Image16& input) const {
    Image16 output(input.width, input.height);
    int adjustment = static_cast<int>(params_.brightness * 65535.0f);

    for (size_t i = 0; i < input.data.size(); i += 4) {
        // RGB各チャンネルに明るさ調整を適用（premultiplied alphaなので、RGBのみ）
        for (int c = 0; c < 3; c++) {
            int value = static_cast<int>(input.data[i + c]) + adjustment;
            output.data[i + c] = static_cast<uint16_t>(std::max(0, std::min(65535, value)));
        }
        // Alphaはそのままコピー
        output.data[i + 3] = input.data[i + 3];
    }

    return output;
}

// グレースケールフィルタ
Image16 GrayscaleFilter16::apply(const Image16& input) const {
    Image16 output(input.width, input.height);

    for (size_t i = 0; i < input.data.size(); i += 4) {
        // グレースケール変換（平均法、premultiplied alphaでも同様に適用）
        uint16_t gray = static_cast<uint16_t>(
            (static_cast<uint32_t>(input.data[i]) +
             static_cast<uint32_t>(input.data[i + 1]) +
             static_cast<uint32_t>(input.data[i + 2])) / 3
        );
        output.data[i] = gray;       // R
        output.data[i + 1] = gray;   // G
        output.data[i + 2] = gray;   // B
        output.data[i + 3] = input.data[i + 3];  // Alphaはそのままコピー
    }

    return output;
}

// ボックスブラーフィルタ
Image16 BoxBlurFilter16::apply(const Image16& input) const {
    int width = input.width;
    int height = input.height;
    int radius = params_.radius;

    // 中間バッファ（水平ブラー結果）
    Image16 temp(width, height);

    // パス1: 水平方向のブラー
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            int count = 0;

            int xStart = std::max(0, x - radius);
            int xEnd = std::min(width - 1, x + radius);

            for (int nx = xStart; nx <= xEnd; nx++) {
                int idx = (y * width + nx) * 4;
                sumR += input.data[idx];
                sumG += input.data[idx + 1];
                sumB += input.data[idx + 2];
                sumA += input.data[idx + 3];
                count++;
            }

            int outIdx = (y * width + x) * 4;
            temp.data[outIdx] = sumR / count;
            temp.data[outIdx + 1] = sumG / count;
            temp.data[outIdx + 2] = sumB / count;
            temp.data[outIdx + 3] = sumA / count;
        }
    }

    // パス2: 垂直方向のブラー
    Image16 output(width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            int count = 0;

            int yStart = std::max(0, y - radius);
            int yEnd = std::min(height - 1, y + radius);

            for (int ny = yStart; ny <= yEnd; ny++) {
                int idx = (ny * width + x) * 4;
                sumR += temp.data[idx];
                sumG += temp.data[idx + 1];
                sumB += temp.data[idx + 2];
                sumA += temp.data[idx + 3];
                count++;
            }

            int outIdx = (y * width + x) * 4;
            output.data[outIdx] = sumR / count;
            output.data[outIdx + 1] = sumG / count;
            output.data[outIdx + 2] = sumB / count;
            output.data[outIdx + 3] = sumA / count;
        }
    }

    return output;
}

} // namespace ImageTransform
