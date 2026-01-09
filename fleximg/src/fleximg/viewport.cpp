#include "viewport.h"
#include <cstring>
#include <algorithm>

namespace FLEXIMG_NAMESPACE {
namespace view_ops {

void copy(ViewPort& dst, int dstX, int dstY,
          const ViewPort& src, int srcX, int srcY,
          int width, int height) {
    if (!dst.isValid() || !src.isValid()) return;

    // クリッピング
    if (srcX < 0) { dstX -= srcX; width += srcX; srcX = 0; }
    if (srcY < 0) { dstY -= srcY; height += srcY; srcY = 0; }
    if (dstX < 0) { srcX -= dstX; width += dstX; dstX = 0; }
    if (dstY < 0) { srcY -= dstY; height += dstY; dstY = 0; }
    width = std::min(width, std::min(src.width - srcX, dst.width - dstX));
    height = std::min(height, std::min(src.height - srcY, dst.height - dstY));
    if (width <= 0 || height <= 0) return;

    // 同一フォーマットならmemcpy
    if (src.formatID == dst.formatID) {
        size_t bpp = dst.bytesPerPixel();
        for (int y = 0; y < height; ++y) {
            const uint8_t* srcRow = static_cast<const uint8_t*>(src.pixelAt(srcX, srcY + y));
            uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(dstX, dstY + y));
            std::memcpy(dstRow, srcRow, width * bpp);
        }
        return;
    }

    // RGBA16_Premultiplied → RGBA8_Straight 変換
    if (src.formatID == PixelFormatIDs::RGBA16_Premultiplied &&
        dst.formatID == PixelFormatIDs::RGBA8_Straight) {
        for (int y = 0; y < height; ++y) {
            const uint16_t* srcRow = static_cast<const uint16_t*>(src.pixelAt(srcX, srcY + y));
            uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(dstX, dstY + y));

            for (int x = 0; x < width; ++x) {
                uint16_t r = srcRow[x * 4 + 0];
                uint16_t g = srcRow[x * 4 + 1];
                uint16_t b = srcRow[x * 4 + 2];
                uint16_t a = srcRow[x * 4 + 3];

                // Premultiplied → Straight (アルファで除算)
                if (a > 0) {
                    r = std::min(65535u, (static_cast<uint32_t>(r) * 65535) / a);
                    g = std::min(65535u, (static_cast<uint32_t>(g) * 65535) / a);
                    b = std::min(65535u, (static_cast<uint32_t>(b) * 65535) / a);
                }

                // 16bit → 8bit
                dstRow[x * 4 + 0] = static_cast<uint8_t>(r >> 8);
                dstRow[x * 4 + 1] = static_cast<uint8_t>(g >> 8);
                dstRow[x * 4 + 2] = static_cast<uint8_t>(b >> 8);
                dstRow[x * 4 + 3] = static_cast<uint8_t>(a >> 8);
            }
        }
        return;
    }

    // RGBA8_Straight → RGBA8_Straight (異なる場合のフォールバック)
    // その他のフォーマット組み合わせは未対応
}

void clear(ViewPort& dst, int x, int y, int width, int height) {
    if (!dst.isValid()) return;

    size_t bpp = dst.bytesPerPixel();
    for (int row = 0; row < height; ++row) {
        int dy = y + row;
        if (dy < 0 || dy >= dst.height) continue;
        uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(x, dy));
        std::memset(dstRow, 0, width * bpp);
    }
}

void blendFirst(ViewPort& dst, int dstX, int dstY,
                const ViewPort& src, int srcX, int srcY,
                int width, int height) {
    // 最初のブレンドはmemcpy最適化
    copy(dst, dstX, dstY, src, srcX, srcY, width, height);
}

void blendOnto(ViewPort& dst, int dstX, int dstY,
               const ViewPort& src, int srcX, int srcY,
               int width, int height) {
    if (!dst.isValid() || !src.isValid()) return;
    if (dst.formatID != PixelFormatIDs::RGBA16_Premultiplied ||
        src.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
        // 非対応フォーマットはコピーにフォールバック
        copy(dst, dstX, dstY, src, srcX, srcY, width, height);
        return;
    }

    // RGBA16_Premultiplied アルファブレンド
    for (int y = 0; y < height; ++y) {
        int sy = srcY + y;
        int dy = dstY + y;
        if (sy < 0 || sy >= src.height || dy < 0 || dy >= dst.height) continue;

        const uint16_t* srcRow = static_cast<const uint16_t*>(src.pixelAt(srcX, sy));
        uint16_t* dstRow = static_cast<uint16_t*>(dst.pixelAt(dstX, dy));

        for (int x = 0; x < width; ++x) {
            const uint16_t* sp = srcRow + x * 4;
            uint16_t* dp = dstRow + x * 4;

            uint16_t srcA = sp[3];

            if (PixelFormatIDs::RGBA16Premul::isTransparent(srcA)) {
                continue;  // 透明 - スキップ
            }
            if (PixelFormatIDs::RGBA16Premul::isOpaque(srcA)) {
                // 不透明 - 上書き
                dp[0] = sp[0];
                dp[1] = sp[1];
                dp[2] = sp[2];
                dp[3] = sp[3];
                continue;
            }

            // アルファブレンド: dst = src + dst * (1 - srcA/65535)
            uint32_t invAlpha = 65535 - srcA;
            dp[0] = sp[0] + ((dp[0] * invAlpha) >> 16);
            dp[1] = sp[1] + ((dp[1] * invAlpha) >> 16);
            dp[2] = sp[2] + ((dp[2] * invAlpha) >> 16);
            dp[3] = sp[3] + ((dp[3] * invAlpha) >> 16);
        }
    }
}

} // namespace view_ops
} // namespace FLEXIMG_NAMESPACE
