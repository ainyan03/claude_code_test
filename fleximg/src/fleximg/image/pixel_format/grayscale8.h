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
static void grayscale8_toStraight(void* dst, const void* src, size_t pixelCount, const PixelAuxInfo*) {
    FLEXIMG_FMT_METRICS(Grayscale8, ToStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < pixelCount; ++i) {
        uint8_t lum = s[i];
        d[i*4 + 0] = lum;   // R
        d[i*4 + 1] = lum;   // G
        d[i*4 + 2] = lum;   // B
        d[i*4 + 3] = 255;   // A
    }
}

// RGBA8_Straight → Grayscale8（BT.601 輝度計算）
static void grayscale8_fromStraight(void* dst, const void* src, size_t pixelCount, const PixelAuxInfo*) {
    FLEXIMG_FMT_METRICS(Grayscale8, FromStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);

    // BT.601: Y = 0.299*R + 0.587*G + 0.114*B
    // 整数近似: (77*R + 150*G + 29*B + 128) >> 8

    // 端数処理（1〜3ピクセル）
    size_t remainder = pixelCount & 3;
    while (remainder--) {
        d[0] = static_cast<uint8_t>((77 * s[0] + 150 * s[1] + 29 * s[2] + 128) >> 8);
        s += 4;
        d += 1;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        d[0] = static_cast<uint8_t>((77 * s[0] + 150 * s[1] + 29 * s[2] + 128) >> 8);
        d[1] = static_cast<uint8_t>((77 * s[4] + 150 * s[5] + 29 * s[6] + 128) >> 8);
        d[2] = static_cast<uint8_t>((77 * s[8] + 150 * s[9] + 29 * s[10] + 128) >> 8);
        d[3] = static_cast<uint8_t>((77 * s[12] + 150 * s[13] + 29 * s[14] + 128) >> 8);
        s += 16;
        d += 4;
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
