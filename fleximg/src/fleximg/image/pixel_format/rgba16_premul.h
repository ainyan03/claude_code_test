#ifndef FLEXIMG_PIXEL_FORMAT_RGBA16_PREMUL_H
#define FLEXIMG_PIXEL_FORMAT_RGBA16_PREMUL_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

#ifdef FLEXIMG_ENABLE_PREMUL

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
    extern const PixelFormatDescriptor RGBA16_Premultiplied;
}

namespace PixelFormatIDs {
    inline const PixelFormatID RGBA16_Premultiplied = &BuiltinFormats::RGBA16_Premultiplied;
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_ENABLE_PREMUL

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION
#ifndef FLEXIMG_PIXEL_FORMAT_RGBA16_PREMUL_IMPL_INCLUDED
#define FLEXIMG_PIXEL_FORMAT_RGBA16_PREMUL_IMPL_INCLUDED

#ifdef FLEXIMG_ENABLE_PREMUL

#include "../../core/format_metrics.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// RGBA16_Premultiplied: 16bit Premultiplied ↔ 8bit Straight 変換
// ========================================================================
// 変換方式: A_tmp = A8 + 1 を使用
// - Forward変換: 除算ゼロ（乗算のみ）
// - Reverse変換: 除数が1-256に限定（テーブル化やSIMD最適化が容易）
// - A8=0 でもRGB情報を保持

static void rgba16Premul_toStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA16_Premultiplied, ToStraight, pixelCount);
    const uint16_t* s = static_cast<const uint16_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; ++i) {
        int idx = i * 4;
        uint16_t r16 = s[idx];
        uint16_t g16 = s[idx + 1];
        uint16_t b16 = s[idx + 2];
        uint16_t a16 = s[idx + 3];

        // A8 = A16 >> 8 (範囲: 0-255)
        // A_tmp = A8 + 1 (範囲: 1-256) - ゼロ除算回避
        uint8_t a8 = a16 >> 8;
        uint16_t a_tmp = a8 + 1;

        // Unpremultiply: RGB / A_tmp（除数が1-256に限定）
        d[idx]     = static_cast<uint8_t>(r16 / a_tmp);
        d[idx + 1] = static_cast<uint8_t>(g16 / a_tmp);
        d[idx + 2] = static_cast<uint8_t>(b16 / a_tmp);
        d[idx + 3] = a8;
    }
}

static void rgba16Premul_fromStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA16_Premultiplied, FromStraight, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; ++i) {
        int idx = i * 4;
        uint16_t r8 = s[idx];
        uint16_t g8 = s[idx + 1];
        uint16_t b8 = s[idx + 2];
        uint16_t a8 = s[idx + 3];

        // A_tmp = A8 + 1 (範囲: 1-256)
        uint16_t a_tmp = a8 + 1;

        // Premultiply: RGB * A_tmp（除算なし）
        // A16 = 255 * A_tmp (範囲: 255-65280)
        d[idx]     = static_cast<uint16_t>(r8 * a_tmp);
        d[idx + 1] = static_cast<uint16_t>(g8 * a_tmp);
        d[idx + 2] = static_cast<uint16_t>(b8 * a_tmp);
        d[idx + 3] = static_cast<uint16_t>(255 * a_tmp);
    }
}

// ========================================================================
// RGBA16_Premultiplied: Premul形式のブレンド・変換関数
// ========================================================================

