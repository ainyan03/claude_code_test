#ifndef FLEXIMG_PIXEL_FORMAT_RGB565_H
#define FLEXIMG_PIXEL_FORMAT_RGB565_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
    extern const PixelFormatDescriptor RGB565_LE;
    extern const PixelFormatDescriptor RGB565_BE;
}

namespace PixelFormatIDs {
    inline const PixelFormatID RGB565_LE = &BuiltinFormats::RGB565_LE;
    inline const PixelFormatID RGB565_BE = &BuiltinFormats::RGB565_BE;
}

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

#include "../../core/format_metrics.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// RGB565_LE: 16bit RGB (Little Endian)
// ========================================================================

// RGB565 → RGB8 変換ルックアップテーブル
// RGB565の16bit値を上位バイトと下位バイトに分けて処理
//
// RGB565構造 (16bit): RRRRR GGGGGG BBBBB
//   high_byte: RRRRRGGG (R5全部 + G6上位3bit)
//   low_byte:  GGGBBBBB (G6下位3bit + B5全部)
//
// G8の分離計算:
//   G8 = (G6 << 2) | (G6 >> 4)
//      = (high_G3 << 5) + (high_G3 >> 1) + (low_G3 << 2) + (low_G3 >> 4)
//   ※ low_G3 >> 4 は low_G3 が 0-7 なので常に 0
//
// テーブル構成 (uint16_t配列、リトルエンディアン前提):
//   high_table[high_byte] = (G_high << 8) | R8  where G_high = (high_G3 << 5) + (high_G3 >> 1)
//   low_table[low_byte]   = (G_low << 8) | B8   where G_low  = low_G3 << 2
//
// 両テーブルとも上位バイトに緑成分を配置
//
namespace {

// 上位バイト用エントリ: uint16_t値 = (G_high << 8) | R8
#define RGB565_HIGH_ENTRY(h) \
    static_cast<uint16_t>( \
        (((((h) & 0x07) << 5) | (((h) & 0x07) >> 1)) << 8) | \
        ((((h) >> 3) << 3) | (((h) >> 3) >> 2)) \
    )

// 下位バイト用エントリ: uint16_t値 = (G_low << 8) | B8
#define RGB565_LOW_ENTRY(l) \
    static_cast<uint16_t>( \
        (((((l) >> 5) & 0x07) << 2) << 8) | \
        ((((l) & 0x1F) << 3) | (((l) & 0x1F) >> 2)) \
    )

#define RGB565_HIGH_ROW(base) \
    RGB565_HIGH_ENTRY(base+0), RGB565_HIGH_ENTRY(base+1), RGB565_HIGH_ENTRY(base+2), RGB565_HIGH_ENTRY(base+3), \
    RGB565_HIGH_ENTRY(base+4), RGB565_HIGH_ENTRY(base+5), RGB565_HIGH_ENTRY(base+6), RGB565_HIGH_ENTRY(base+7), \
    RGB565_HIGH_ENTRY(base+8), RGB565_HIGH_ENTRY(base+9), RGB565_HIGH_ENTRY(base+10), RGB565_HIGH_ENTRY(base+11), \
    RGB565_HIGH_ENTRY(base+12), RGB565_HIGH_ENTRY(base+13), RGB565_HIGH_ENTRY(base+14), RGB565_HIGH_ENTRY(base+15)

#define RGB565_LOW_ROW(base) \
    RGB565_LOW_ENTRY(base+0), RGB565_LOW_ENTRY(base+1), RGB565_LOW_ENTRY(base+2), RGB565_LOW_ENTRY(base+3), \
    RGB565_LOW_ENTRY(base+4), RGB565_LOW_ENTRY(base+5), RGB565_LOW_ENTRY(base+6), RGB565_LOW_ENTRY(base+7), \
    RGB565_LOW_ENTRY(base+8), RGB565_LOW_ENTRY(base+9), RGB565_LOW_ENTRY(base+10), RGB565_LOW_ENTRY(base+11), \
    RGB565_LOW_ENTRY(base+12), RGB565_LOW_ENTRY(base+13), RGB565_LOW_ENTRY(base+14), RGB565_LOW_ENTRY(base+15)

// RGB565上位バイト用テーブル (256 × 2 = 512 bytes): (G_high << 8) | R8
alignas(64) static const uint16_t rgb565HighTable[256] = {
    RGB565_HIGH_ROW(0x00), RGB565_HIGH_ROW(0x10), RGB565_HIGH_ROW(0x20), RGB565_HIGH_ROW(0x30),
    RGB565_HIGH_ROW(0x40), RGB565_HIGH_ROW(0x50), RGB565_HIGH_ROW(0x60), RGB565_HIGH_ROW(0x70),
    RGB565_HIGH_ROW(0x80), RGB565_HIGH_ROW(0x90), RGB565_HIGH_ROW(0xa0), RGB565_HIGH_ROW(0xb0),
    RGB565_HIGH_ROW(0xc0), RGB565_HIGH_ROW(0xd0), RGB565_HIGH_ROW(0xe0), RGB565_HIGH_ROW(0xf0)
};

// RGB565下位バイト用テーブル (256 × 2 = 512 bytes): (G_low << 8) | B8
alignas(64) static const uint16_t rgb565LowTable[256] = {
    RGB565_LOW_ROW(0x00), RGB565_LOW_ROW(0x10), RGB565_LOW_ROW(0x20), RGB565_LOW_ROW(0x30),
    RGB565_LOW_ROW(0x40), RGB565_LOW_ROW(0x50), RGB565_LOW_ROW(0x60), RGB565_LOW_ROW(0x70),
    RGB565_LOW_ROW(0x80), RGB565_LOW_ROW(0x90), RGB565_LOW_ROW(0xa0), RGB565_LOW_ROW(0xb0),
    RGB565_LOW_ROW(0xc0), RGB565_LOW_ROW(0xd0), RGB565_LOW_ROW(0xe0), RGB565_LOW_ROW(0xf0)
};

#undef RGB565_HIGH_ENTRY
#undef RGB565_LOW_ENTRY
#undef RGB565_HIGH_ROW
#undef RGB565_LOW_ROW

} // namespace

