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
#ifndef FLEXIMG_PIXEL_FORMAT_RGB565_IMPL_INCLUDED
#define FLEXIMG_PIXEL_FORMAT_RGB565_IMPL_INCLUDED

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
// テーブル構成:
//   high_table[high_byte] = { R8, G_high }  where G_high = (high_G3 << 5) + (high_G3 >> 1)
//   low_table[low_byte]   = { G_low, B8 }   where G_low  = low_G3 << 2
//
namespace {

// 上位バイト用エントリ: R8とG_highを計算
#define RGB565_HIGH_ENTRY(h) \
    static_cast<uint8_t>((((h) >> 3) << 3) | (((h) >> 3) >> 2)), \
    static_cast<uint8_t>((((h) & 0x07) << 5) | (((h) & 0x07) >> 1))

// 下位バイト用エントリ: G_lowとB8を計算
#define RGB565_LOW_ENTRY(l) \
    static_cast<uint8_t>((((l) >> 5) & 0x07) << 2), \
    static_cast<uint8_t>((((l) & 0x1F) << 3) | (((l) & 0x1F) >> 2))

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

// RGB565上位バイト用テーブル (256 × 2 = 512 bytes): [R8, G_high]
alignas(64) static const uint8_t rgb565HighTable[256 * 2] = {
    RGB565_HIGH_ROW(0x00), RGB565_HIGH_ROW(0x10), RGB565_HIGH_ROW(0x20), RGB565_HIGH_ROW(0x30),
    RGB565_HIGH_ROW(0x40), RGB565_HIGH_ROW(0x50), RGB565_HIGH_ROW(0x60), RGB565_HIGH_ROW(0x70),
    RGB565_HIGH_ROW(0x80), RGB565_HIGH_ROW(0x90), RGB565_HIGH_ROW(0xa0), RGB565_HIGH_ROW(0xb0),
    RGB565_HIGH_ROW(0xc0), RGB565_HIGH_ROW(0xd0), RGB565_HIGH_ROW(0xe0), RGB565_HIGH_ROW(0xf0)
};

// RGB565下位バイト用テーブル (256 × 2 = 512 bytes): [G_low, B8]
alignas(64) static const uint8_t rgb565LowTable[256 * 2] = {
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
        const uint8_t* h = &rgb565HighTable[s[s_off + 1] * 2]; \
        const uint8_t* l = &rgb565LowTable[s[s_off] * 2]; \
        d[d_off]     = h[0]; \
        d[d_off + 1] = h[1] + l[0]; \
        d[d_off + 2] = l[1]; \
        d[d_off + 3] = 255; \
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

#ifdef FLEXIMG_ENABLE_PREMUL
// blendUnderPremul: srcフォーマット(RGB565_LE)からPremul形式のdstへunder合成
// RGB565はアルファなし→常に不透明として扱う
// 最適化: ルックアップテーブル使用
static void rgb565le_blendUnderPremul(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_LE, BlendUnder, pixelCount);
    uint16_t* __restrict__ d = static_cast<uint16_t*>(dst);
    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);

    for (int i = 0; i < pixelCount; ++i) {
        int idx16 = i * 4;
        // 8bit精度でアルファ取得（16bitの上位バイト）
        uint_fast8_t dstA8 = static_cast<uint_fast8_t>(d[idx16 + 3] >> 8);

        // dst が不透明 → スキップ
        if (dstA8 == 255) continue;

        // RGB565_LE → RGB8 変換（ルックアップテーブル使用）
        uint8_t low = s[i * 2];
        uint8_t high = s[i * 2 + 1];
        const uint8_t* h = &rgb565HighTable[high * 2];
        const uint8_t* l = &rgb565LowTable[low * 2];
        uint_fast8_t srcR8 = h[0];
        uint_fast8_t srcG8 = static_cast<uint_fast8_t>(h[1] + l[0]);
        uint_fast8_t srcB8 = l[1];

        // dst が透明 → 単純コピー（16bit形式で）
        if (dstA8 == 0) {
            d[idx16]     = static_cast<uint16_t>(srcR8 << 8);
            d[idx16 + 1] = static_cast<uint16_t>(srcG8 << 8);
            d[idx16 + 2] = static_cast<uint16_t>(srcB8 << 8);
            d[idx16 + 3] = RGBA16Premul::ALPHA_OPAQUE_MIN;
            continue;
        }

        // under合成（8bit精度）
        uint_fast16_t invDstA8 = 255 - dstA8;
        d[idx16]     = static_cast<uint16_t>(d[idx16]     + srcR8 * invDstA8);
        d[idx16 + 1] = static_cast<uint16_t>(d[idx16 + 1] + srcG8 * invDstA8);
        d[idx16 + 2] = static_cast<uint16_t>(d[idx16 + 2] + srcB8 * invDstA8);
        d[idx16 + 3] = static_cast<uint16_t>(d[idx16 + 3] + 255 * invDstA8);
    }
}