// blendUnderPremul: srcフォーマット(RGBA16_Premultiplied)からPremul形式のdstへunder合成
// under合成: dst = dst + src * (1 - dstA)
// - dst が不透明なら何もしない（スキップ）
// - dst が透明なら単純コピー
// - dst が半透明ならunder合成
//
// 8bit精度方式: 他のblendUnderPremul関数と一貫性を保つため、
// ブレンド計算は8bit精度で行う（蓄積は16bitで精度を維持）
// TODO: SWAR最適化（RG/BAを32bitにパックして同時演算）
static void rgba16Premul_blendUnderPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA16_Premultiplied, BlendUnder, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint16_t* s = static_cast<const uint16_t*>(src);

    for (int i = 0; i < pixelCount; ++i) {
        int idx = i * 4;
        // 8bit精度でアルファ取得（16bitの上位バイト）
        uint_fast8_t dstA8 = static_cast<uint_fast8_t>(d[idx + 3] >> 8);

        // dst が不透明 → スキップ
        if (dstA8 == 255) continue;

        uint16_t srcR = s[idx];
        uint16_t srcG = s[idx + 1];
        uint16_t srcB = s[idx + 2];
        uint16_t srcA = s[idx + 3];

        // src が透明 → スキップ
        uint_fast8_t srcA8 = static_cast<uint_fast8_t>(srcA >> 8);
        if (srcA8 == 0) continue;

        // dst が透明 → 単純コピー
        if (dstA8 == 0) {
            d[idx]     = srcR;
            d[idx + 1] = srcG;
            d[idx + 2] = srcB;
            d[idx + 3] = srcA;
            continue;
        }

        // under合成（8bit精度）
        // src16を8bitに変換してからブレンド計算、結果は16bitに蓄積
        uint_fast16_t invDstA8 = 255 - dstA8;
        uint_fast8_t srcR8 = static_cast<uint_fast8_t>(srcR >> 8);
        uint_fast8_t srcG8 = static_cast<uint_fast8_t>(srcG >> 8);
        uint_fast8_t srcB8 = static_cast<uint_fast8_t>(srcB >> 8);

        d[idx]     = static_cast<uint16_t>(d[idx]     + srcR8 * invDstA8);
        d[idx + 1] = static_cast<uint16_t>(d[idx + 1] + srcG8 * invDstA8);
        d[idx + 2] = static_cast<uint16_t>(d[idx + 2] + srcB8 * invDstA8);
        d[idx + 3] = static_cast<uint16_t>(d[idx + 3] + srcA8 * invDstA8);
    }
}

// fromPremul: Premul形式(RGBA16_Premultiplied)のsrcからRGBA16_Premultipliedのdstへ変換コピー
// 同一フォーマットなので単純コピー
static void rgba16Premul_fromPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA16_Premultiplied, FromPremul, pixelCount);
    std::memcpy(dst, src, static_cast<size_t>(pixelCount) * 8);
}

// toPremul: RGBA16_Premultiplied形式のsrcからPremul形式のdstへ変換コピー
// 同一フォーマットなので単純コピー
static void rgba16Premul_toPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA16_Premultiplied, ToPremul, pixelCount);
    std::memcpy(dst, src, static_cast<size_t>(pixelCount) * 8);
}

// ------------------------------------------------------------------------
// フォーマット定義
// ------------------------------------------------------------------------

namespace BuiltinFormats {

const PixelFormatDescriptor RGBA16_Premultiplied = {
    "RGBA16_Premultiplied",
    64,  // bitsPerPixel
    1,   // pixelsPerUnit
    8,   // bytesPerUnit
    4,   // channelCount
    { ChannelDescriptor(ChannelType::Red, 16, 0),
      ChannelDescriptor(ChannelType::Green, 16, 0),
      ChannelDescriptor(ChannelType::Blue, 16, 0),
      ChannelDescriptor(ChannelType::Alpha, 16, 0) },  // R, G, B, A
    true,   // hasAlpha
    true,   // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    rgba16Premul_toStraight,
    rgba16Premul_fromStraight,
    nullptr,  // toStraightIndexed
    nullptr,  // fromStraightIndexed
    rgba16Premul_toPremul,
    rgba16Premul_fromPremul,
    rgba16Premul_blendUnderPremul,
    nullptr,  // blendUnderStraight
    nullptr,  // siblingEndian
    nullptr   // swapEndian
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_ENABLE_PREMUL

#endif // FLEXIMG_PIXEL_FORMAT_RGBA16_PREMUL_IMPL_INCLUDED
#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_RGBA16_PREMUL_H
