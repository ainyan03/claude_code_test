#include "viewport.h"
#include "pixel_format.h"
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

    // 異なるフォーマット間のコピー
    for (int y = 0; y < height; ++y) {
        const void* srcRow = src.pixelAt(srcX, srcY + y);
        void* dstRow = dst.pixelAt(dstX, dstY + y);
        convertFormat(srcRow, src.formatID, dstRow, dst.formatID, width);
    }
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

} // namespace view_ops
} // namespace FLEXIMG_NAMESPACE
