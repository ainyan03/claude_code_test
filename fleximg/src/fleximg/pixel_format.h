#ifndef FLEXIMG_PIXEL_FORMAT_H
#define FLEXIMG_PIXEL_FORMAT_H

#include <cstdint>
#include "common.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// ピクセルフォーマットID
// ========================================================================

using PixelFormatID = uint32_t;

namespace PixelFormatIDs {
    // 16bit RGBA系
    constexpr PixelFormatID RGBA16_Straight       = 0x0001;
    constexpr PixelFormatID RGBA16_Premultiplied  = 0x0002;

    // 8bit RGBA系
    constexpr PixelFormatID RGBA8_Straight        = 0x0200;
    constexpr PixelFormatID RGBA8_Premultiplied   = 0x0201;

    // RGBA16_Premultiplied用アルファ閾値
    namespace RGBA16Premul {
        constexpr uint16_t ALPHA_TRANSPARENT_MAX = 255;
        constexpr uint16_t ALPHA_OPAQUE_MIN = 65280;

        inline constexpr bool isTransparent(uint16_t a) { return a <= ALPHA_TRANSPARENT_MAX; }
        inline constexpr bool isOpaque(uint16_t a) { return a >= ALPHA_OPAQUE_MIN; }
    }
}

// ========================================================================
// ピクセルフォーマット情報取得（簡易版）
// ========================================================================

inline size_t getBytesPerPixel(PixelFormatID formatID) {
    switch (formatID) {
        case PixelFormatIDs::RGBA16_Straight:
        case PixelFormatIDs::RGBA16_Premultiplied:
            return 8;  // 16bit x 4 = 64bit = 8bytes
        case PixelFormatIDs::RGBA8_Straight:
        case PixelFormatIDs::RGBA8_Premultiplied:
            return 4;  // 8bit x 4 = 32bit = 4bytes
        default:
            return 4;
    }
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_PIXEL_FORMAT_H