// RGB565_LE→RGBA8_Straight 1ピクセル変換マクロ（ルックアップテーブル使用）
// s: uint8_t*, d: uint8_t*, s_off: srcオフセット, d_off: dstオフセット
#define RGB565LE_TO_STRAIGHT_PIXEL(s_off, d_off) \
    do { \
        auto l16 = rgb565LowTable[s[s_off]]; \
        auto h16 = rgb565HighTable[s[s_off + 1]]; \
        d[d_off + 3] = 255; \
        d[d_off + 2] = static_cast<uint8_t>(l16); \
        *reinterpret_cast<uint16_t*>(&d[d_off]) = static_cast<uint16_t>(h16 + (l16 & 0xFF00)); \
    } while(0)

static void rgb565le_toStraight(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_LE, ToStraight, pixelCount);
    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);
    uint8_t* __restrict__ d = static_cast<uint8_t*>(dst);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        RGB565LE_TO_STRAIGHT_PIXEL(0, 0);
        s += 2;
        d += 4;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        RGB565LE_TO_STRAIGHT_PIXEL(0, 0);
        RGB565LE_TO_STRAIGHT_PIXEL(2, 4);
        RGB565LE_TO_STRAIGHT_PIXEL(4, 8);
        RGB565LE_TO_STRAIGHT_PIXEL(6, 12);
        s += 8;
        d += 16;
    }
}
#undef RGB565LE_TO_STRAIGHT_PIXEL

static void rgb565le_fromStraight(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_LE, FromStraight, pixelCount);
    uint16_t* __restrict__ d = static_cast<uint16_t*>(dst);
    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        *d++ = static_cast<uint16_t>(((s[0] >> 3) << 11) | ((s[1] >> 2) << 5) | (s[2] >> 3));
        s += 4;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        // ピクセル0
        d[0] = static_cast<uint16_t>(((s[0] >> 3) << 11) | ((s[1] >> 2) << 5) | (s[2] >> 3));
        // ピクセル1
        d[1] = static_cast<uint16_t>(((s[4] >> 3) << 11) | ((s[5] >> 2) << 5) | (s[6] >> 3));
        // ピクセル2
        d[2] = static_cast<uint16_t>(((s[8] >> 3) << 11) | ((s[9] >> 2) << 5) | (s[10] >> 3));
        // ピクセル3
        d[3] = static_cast<uint16_t>(((s[12] >> 3) << 11) | ((s[13] >> 2) << 5) | (s[14] >> 3));
        s += 16;
        d += 4;
    }
}

// ========================================================================
// RGB565_BE: 16bit RGB (Big Endian)
// ========================================================================

// RGB565_BE→RGBA8_Straight 1ピクセル変換マクロ（ルックアップテーブル使用）
// s: uint8_t*, d: uint8_t*, s_off: srcオフセット, d_off: dstオフセット
// RGB565_BE: [high_byte, low_byte] in memory（LEとは逆）
#define RGB565BE_TO_STRAIGHT_PIXEL(s_off, d_off) \
    do { \
        auto l16 = rgb565LowTable[s[s_off + 1]]; \
        auto h16 = rgb565HighTable[s[s_off]]; \
        d[d_off + 3] = 255; \
        d[d_off + 2] = static_cast<uint8_t>(l16); \
        *reinterpret_cast<uint16_t*>(&d[d_off]) = static_cast<uint16_t>(h16 + (l16 & 0xFF00)); \
    } while(0)

