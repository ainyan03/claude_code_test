#ifndef FLEXIMG_PIXEL_FORMAT_RGBA8_STRAIGHT_H
#define FLEXIMG_PIXEL_FORMAT_RGBA8_STRAIGHT_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
    extern const PixelFormatDescriptor RGBA8_Straight;
}

namespace PixelFormatIDs {
    inline const PixelFormatID RGBA8_Straight = &BuiltinFormats::RGBA8_Straight;
}

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

#include "../../core/format_metrics.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// RGBA8_Straight 変換関数
// 標準フォーマット: RGBA8_Straight（8bit RGBA、ストレートアルファ）
// ========================================================================

// RGBA8_Straight: Straight形式なのでコピー
static void rgba8Straight_toStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA8_Straight, ToStraight, pixelCount);
    std::memcpy(dst, src, static_cast<size_t>(pixelCount) * 4);
}

static void rgba8Straight_fromStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA8_Straight, FromStraight, pixelCount);
    std::memcpy(dst, src, static_cast<size_t>(pixelCount) * 4);
}

// blendUnderStraight: srcフォーマット(RGBA8_Straight)からStraight形式(RGBA8_Straight)のdstへunder合成
// under合成: dst = dst + src * (1 - dstA)
// - dst が不透明なら何もしない（スキップ）
// - dst が透明なら単純コピー
// - dst が半透明ならunder合成（unpremultiply含む）
//
// 最適化:
// - 4ピクセル単位の一括判定（連続不透明/透明領域の高速スキップ）
// - 32bitメモリアクセス（透明→コピー時）
// - ESP32: 除算パイプラインを活用（RGB計算でレイテンシ隠蔽）

// 1ピクセルのunder合成処理（インライン展開用マクロ）
#define BLEND_UNDER_STRAIGHT_1PX(d_ptr, s_ptr) \
    do { \
        uint_fast16_t dstA = (d_ptr)[3]; \
        if (dstA == 255) break; /* dst不透明→スキップ */ \
        uint_fast16_t srcA = (s_ptr)[3]; \
        if (srcA == 0) break; /* src透明→スキップ */ \
        if (dstA == 0) { \
            *reinterpret_cast<uint32_t*>(d_ptr) = *reinterpret_cast<const uint32_t*>(s_ptr); \
            break; /* dst透明→コピー */ \
        } \
        /* under合成（Straight形式） */ \
        uint_fast32_t resultR_premul = (d_ptr)[0]; \
        uint_fast32_t resultG_premul = (d_ptr)[1]; \
        uint_fast32_t resultB_premul = (d_ptr)[2]; \
        resultR_premul *= dstA; \
        resultG_premul *= dstA; \
        resultB_premul *= dstA; \
        uint_fast32_t srcR = (s_ptr)[0]; \
        uint_fast32_t srcG = (s_ptr)[1]; \
        uint_fast32_t srcB = (s_ptr)[2]; \
        uint_fast16_t invDstA = 255 - dstA; \
        srcA *= invDstA; \
        srcR = (srcR * srcA + 127) / 255; \
        srcG = (srcG * srcA + 127) / 255; \
        srcB = (srcB * srcA + 127) / 255; \
        uint_fast16_t resultA = static_cast<uint_fast16_t>(dstA + (static_cast<uint_fast32_t>(srcA) + 127) / 255); \
        (d_ptr)[3] = static_cast<uint8_t>(resultA); \
        resultR_premul = resultR_premul + srcR; \
        resultG_premul = resultG_premul + srcG; \
        resultB_premul = resultB_premul + srcB; \
        resultR_premul /= resultA; \
        resultG_premul /= resultA; \
        resultB_premul /= resultA; \
        (d_ptr)[0] = static_cast<uint8_t>(resultR_premul); \
        (d_ptr)[1] = static_cast<uint8_t>(resultG_premul); \
        (d_ptr)[2] = static_cast<uint8_t>(resultB_premul); \
    } while(0)

static void rgba8Straight_blendUnderStraight(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA8_Straight, BlendUnder, pixelCount);
    uint8_t* __restrict__ d = static_cast<uint8_t*>(dst);
    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        BLEND_UNDER_STRAIGHT_1PX(d, s);
        d += 4;
        s += 4;
    }

    // 4ピクセル単位でループ
    int count4 = pixelCount >> 2;
    while (count4--) {
        // 4ピクセルのdstアルファを取得
        uint_fast8_t dstA0 = d[3];
        uint_fast8_t dstA1 = d[7];
        uint_fast8_t dstA2 = d[11];
        uint_fast8_t dstA3 = d[15];

        // 全て不透明(255) → 4ピクセル一括スキップ
        if ((dstA0 & dstA1 & dstA2 & dstA3) == 255) {
            d += 16;
            s += 16;
            continue;
        }

        // 4ピクセルのsrcアルファを取得
        uint_fast8_t srcA0 = s[3];
        uint_fast8_t srcA1 = s[7];
        uint_fast8_t srcA2 = s[11];
        uint_fast8_t srcA3 = s[15];

        // 全て透明(0) → 4ピクセル一括スキップ
        if ((srcA0 | srcA1 | srcA2 | srcA3) == 0) {
            d += 16;
            s += 16;
            continue;
        }

        // dst全て透明(0) → 4ピクセル一括コピー
        if ((dstA0 | dstA1 | dstA2 | dstA3) == 0) {
            reinterpret_cast<uint32_t*>(d)[0] = reinterpret_cast<const uint32_t*>(s)[0];
            reinterpret_cast<uint32_t*>(d)[1] = reinterpret_cast<const uint32_t*>(s)[1];
            reinterpret_cast<uint32_t*>(d)[2] = reinterpret_cast<const uint32_t*>(s)[2];
            reinterpret_cast<uint32_t*>(d)[3] = reinterpret_cast<const uint32_t*>(s)[3];
            d += 16;
            s += 16;
            continue;
        }

        // 個別処理
        BLEND_UNDER_STRAIGHT_1PX(d, s);
        BLEND_UNDER_STRAIGHT_1PX(d + 4, s + 4);
        BLEND_UNDER_STRAIGHT_1PX(d + 8, s + 8);
        BLEND_UNDER_STRAIGHT_1PX(d + 12, s + 12);

        d += 16;
        s += 16;
    }
}
#undef BLEND_UNDER_STRAIGHT_1PX

// ------------------------------------------------------------------------
// フォーマット定義
// ------------------------------------------------------------------------

namespace BuiltinFormats {

const PixelFormatDescriptor RGBA8_Straight = {
    "RGBA8_Straight",
    32,  // bitsPerPixel
    1,   // pixelsPerUnit
    4,   // bytesPerUnit
    4,   // channelCount
    { ChannelDescriptor(ChannelType::Red, 8, 0),
      ChannelDescriptor(ChannelType::Green, 8, 0),
      ChannelDescriptor(ChannelType::Blue, 8, 0),
      ChannelDescriptor(ChannelType::Alpha, 8, 0) },  // R, G, B, A
    true,   // hasAlpha
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    rgba8Straight_toStraight,
    rgba8Straight_fromStraight,
    nullptr,  // expandIndex
    rgba8Straight_blendUnderStraight,  // blendUnderStraight
    nullptr,  // siblingEndian
    nullptr   // swapEndian
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_RGBA8_STRAIGHT_H
