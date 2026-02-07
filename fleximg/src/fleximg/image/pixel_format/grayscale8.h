#ifndef FLEXIMG_PIXEL_FORMAT_GRAYSCALE8_H
#define FLEXIMG_PIXEL_FORMAT_GRAYSCALE8_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
    extern const PixelFormatDescriptor Grayscale8;
}

namespace PixelFormatIDs {
    inline const PixelFormatID Grayscale8 = &BuiltinFormats::Grayscale8;
}

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

#include "../../core/format_metrics.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// Grayscale8: 単一輝度チャンネル ↔ RGBA8_Straight 変換
// ========================================================================

// Grayscale8 → RGBA8_Straight（L → R=G=B=L, A=255）
static void grayscale8_toStraight(void* dst, const void* src, int pixelCount, const PixelAuxInfo*) {
    FLEXIMG_FMT_METRICS(Grayscale8, ToStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; ++i) {
        uint8_t lum = s[i];
        d[i*4 + 0] = lum;   // R
        d[i*4 + 1] = lum;   // G
        d[i*4 + 2] = lum;   // B
        d[i*4 + 3] = 255;   // A
    }
}

// RGBA8_Straight → Grayscale8（BT.601 輝度計算）
static void grayscale8_fromStraight(void* dst, const void* src, int pixelCount, const PixelAuxInfo*) {
    FLEXIMG_FMT_METRICS(Grayscale8, FromStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; ++i) {
        // BT.601: Y = 0.299*R + 0.587*G + 0.114*B
        // 整数近似: (77*R + 150*G + 29*B + 128) >> 8
        uint_fast16_t r = s[i*4 + 0];
        uint_fast16_t g = s[i*4 + 1];
        uint_fast16_t b = s[i*4 + 2];
        d[i] = static_cast<uint8_t>((77 * r + 150 * g + 29 * b + 128) >> 8);
    }
}

// ------------------------------------------------------------------------
// フォーマット定義
// ------------------------------------------------------------------------

namespace BuiltinFormats {

const PixelFormatDescriptor Grayscale8 = {
    "Grayscale8",
    grayscale8_toStraight,
    grayscale8_fromStraight,
    nullptr,  // expandIndex
    nullptr,  // blendUnderStraight
    nullptr,  // siblingEndian
    nullptr,  // swapEndian
    pixel_format::detail::copyRowDDA_1Byte,  // copyRowDDA
    pixel_format::detail::copyQuadDDA_1Byte, // copyQuadDDA
    BitOrder::MSBFirst,
    ByteOrder::Native,
    0,      // maxPaletteSize
    8,   // bitsPerPixel
    1,   // bytesPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    1,   // channelCount
    false,  // hasAlpha
    false,  // isIndexed
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_GRAYSCALE8_H
