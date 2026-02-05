#ifndef FLEXIMG_PIXEL_FORMAT_ALPHA8_H
#define FLEXIMG_PIXEL_FORMAT_ALPHA8_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
    extern const PixelFormatDescriptor Alpha8;
}

namespace PixelFormatIDs {
    inline const PixelFormatID Alpha8 = &BuiltinFormats::Alpha8;
}

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

#include "../../core/format_metrics.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// Alpha8: 単一アルファチャンネル ↔ RGBA8_Straight 変換
// ========================================================================

// Alpha8 → RGBA8_Straight（可視化のため全チャンネルにアルファ値を展開）
static void alpha8_toStraight(void* dst, const void* src, int pixelCount, const PixelAuxInfo*) {
    FLEXIMG_FMT_METRICS(Alpha8, ToStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; ++i) {
        uint8_t alpha = s[i];
        d[i*4 + 0] = alpha;  // R
        d[i*4 + 1] = alpha;  // G
        d[i*4 + 2] = alpha;  // B
        d[i*4 + 3] = alpha;  // A
    }
}

// RGBA8_Straight → Alpha8（Aチャンネルのみ抽出）
static void alpha8_fromStraight(void* dst, const void* src, int pixelCount, const PixelAuxInfo*) {
    FLEXIMG_FMT_METRICS(Alpha8, FromStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; ++i) {
        d[i] = s[i*4 + 3];  // Aチャンネル抽出
    }
}

// ------------------------------------------------------------------------
// フォーマット定義
// ------------------------------------------------------------------------

namespace BuiltinFormats {

const PixelFormatDescriptor Alpha8 = {
    "Alpha8",
    8,   // bitsPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    1,   // channelCount
    { ChannelDescriptor(ChannelType::Alpha, 8, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },  // Alpha only
    true,   // hasAlpha
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    alpha8_toStraight,
    alpha8_fromStraight,
    nullptr,  // expandIndex
    nullptr,  // blendUnderStraight
    nullptr,  // siblingEndian
    nullptr,  // swapEndian
    pixel_format::detail::copyRowDDA_1bpp,  // copyRowDDA
    pixel_format::detail::copyQuadDDA_1bpp  // copyQuadDDA
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_ALPHA8_H