// toPremul: RGB565_LEのsrcからPremul形式のdstへ変換コピー
// アルファなし→完全不透明として変換（ルックアップテーブル使用）
// RGB565_LE→RGBA16_Premul 1ピクセル変換マクロ
// s: uint8_t*, d: uint16_t*, s_off: srcオフセット, d_off: dstオフセット(uint16_t単位)
#define RGB565LE_TO_PREMUL_PIXEL(s_off, d_off) \
    do { \
        const uint8_t* h = &rgb565HighTable[s[s_off + 1] * 2]; \
        const uint8_t* l = &rgb565LowTable[s[s_off] * 2]; \
        d[d_off]     = static_cast<uint16_t>(h[0] << 8); \
        d[d_off + 1] = static_cast<uint16_t>((h[1] + l[0]) << 8); \
        d[d_off + 2] = static_cast<uint16_t>(l[1] << 8); \
        d[d_off + 3] = RGBA16Premul::ALPHA_OPAQUE_MIN; \
    } while(0)

static void rgb565le_toPremul(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_LE, ToPremul, pixelCount);
    uint16_t* __restrict__ d = static_cast<uint16_t*>(dst);
    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        RGB565LE_TO_PREMUL_PIXEL(0, 0);
        s += 2;
        d += 4;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        RGB565LE_TO_PREMUL_PIXEL(0, 0);
        RGB565LE_TO_PREMUL_PIXEL(2, 4);
        RGB565LE_TO_PREMUL_PIXEL(4, 8);
        RGB565LE_TO_PREMUL_PIXEL(6, 12);
        s += 8;
        d += 16;
    }
}
#undef RGB565LE_TO_PREMUL_PIXEL

// fromPremul: Premul形式のsrcからRGB565_LEのdstへ変換コピー
// アルファ情報は破棄
// RGBA16_Premul→RGB565_LE 1ピクセル変換マクロ（リトルエンディアン前提）
// s: uint16_t*, d: uint16_t*, s_off: srcオフセット(uint16_t単位), d_off: dstオフセット, a_off: アルファバイトオフセット
#define RGB565LE_FROM_PREMUL_PIXEL(s_off, d_off, a_off) \
    do { \
        uint32_t inv = invUnpremulTable[reinterpret_cast<const uint8_t*>(s)[a_off]]; \
        uint8_t r = static_cast<uint8_t>((s[s_off] * inv) >> 16); \
        uint8_t g = static_cast<uint8_t>((s[s_off + 1] * inv) >> 16); \
        uint8_t b = static_cast<uint8_t>((s[s_off + 2] * inv) >> 16); \
        d[d_off] = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); \
    } while(0)

static void rgb565le_fromPremul(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_LE, FromPremul, pixelCount);
    uint16_t* __restrict__ d = static_cast<uint16_t*>(dst);
    const uint16_t* __restrict__ s = static_cast<const uint16_t*>(src);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        RGB565LE_FROM_PREMUL_PIXEL(0, 0, 7);
        s += 4;
        d += 1;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        RGB565LE_FROM_PREMUL_PIXEL(0, 0, 7);
        RGB565LE_FROM_PREMUL_PIXEL(4, 1, 15);
        RGB565LE_FROM_PREMUL_PIXEL(8, 2, 23);
        RGB565LE_FROM_PREMUL_PIXEL(12, 3, 31);
        s += 16;
        d += 4;
    }
}
#undef RGB565LE_FROM_PREMUL_PIXEL
#endif // FLEXIMG_ENABLE_PREMUL

