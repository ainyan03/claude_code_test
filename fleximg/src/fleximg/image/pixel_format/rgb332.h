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
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint32_t* d = static_cast<uint32_t*>(dst);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        *d++ = rgb332ToRgba8[*s++];
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        d[0] = rgb332ToRgba8[s[0]];
        d[1] = rgb332ToRgba8[s[1]];
        d[2] = rgb332ToRgba8[s[2]];
        d[3] = rgb332ToRgba8[s[3]];
        s += 4;
        d += 4;
    }
}

// RGBA8 → RGB332 変換マクロ（32bitロードした値から変換）
// (((r << 3) + g) << 2) + b の形式でESP32のシフト+加算命令を活用
#define RGBA8_TO_RGB332(rgba) \
    (((((rgba) >> 5) << 3) + (((rgba) >> 13) & 0x07)) << 2) + (((rgba) >> 22) & 0x03)

static void rgb332_fromStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB332, FromStraight, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint32_t* s = static_cast<const uint32_t*>(src);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        auto rgba = *s++;
        *d++ = static_cast<uint8_t>(RGBA8_TO_RGB332(rgba));
    }

    // 4ピクセル単位でループ（ロードを先に発行してレイテンシ隠蔽）
    pixelCount >>= 2;
    while (pixelCount--) {
        // 4つのロードを先に発行
        auto rgba0 = s[0];
        auto rgba1 = s[1];
        auto rgba2 = s[2];
        auto rgba3 = s[3];

        // 演算と8bitストア
        d[0] = static_cast<uint8_t>(RGBA8_TO_RGB332(rgba0));
        d[1] = static_cast<uint8_t>(RGBA8_TO_RGB332(rgba1));
        d[2] = static_cast<uint8_t>(RGBA8_TO_RGB332(rgba2));
        d[3] = static_cast<uint8_t>(RGBA8_TO_RGB332(rgba3));

        s += 4;
        d += 4;
    }
}
#undef RGBA8_TO_RGB332

#ifdef FLEXIMG_ENABLE_PREMUL
// blendUnderPremul: srcフォーマット(RGB332)からPremul形式のdstへunder合成
// 最適化: ルックアップテーブル + SWAR (SIMD Within A Register)
static void rgb332_blendUnderPremul(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB332, BlendUnder, pixelCount);
    uint16_t* __restrict__ d = static_cast<uint16_t*>(dst);
    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);

    // プリデクリメント方式（RGBA8_Straightと同様のパターン）
    s -= 1;
    d -= 4;

    for (int i = 0; i < pixelCount; ++i) {
        d += 4;
        s += 1;

        // dstアルファを8bit精度で取得（リトルエンディアン前提で上位バイト直接読み取り）
        uint_fast8_t dstA8 = reinterpret_cast<uint8_t*>(d)[7];

        // dst が不透明 → スキップ
        if (dstA8 == 255) continue;

        // RGB332 → RGBA8 変換（ルックアップテーブル使用、32bitロード）
        uint32_t rgba = rgb332ToRgba8[*s];
        uint_fast8_t srcR8 = static_cast<uint_fast8_t>(rgba);
        uint_fast8_t srcG8 = static_cast<uint_fast8_t>(rgba >> 8);
        uint_fast8_t srcB8 = static_cast<uint_fast8_t>(rgba >> 16);

        // dst が透明 → 単純コピー（16bit形式で、alphaは不透明扱い）
        if (dstA8 == 0) {
            d[0] = static_cast<uint16_t>(srcR8 << 8);
            d[1] = static_cast<uint16_t>(srcG8 << 8);
            d[2] = static_cast<uint16_t>(srcB8 << 8);
            d[3] = RGBA16Premul::ALPHA_OPAQUE_MIN;
            continue;
        }

        // under合成（SWAR最適化: RGとBAを32bitにパックして同時演算）
        uint_fast16_t invDstA8 = 255 - dstA8;

        // RGチャンネル: srcRG * invDstA8 を計算して加算
        uint32_t srcRG = srcR8 + (static_cast<uint32_t>(srcG8) << 16);
        uint32_t dstRG = d[0] + (static_cast<uint32_t>(d[1]) << 16);
        uint32_t blendRG = dstRG + srcRG * invDstA8;
        d[0] = static_cast<uint16_t>(blendRG);
        d[1] = static_cast<uint16_t>(blendRG >> 16);

        // BAチャンネル: srcBA * invDstA8 を計算して加算（RGB332は不透明なのでsrcA=255）
        uint32_t srcBA = srcB8 + (static_cast<uint32_t>(255) << 16);
        uint32_t dstBA = d[2] + (static_cast<uint32_t>(d[3]) << 16);
        uint32_t blendBA = dstBA + srcBA * invDstA8;
        d[2] = static_cast<uint16_t>(blendBA);
        d[3] = static_cast<uint16_t>(blendBA >> 16);
    }
}

// toPremul: RGB332のsrcからPremul形式のdstへ変換コピー
static void rgb332_toPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB332, ToPremul, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);

    for (int i = 0; i < pixelCount; ++i) {
        // RGB332 → RGBA8 変換（ルックアップテーブル使用、32bitロード）
        uint32_t rgba = rgb332ToRgba8[s[i]];

        int idx = i * 4;
        d[idx]     = static_cast<uint16_t>((rgba & 0xFF) << 8);
        d[idx + 1] = static_cast<uint16_t>(((rgba >> 8) & 0xFF) << 8);
        d[idx + 2] = static_cast<uint16_t>(((rgba >> 16) & 0xFF) << 8);
        d[idx + 3] = RGBA16Premul::ALPHA_OPAQUE_MIN;
    }
}

// fromPremul: Premul形式のsrcからRGB332のdstへ変換コピー
static void rgb332_fromPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB332, FromPremul, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint16_t* s = static_cast<const uint16_t*>(src);

    for (int i = 0; i < pixelCount; ++i) {
        int idx = i * 4;
        uint16_t r16 = s[idx];
        uint16_t g16 = s[idx + 1];
        uint16_t b16 = s[idx + 2];
        uint16_t a16 = s[idx + 3];

        uint8_t a8 = a16 >> 8;
        uint16_t a_tmp = a8 + 1;
        uint8_t r = static_cast<uint8_t>(r16 / a_tmp);
        uint8_t g = static_cast<uint8_t>(g16 / a_tmp);
        uint8_t b = static_cast<uint8_t>(b16 / a_tmp);

        d[i] = static_cast<uint8_t>((r & 0xE0) | ((g >> 5) << 2) | (b >> 6));
    }
}
#endif // FLEXIMG_ENABLE_PREMUL

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
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    rgb332_toStraight,
    rgb332_fromStraight,
    nullptr,  // toStraightIndexed
    nullptr,  // fromStraightIndexed
#ifdef FLEXIMG_ENABLE_PREMUL
    rgb332_toPremul,
    rgb332_fromPremul,
    rgb332_blendUnderPremul,
#else
    nullptr,  // toPremul
    nullptr,  // fromPremul
    nullptr,  // blendUnderPremul
#endif
    nullptr,  // blendUnderStraight
    nullptr,  // siblingEndian
    nullptr   // swapEndian
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_RGB332_H
