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
// - 255倍を (x << 8) - x で計算（乗算削減）
// - 255スケールで計算し、途中の除算を削減


// gotoラベル方式のディスパッチ版
// - シンプルな分岐で分岐予測しやすい
// - 連続不透明/透明領域を4ピクセル単位で高速スキップ
// - 組み込み環境（ESP32等）向けに最適化
static void rgba8Straight_blendUnderStraight(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA8_Straight, BlendUnder, pixelCount);
    if (pixelCount <= 0) return;

    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);
    uint_fast8_t srcA = s[3];
    uint8_t* __restrict__ d = static_cast<uint8_t*>(dst);
    uint_fast8_t dstA = d[3];

    // メインディスパッチ: srcAを優先的にチェック
    if (srcA == 0) goto handle_srcA_0;
    if (dstA == 255) goto handle_dstA_255;
    if (dstA == 0) goto handle_dstA_0;
blend:
    // ========================================================================
    // ブレンド処理ループ（srcA != 0, dstA != 0, dstA != 255）
    // ========================================================================
    while (--pixelCount >= 0) {
        dstA = d[3];
        srcA = s[3];
        if (dstA == 255) break;
        if (dstA == 0) break;
        if (srcA == 0) break;
        // under合成（Straight形式、255スケール最適化版）
        uint_fast32_t dstA_255 = (static_cast<uint_fast32_t>(dstA) << 8) - dstA;
        uint_fast16_t invDstA = 255 - dstA;
        uint_fast32_t srcA_invDstA = srcA * invDstA;
        uint_fast32_t resultA_255 = dstA_255 + srcA_invDstA;
        uint_fast16_t A8 = static_cast<uint_fast16_t>((resultA_255 + 127) / 255);
        uint_fast32_t half = resultA_255 >> 1;
        auto R8 = (d[0] * dstA_255 + s[0] * srcA_invDstA + half) / resultA_255;
        auto G8 = (d[1] * dstA_255 + s[1] * srcA_invDstA + half) / resultA_255;
        auto B8 = (d[2] * dstA_255 + s[2] * srcA_invDstA + half) / resultA_255;
        d[0] = static_cast<uint8_t>(R8);
        d[1] = static_cast<uint8_t>(G8);
        d[2] = static_cast<uint8_t>(B8);
        d[3] = static_cast<uint8_t>(A8);

        d += 4;
        s += 4;
    }
    if (pixelCount <= 0) return;
    if (srcA == 0) goto handle_srcA_0;
    if (dstA == 0) goto handle_dstA_0;
    // ここには通常到達しない

    // ========================================================================
    // dst不透明(255) → 連続スキップ
    // ========================================================================
handle_dstA_255:
    {
        // 現在のピクセルは255確認済み、スキップ
        if (--pixelCount <= 0) return;
        d += 4;
        dstA = d[3];
        s += 4;

        // 4ピクセル単位でスキップ
        auto plimit = pixelCount >> 2;
        if (plimit && dstA == 255) {
            auto d_start = d;
            do {
                uint_fast8_t a1 = d[7];
                uint_fast8_t a2 = d[11];
                uint_fast8_t a3 = d[15];
                if ((dstA & a1 & a2 & a3) != 255) break;
                d += 16;
                dstA = d[3];
            } while (--plimit);
            auto pindex = (d - d_start);
            if (d != d_start) {
                pixelCount -= pindex >> 2;
                if (pixelCount <= 0) return;
                s += pindex;
            }
        }
        srcA = s[3];
        if (dstA == 255) goto handle_dstA_255;
        if (dstA == 0) goto handle_dstA_0;
        if (srcA == 0) goto handle_srcA_0;
        goto blend;
    }

    // ========================================================================
    // dst透明(0) → 連続コピー
    // ========================================================================
handle_dstA_0:
    {
        // 現在のピクセルは0確認済み、コピー
        *reinterpret_cast<uint32_t*>(d) = *reinterpret_cast<const uint32_t*>(s);
        if (--pixelCount <= 0) return;
        d += 4;
        dstA = d[3];
        s += 4;

        // 4ピクセル単位でコピー
        auto plimit = pixelCount >> 2;
        if (plimit && dstA == 0) {
            auto s_start = s;
            do {
                uint_fast8_t a1 = d[7];
                uint_fast8_t a2 = d[11];
                uint_fast8_t a3 = d[15];
                if ((dstA | a1 | a2 | a3) != 0) break;
                reinterpret_cast<uint32_t*>(d)[0] = reinterpret_cast<const uint32_t*>(s)[0];
                reinterpret_cast<uint32_t*>(d)[1] = reinterpret_cast<const uint32_t*>(s)[1];
                reinterpret_cast<uint32_t*>(d)[2] = reinterpret_cast<const uint32_t*>(s)[2];
                reinterpret_cast<uint32_t*>(d)[3] = reinterpret_cast<const uint32_t*>(s)[3];
                d += 16;
                dstA = d[3];
                s += 16;
            } while (--plimit);
            auto pindex = (s - s_start);
            if (pindex) {
                pixelCount -= pindex >> 2;
                if (pixelCount <= 0) return;
            }
        }

        srcA = s[3];
        if (dstA == 255) goto handle_dstA_255;
        if (dstA == 0) goto handle_dstA_0;
        if (srcA == 0) goto handle_srcA_0;
        goto blend;
    }

    // ========================================================================
    // src透明(0) → 連続スキップ
    // ========================================================================
handle_srcA_0:
    {
        // 現在のピクセルは0確認済み、スキップ
        if (--pixelCount <= 0) return;
        s += 4;
        srcA = s[3];
        d += 4;

        // 4ピクセル単位でスキップ
        auto plimit = pixelCount >> 2;
        if (plimit && srcA == 0) {
            auto s_start = s;
            do {
                uint_fast8_t a1 = s[7];
                uint_fast8_t a2 = s[11];
                uint_fast8_t a3 = s[15];
                if ((srcA | a1 | a2 | a3) != 0) break;
                s += 16;
                srcA = s[3];
            } while (--plimit);
            auto pindex = (s - s_start);
            if (pindex) {
                pixelCount -= pindex >> 2;
                if (pixelCount <= 0) return;
                d += pindex;
            }
        }
        dstA = d[3];
        if (srcA == 0) goto handle_srcA_0;
        if (dstA == 255) goto handle_dstA_255;
        if (dstA == 0) goto handle_dstA_0;
        goto blend;
    }
}

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