// ========================================================================
// RGB565_BE: 16bit RGB (Big Endian)
// ========================================================================

// RGB565_BE→RGBA8_Straight 1ピクセル変換マクロ（ルックアップテーブル使用）
// s: uint8_t*, d: uint8_t*, s_off: srcオフセット, d_off: dstオフセット
// RGB565_BE: [high_byte, low_byte] in memory（LEとは逆）
#define RGB565BE_TO_STRAIGHT_PIXEL(s_off, d_off) \
    do { \
        const uint8_t* h = &rgb565HighTable[s[s_off] * 2]; \
        const uint8_t* l = &rgb565LowTable[s[s_off + 1] * 2]; \
        d[d_off]     = h[0]; \
        d[d_off + 1] = h[1] + l[0]; \
        d[d_off + 2] = l[1]; \
        d[d_off + 3] = 255; \
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

#ifdef FLEXIMG_ENABLE_PREMUL
// blendUnderPremul: srcフォーマット(RGB565_BE)からPremul形式のdstへunder合成
// 最適化: ルックアップテーブル使用
static void rgb565be_blendUnderPremul(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_BE, BlendUnder, pixelCount);
    uint16_t* __restrict__ d = static_cast<uint16_t*>(dst);
    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);

    for (int i = 0; i < pixelCount; ++i) {
        int idx16 = i * 4;
        // 8bit精度でアルファ取得（16bitの上位バイト）
        uint_fast8_t dstA8 = static_cast<uint_fast8_t>(d[idx16 + 3] >> 8);

        // dst が不透明 → スキップ
        if (dstA8 == 255) continue;

        // RGB565_BE → RGB8 変換（ルックアップテーブル使用）
        uint8_t high = s[i * 2];
        uint8_t low = s[i * 2 + 1];
        const uint8_t* h = &rgb565HighTable[high * 2];
        const uint8_t* l = &rgb565LowTable[low * 2];
        uint_fast8_t srcR8 = h[0];
        uint_fast8_t srcG8 = static_cast<uint_fast8_t>(h[1] + l[0]);
        uint_fast8_t srcB8 = l[1];

        // dst が透明 → 単純コピー（16bit形式で）
        if (dstA8 == 0) {
            d[idx16]     = static_cast<uint16_t>(srcR8 << 8);
            d[idx16 + 1] = static_cast<uint16_t>(srcG8 << 8);
            d[idx16 + 2] = static_cast<uint16_t>(srcB8 << 8);
            d[idx16 + 3] = RGBA16Premul::ALPHA_OPAQUE_MIN;
            continue;
        }

        // under合成（8bit精度）
        uint_fast16_t invDstA8 = 255 - dstA8;
        d[idx16]     = static_cast<uint16_t>(d[idx16]     + srcR8 * invDstA8);
        d[idx16 + 1] = static_cast<uint16_t>(d[idx16 + 1] + srcG8 * invDstA8);
        d[idx16 + 2] = static_cast<uint16_t>(d[idx16 + 2] + srcB8 * invDstA8);
        d[idx16 + 3] = static_cast<uint16_t>(d[idx16 + 3] + 255 * invDstA8);
    }
}

// toPremul: RGB565_BEのsrcからPremul形式のdstへ変換コピー
// アルファなし→完全不透明として変換（ルックアップテーブル使用）
// RGB565_BE→RGBA16_Premul 1ピクセル変換マクロ
// s: uint8_t*, d: uint16_t*, s_off: srcオフセット, d_off: dstオフセット(uint16_t単位)
// RGB565_BE: [high_byte, low_byte] in memory（LEとは逆）
#define RGB565BE_TO_PREMUL_PIXEL(s_off, d_off) \
    do { \
        const uint8_t* h = &rgb565HighTable[s[s_off] * 2]; \
        const uint8_t* l = &rgb565LowTable[s[s_off + 1] * 2]; \
        d[d_off]     = static_cast<uint16_t>(h[0] << 8); \
        d[d_off + 1] = static_cast<uint16_t>((h[1] + l[0]) << 8); \
        d[d_off + 2] = static_cast<uint16_t>(l[1] << 8); \
        d[d_off + 3] = RGBA16Premul::ALPHA_OPAQUE_MIN; \
    } while(0)

