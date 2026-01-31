#ifndef FLEXIMG_PIXEL_FORMAT_RGB332_H
#define FLEXIMG_PIXEL_FORMAT_RGB332_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
    extern const PixelFormatDescriptor RGB332;
}

namespace PixelFormatIDs {
    inline const PixelFormatID RGB332 = &BuiltinFormats::RGB332;
}

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

#include "../../core/format_metrics.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// RGB332: 8bit RGB (3-3-2)
// ========================================================================

// RGB332 → RGBA8 変換ルックアップテーブル
// RGB332の256通りの値に対して、RGBA8値を事前計算
// 各エントリ: uint32_t (リトルエンディアン: R8 | G8<<8 | B8<<16 | A8<<24)
// 32bitロード/ストアで効率的に変換可能
namespace {

// テーブル生成用マクロ: uint32_t値 = (255 << 24) | (B8 << 16) | (G8 << 8) | R8
#define RGB332_ENTRY(p) \
    static_cast<uint32_t>( \
        (static_cast<uint32_t>((((p) >> 5) & 0x07) * 0x49 >> 1)) | \
        (static_cast<uint32_t>((((p) >> 2) & 0x07) * 0x49 >> 1) << 8) | \
        (static_cast<uint32_t>(((p) & 0x03) * 0x55) << 16) | \
        (static_cast<uint32_t>(255) << 24) \
    )

#define RGB332_ROW(base) \
    RGB332_ENTRY(base+0), RGB332_ENTRY(base+1), RGB332_ENTRY(base+2), RGB332_ENTRY(base+3), \
    RGB332_ENTRY(base+4), RGB332_ENTRY(base+5), RGB332_ENTRY(base+6), RGB332_ENTRY(base+7), \
    RGB332_ENTRY(base+8), RGB332_ENTRY(base+9), RGB332_ENTRY(base+10), RGB332_ENTRY(base+11), \
    RGB332_ENTRY(base+12), RGB332_ENTRY(base+13), RGB332_ENTRY(base+14), RGB332_ENTRY(base+15)

// RGB332 → RGBA8 変換テーブル (256 × 4 = 1024 bytes)
alignas(64) static const uint32_t rgb332ToRgba8[256] = {
    RGB332_ROW(0x00), RGB332_ROW(0x10), RGB332_ROW(0x20), RGB332_ROW(0x30),
    RGB332_ROW(0x40), RGB332_ROW(0x50), RGB332_ROW(0x60), RGB332_ROW(0x70),
    RGB332_ROW(0x80), RGB332_ROW(0x90), RGB332_ROW(0xa0), RGB332_ROW(0xb0),
    RGB332_ROW(0xc0), RGB332_ROW(0xd0), RGB332_ROW(0xe0), RGB332_ROW(0xf0)
};

#undef RGB332_ENTRY
#undef RGB332_ROW

} // namespace

static void rgb332_toStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB332, ToStraight, pixelCount);
    pixel_format::detail::lut8to32(static_cast<uint32_t*>(dst),
                     static_cast<const uint8_t*>(src),
                     pixelCount, rgb332ToRgba8);
}

// RGBA8 → RGB332 変換マクロ（32bitロードした値から変換）
// (((r << 3) + g) << 2) + b の形式でESP32のシフト+加算命令を活用
#define RGBA8_TO_RGB332(rgba) \
    (((((rgba) >> 5) << 3) + (((rgba) >> 13) & 0x07)) << 2) + (((rgba) >> 22) & 0x03)

static void rgb332_fromStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB332, FromStraight, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint32_t* s = static_cast<const uint32_t*>(src);

    // 端数処理（1ピクセル）
    if (pixelCount & 1) {
        auto rgba = *s++;
        *d++ = static_cast<uint8_t>(RGBA8_TO_RGB332(rgba));
    }

    // 2ピクセル単位でループ（ロードを先に発行してレイテンシ隠蔽）
    pixelCount >>= 1;
    while (pixelCount--) {
        // 2つのロードを先に発行
        auto rgba0 = s[0];
        auto rgba1 = s[1];
        s += 2;
        // 演算と8bitストア
        d[0] = static_cast<uint8_t>(RGBA8_TO_RGB332(rgba0));
        d[1] = static_cast<uint8_t>(RGBA8_TO_RGB332(rgba1));
        d += 2;
    }
}
#undef RGBA8_TO_RGB332

// ------------------------------------------------------------------------
// フォーマット定義
// ------------------------------------------------------------------------

namespace BuiltinFormats {

const PixelFormatDescriptor RGB332 = {
    "RGB332",
    8,   // bitsPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    3,   // channelCount
    { ChannelDescriptor(ChannelType::Red, 3, 5),
      ChannelDescriptor(ChannelType::Green, 3, 2),
      ChannelDescriptor(ChannelType::Blue, 2, 0),
      ChannelDescriptor() },  // R, G, B, (no A)
    false,  // hasAlpha
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    rgb332_toStraight,
    rgb332_fromStraight,
    nullptr,  // expandIndex
    nullptr,  // blendUnderStraight
    nullptr,  // siblingEndian
    nullptr   // swapEndian
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_RGB332_H
