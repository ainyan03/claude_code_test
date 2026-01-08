#include "viewport.h"
#include "image_buffer.h"
#include "pixel_format.h"
#include <cstring>
#include <algorithm>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// blendFirst - 透明キャンバスへの最初の描画（memcpy最適化）
// ========================================================================

void ViewPort::blendFirst(const ViewPort& src, int offsetX, int offsetY) {
    // クリッピング範囲を計算
    int srcStartX = std::max(0, -offsetX);
    int srcStartY = std::max(0, -offsetY);
    int dstStartX = std::max(0, offsetX);
    int dstStartY = std::max(0, offsetY);
    int copyWidth = std::min(src.width - srcStartX, width - dstStartX);
    int copyHeight = std::min(src.height - srcStartY, height - dstStartY);

    if (copyWidth <= 0 || copyHeight <= 0) return;

    // 行単位でmemcpy（透明キャンバスへの最初の合成なのでブレンド不要）
    for (int y = 0; y < copyHeight; y++) {
        const uint16_t* srcRow = src.getPixelPtr<uint16_t>(srcStartX, srcStartY + y);
        uint16_t* dstRow = getPixelPtr<uint16_t>(dstStartX, dstStartY + y);
        std::memcpy(dstRow, srcRow, copyWidth * 4 * sizeof(uint16_t));
    }
}

// ========================================================================
// blendOnto - 既存画像への合成（アルファブレンド）
// ========================================================================

void ViewPort::blendOnto(const ViewPort& src, int offsetX, int offsetY) {
    // ループ範囲を事前計算
    int yStart = std::max(0, -offsetY);
    int yEnd = std::min(src.height, height - offsetY);
    int xStart = std::max(0, -offsetX);
    int xEnd = std::min(src.width, width - offsetX);

    if (yStart >= yEnd || xStart >= xEnd) return;

    // 閾値定数をローカル変数にキャッシュ（ループ最適化）
    constexpr uint16_t ALPHA_TRANS_MAX = PixelFormatIDs::RGBA16Premul::ALPHA_TRANSPARENT_MAX;
    constexpr uint16_t ALPHA_OPAQUE_MIN = PixelFormatIDs::RGBA16Premul::ALPHA_OPAQUE_MIN;

    for (int y = yStart; y < yEnd; y++) {
        const uint16_t* srcRow = src.getPixelPtr<uint16_t>(0, y);
        uint16_t* dstRow = getPixelPtr<uint16_t>(0, y + offsetY);

        for (int x = xStart; x < xEnd; x++) {
            const uint16_t* srcPixel = srcRow + x * 4;
            uint16_t* dstPixel = dstRow + (x + offsetX) * 4;

            uint16_t srcA = srcPixel[3];
            // 透明スキップ
            if (srcA <= ALPHA_TRANS_MAX) continue;

            uint16_t srcR = srcPixel[0];
            uint16_t srcG = srcPixel[1];
            uint16_t srcB = srcPixel[2];
            uint16_t dstA = dstPixel[3];

            // 不透明最適化
            if (srcA < ALPHA_OPAQUE_MIN && dstA != 0) {
                // プリマルチプライド合成: src over dst
                uint16_t invSrcA = 65535 - srcA;
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

// ========================================================================
// toImageBuffer - ビューをImageBufferにコピー（フォーマット変換対応）
// ========================================================================

ImageBuffer ViewPort::toImageBuffer(PixelFormatID targetFormat) const {
    if (!isValid()) {
        return ImageBuffer();
    }

    // ターゲットフォーマットが指定されていない場合は現在のフォーマットを使用
    PixelFormatID outputFormat = (targetFormat == 0) ? formatID : targetFormat;

    // 同じフォーマットの場合は単純コピー
    if (outputFormat == formatID) {
        ImageBuffer result(width, height, formatID);
        size_t bytesPerPixel = getBytesPerPixel();
        for (int y = 0; y < height; y++) {
            const void* srcRow = getPixelAddress(0, y);
            void* dstRow = result.getPixelAddress(0, y);
            std::memcpy(dstRow, srcRow, width * bytesPerPixel);
        }
        return result;
    }

    // フォーマット変換が必要
    // ImageBuffer::convertTo を使用するため、まず同じフォーマットでコピー
    ImageBuffer temp(width, height, formatID);
    size_t bytesPerPixel = getBytesPerPixel();
    for (int y = 0; y < height; y++) {
        const void* srcRow = getPixelAddress(0, y);
        void* dstRow = temp.getPixelAddress(0, y);
        std::memcpy(dstRow, srcRow, width * bytesPerPixel);
    }

    // フォーマット変換して返す
    return temp.convertTo(outputFormat);
}

} // namespace FLEXIMG_NAMESPACE