static void rgb565be_toPremul(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_BE, ToPremul, pixelCount);
    uint16_t* __restrict__ d = static_cast<uint16_t*>(dst);
    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        RGB565BE_TO_PREMUL_PIXEL(0, 0);
        s += 2;
        d += 4;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        RGB565BE_TO_PREMUL_PIXEL(0, 0);
        RGB565BE_TO_PREMUL_PIXEL(2, 4);
        RGB565BE_TO_PREMUL_PIXEL(4, 8);
        RGB565BE_TO_PREMUL_PIXEL(6, 12);
        s += 8;
        d += 16;
    }
}
#undef RGB565BE_TO_PREMUL_PIXEL

// fromPremul: Premul形式のsrcからRGB565_BEのdstへ変換コピー
// RGB565_BE 1ピクセル変換マクロ（リトルエンディアン前提）
// s: uint16_t*, d: uint8_t*, s_off: srcオフセット(uint16_t単位), d_off: dstオフセット, a_off: アルファバイトオフセット
#define RGB565BE_FROM_PREMUL_PIXEL(s_off, d_off, a_off) \
    do { \
        uint32_t inv = invUnpremulTable[reinterpret_cast<const uint8_t*>(s)[a_off]]; \
        uint8_t r = static_cast<uint8_t>((s[s_off] * inv) >> 16); \
        uint8_t g = static_cast<uint8_t>((s[s_off + 1] * inv) >> 16); \
        uint8_t b = static_cast<uint8_t>((s[s_off + 2] * inv) >> 16); \
        d[d_off] = (r & 0xF8) | (g >> 5); \
        d[d_off + 1] = ((g << 3) & 0xE0) | (b >> 3); \
    } while(0)

static void rgb565be_fromPremul(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_BE, FromPremul, pixelCount);
    uint8_t* __restrict__ d = static_cast<uint8_t*>(dst);
    const uint16_t* __restrict__ s = static_cast<const uint16_t*>(src);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        RGB565BE_FROM_PREMUL_PIXEL(0, 0, 7);
        s += 4;
        d += 2;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        RGB565BE_FROM_PREMUL_PIXEL(0, 0, 7);
        RGB565BE_FROM_PREMUL_PIXEL(4, 2, 15);
        RGB565BE_FROM_PREMUL_PIXEL(8, 4, 23);
        RGB565BE_FROM_PREMUL_PIXEL(12, 6, 31);
        s += 16;
        d += 8;
    }
}
#undef RGB565BE_FROM_PREMUL_PIXEL
#endif // FLEXIMG_ENABLE_PREMUL

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
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::LittleEndian,
    rgb565le_toStraight,
    rgb565le_fromStraight,
    nullptr,  // toStraightIndexed
    nullptr,  // fromStraightIndexed
#ifdef FLEXIMG_ENABLE_PREMUL
    rgb565le_toPremul,
    rgb565le_fromPremul,
    rgb565le_blendUnderPremul,
#else
    nullptr,  // toPremul
    nullptr,  // fromPremul
    nullptr,  // blendUnderPremul
#endif
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
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::BigEndian,
    rgb565be_toStraight,
    rgb565be_fromStraight,
    nullptr,  // toStraightIndexed
    nullptr,  // fromStraightIndexed
#ifdef FLEXIMG_ENABLE_PREMUL
    rgb565be_toPremul,
    rgb565be_fromPremul,
    rgb565be_blendUnderPremul,
#else
    nullptr,  // toPremul
    nullptr,  // fromPremul
    nullptr,  // blendUnderPremul
#endif
    nullptr,  // blendUnderStraight
    &RGB565_LE,  // siblingEndian
    swap16       // swapEndian
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_PIXEL_FORMAT_RGB565_IMPL_INCLUDED
#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_RGB565_H