static void rgb565be_toStraight(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_BE, ToStraight, pixelCount);
    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);
    uint8_t* __restrict__ d = static_cast<uint8_t*>(dst);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        RGB565BE_TO_STRAIGHT_PIXEL(0, 0);
        s += 2;
        d += 4;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        RGB565BE_TO_STRAIGHT_PIXEL(0, 0);
        RGB565BE_TO_STRAIGHT_PIXEL(2, 4);
        RGB565BE_TO_STRAIGHT_PIXEL(4, 8);
        RGB565BE_TO_STRAIGHT_PIXEL(6, 12);
        s += 8;
        d += 16;
    }
}
#undef RGB565BE_TO_STRAIGHT_PIXEL

static void rgb565be_fromStraight(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_BE, FromStraight, pixelCount);
    uint8_t* __restrict__ d = static_cast<uint8_t*>(dst);
    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        // RGB565_BE: RRRRRGGG GGGBBBBB
        d[0] = (s[0] & 0xF8) | (s[1] >> 5);           // 上位: R[7:3] | G[7:5]
        d[1] = ((s[1] << 3) & 0xE0) | (s[2] >> 3);    // 下位: G[4:2] | B[7:3]
        s += 4;
        d += 2;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        // ピクセル0
        d[0] = (s[0] & 0xF8) | (s[1] >> 5);
        d[1] = ((s[1] << 3) & 0xE0) | (s[2] >> 3);
        // ピクセル1
        d[2] = (s[4] & 0xF8) | (s[5] >> 5);
        d[3] = ((s[5] << 3) & 0xE0) | (s[6] >> 3);
        // ピクセル2
        d[4] = (s[8] & 0xF8) | (s[9] >> 5);
        d[5] = ((s[9] << 3) & 0xE0) | (s[10] >> 3);
        // ピクセル3
        d[6] = (s[12] & 0xF8) | (s[13] >> 5);
        d[7] = ((s[13] << 3) & 0xE0) | (s[14] >> 3);
        s += 16;
        d += 8;
    }
}

// ========================================================================
// 16bit用バイトスワップ（RGB565_LE ↔ RGB565_BE）
// ========================================================================

static void swap16(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    const uint16_t* srcPtr = static_cast<const uint16_t*>(src);
    uint16_t* dstPtr = static_cast<uint16_t*>(dst);
    for (int i = 0; i < pixelCount; ++i) {
        uint16_t v = srcPtr[i];
        dstPtr[i] = static_cast<uint16_t>((v >> 8) | (v << 8));
    }
}

// ------------------------------------------------------------------------
// フォーマット定義
// ------------------------------------------------------------------------

namespace BuiltinFormats {

// Forward declaration for sibling reference
extern const PixelFormatDescriptor RGB565_BE;

const PixelFormatDescriptor RGB565_LE = {
    "RGB565_LE",
    16,  // bitsPerPixel
    1,   // pixelsPerUnit
    2,   // bytesPerUnit
    3,   // channelCount
    { ChannelDescriptor(ChannelType::Red, 5, 11),
      ChannelDescriptor(ChannelType::Green, 6, 5),
      ChannelDescriptor(ChannelType::Blue, 5, 0),
      ChannelDescriptor() },  // R, G, B, (no A)
    false,  // hasAlpha
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::LittleEndian,
    rgb565le_toStraight,
    rgb565le_fromStraight,
    nullptr,  // expandIndex
    nullptr,  // blendUnderStraight
    &RGB565_BE,  // siblingEndian
    swap16       // swapEndian
};

const PixelFormatDescriptor RGB565_BE = {
    "RGB565_BE",
    16,  // bitsPerPixel
    1,   // pixelsPerUnit
    2,   // bytesPerUnit
    3,   // channelCount
    { ChannelDescriptor(ChannelType::Red, 5, 11),
      ChannelDescriptor(ChannelType::Green, 6, 5),
      ChannelDescriptor(ChannelType::Blue, 5, 0),
      ChannelDescriptor() },  // R, G, B, (no A)
    false,  // hasAlpha
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::BigEndian,
    rgb565be_toStraight,
    rgb565be_fromStraight,
    nullptr,  // expandIndex
    nullptr,  // blendUnderStraight
    &RGB565_LE,  // siblingEndian
    swap16       // swapEndian
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_RGB565_H
