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

#ifdef FLEXIMG_ENABLE_PREMUL
#include <array>
// ========================================================================
// 逆数テーブル（fromPremul除算回避用）
// ========================================================================
// invUnpremulTable[a8] = ceil(65536 / (a8 + 1))  (a8 = 0..255)
// 使用: (r16 * invUnpremulTable[a8]) >> 16 ≈ r16 / (a8 + 1)
//
// ceil（切り上げ）を使用する理由:
// - floor（切り捨て）だと結果が常に小さくなる方向に誤差が出る（96.5%で-1誤差）
// - ceilを使うと誤差が相殺され、100%のケースで誤差0を達成できる
// - オーバーフローは発生しない（検証済み）
//
namespace {

// 逆数計算（ceil版、a=0 の場合は 0 を返す安全策）
constexpr uint16_t calcInvUnpremul(int a) {
    // ceil(65536 / (a+1)) = (65536 + a) / (a+1)
    return (a == 0) ? 0 : static_cast<uint16_t>((65536u + static_cast<uint32_t>(a)) / static_cast<uint32_t>(a + 1));
}

// constexpr テーブル生成関数
constexpr std::array<uint16_t, 256> makeInvUnpremulTable() {
    std::array<uint16_t, 256> table{};
    for (int i = 0; i < 256; ++i) {
        table[static_cast<size_t>(i)] = calcInvUnpremul(i);
    }
    return table;
}

alignas(64) constexpr std::array<uint16_t, 256> invUnpremulTable = makeInvUnpremulTable();

} // namespace
#endif // FLEXIMG_ENABLE_PREMUL

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

#ifdef FLEXIMG_ENABLE_PREMUL
// RGBA8_Straight: Premul形式のブレンド・変換関数

// blendUnderPremul: srcフォーマット(RGBA8_Straight)からPremul形式のdstへunder合成
// RGBA8_Straight → RGBA16_Premultiplied変換しながらunder合成
//
// 最適化手法:
// - SWAR (SIMD Within A Register): RG/BAを32bitにパックして同時演算
// - プリデクリメント方式: continue時のポインタ進行を保証
// - 8bit精度のアルファ処理: 16bit→8bit変換でシフト演算を削減
static void rgba8Straight_blendUnderPremul(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA8_Straight, BlendUnder, pixelCount);
    uint16_t* __restrict__ d = static_cast<uint16_t*>(dst);
    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);

    // プリデクリメント: ループ先頭でインクリメントするため事前に戻す
    s -= 4;
    d -= 4;

    for (int i = 0; i < pixelCount; ++i) {
        d += 4;
        // dstアルファを8bit精度で取得（上位バイト直接読み取り、リトルエンディアン前提）
        uint_fast8_t dstA8 = reinterpret_cast<uint8_t*>(d)[7];
        s += 4;

        // dst が不透明 → スキップ
        if (dstA8 == 255) continue;

        uint_fast8_t srcA8 = s[3];

        // src が透明 → スキップ
        if (srcA8 == 0) continue;

        // RGBA8_Straight → RGBA16_Premultiplied 変換
        // SWAR: RGとBAを32bitにパックして乗算を2回に削減
        // a_tmp = srcA8 + 1 (範囲: 1-256) で8bit→16bit変換
        // 色: c16 = c8 * a_tmp
        // α:  a16 = 255 * a_tmp （rgba8Straight_toPremulと整合）
        uint_fast16_t a_tmp = srcA8 + 1;
        uint32_t srcRG = (s[0] + (static_cast<uint32_t>(s[1]) << 16)) * a_tmp;
        uint32_t srcBA = (s[2] + (static_cast<uint32_t>(255) << 16)) * a_tmp;

        // dst が半透明 → under合成
        if (dstA8 != 0) {
            // under合成: dst = dst + src * (1 - dstA)
            // SWAR: 8bit精度に変換後、32bitで2チャンネル同時演算
            uint_fast16_t invDstA8 = 255 - dstA8;
            srcRG = reinterpret_cast<uint32_t*>(d)[0] + ((srcRG >> 8) & 0x00FF00FF) * invDstA8;
            srcBA = reinterpret_cast<uint32_t*>(d)[1] + ((srcBA >> 8) & 0x00FF00FF) * invDstA8;
        }

        // 32bit単位で書き込み（2チャンネル同時）
        reinterpret_cast<uint32_t*>(d)[0] = srcRG;
        reinterpret_cast<uint32_t*>(d)[1] = srcBA;
    }
}
#endif // FLEXIMG_ENABLE_PREMUL

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
    FLEXIMG_FMT_METRICS(RGBA8_Straight, BlendUnderStraight, pixelCount);
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

#ifdef FLEXIMG_ENABLE_PREMUL
// fromPremul: Premul形式(RGBA16_Premultiplied)のsrcからRGBA8_Straightのdstへ変換コピー
// RGBA16_Premul→RGBA8_Straight 1ピクセル変換マクロ（リトルエンディアン前提）
// s: uint16_t*, d: uint8_t*, s_off: srcオフセット(uint16_t単位), d_off: dstオフセット, a_off: アルファバイトオフセット
#define RGBA8_FROM_PREMUL_PIXEL(s_off, d_off, a_off) \
    do { \
        uint8_t a8 = reinterpret_cast<const uint8_t*>(s)[a_off]; \
        uint32_t inv = invUnpremulTable[a8]; \
        d[d_off]     = static_cast<uint8_t>((s[s_off] * inv) >> 16); \
        d[d_off + 1] = static_cast<uint8_t>((s[s_off + 1] * inv) >> 16); \
        d[d_off + 2] = static_cast<uint8_t>((s[s_off + 2] * inv) >> 16); \
        d[d_off + 3] = a8; \
    } while(0)

static void rgba8Straight_fromPremul(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA8_Straight, FromPremul, pixelCount);
    uint8_t* __restrict__ d = static_cast<uint8_t*>(dst);
    const uint16_t* __restrict__ s = static_cast<const uint16_t*>(src);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        RGBA8_FROM_PREMUL_PIXEL(0, 0, 7);
        s += 4;
        d += 4;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        RGBA8_FROM_PREMUL_PIXEL(0, 0, 7);
        RGBA8_FROM_PREMUL_PIXEL(4, 4, 15);
        RGBA8_FROM_PREMUL_PIXEL(8, 8, 23);
        RGBA8_FROM_PREMUL_PIXEL(12, 12, 31);
        s += 16;
        d += 16;
    }
}
#undef RGBA8_FROM_PREMUL_PIXEL

// toPremul: RGBA8_StraightのsrcからPremul形式(RGBA16_Premultiplied)のdstへ変換コピー
//
// 最適化手法:
// - SWAR (SIMD Within A Register): RG/BAを32bitにパックして同時演算
// - 4ピクセルアンローリング
//
// アルファチャンネルの変換:
// - RGB: c16 = c8 * (a8 + 1)
// - A:   a16 = 255 * (a8 + 1)  ← 255を使用（rgba16Premul_fromStraightと整合）
// これにより fromPremul での復元時に a8 = a16 >> 8 = 255 * (a8+1) / 256 ≈ a8 となる
//
// SWAR版1ピクセル変換マクロ
#define RGBA8_TO_PREMUL_SWAR(s_off, d_off) \
    do { \
        uint_fast16_t a_tmp = s[s_off + 3] + 1; \
        d32[d_off]     = (s[s_off] + (static_cast<uint32_t>(s[s_off + 1]) << 16)) * a_tmp; \
        d32[d_off + 1] = (s[s_off + 2] + (static_cast<uint32_t>(255) << 16)) * a_tmp; \
    } while(0)

static void rgba8Straight_toPremul(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA8_Straight, ToPremul, pixelCount);
    uint32_t* __restrict__ d32 = static_cast<uint32_t*>(dst);
    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        RGBA8_TO_PREMUL_SWAR(0, 0);
        s += 4;
        d32 += 2;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        RGBA8_TO_PREMUL_SWAR(0, 0);
        RGBA8_TO_PREMUL_SWAR(4, 2);
        RGBA8_TO_PREMUL_SWAR(8, 4);
        RGBA8_TO_PREMUL_SWAR(12, 6);
        s += 16;
        d32 += 8;
    }
}
#undef RGBA8_TO_PREMUL_SWAR
#endif // FLEXIMG_ENABLE_PREMUL

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
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    rgba8Straight_toStraight,
    rgba8Straight_fromStraight,
    nullptr,  // toStraightIndexed
    nullptr,  // fromStraightIndexed
#ifdef FLEXIMG_ENABLE_PREMUL
    rgba8Straight_toPremul,
    rgba8Straight_fromPremul,
    rgba8Straight_blendUnderPremul,
#else
    nullptr,  // toPremul
    nullptr,  // fromPremul
    nullptr,  // blendUnderPremul
#endif
    rgba8Straight_blendUnderStraight,  // blendUnderStraight
    nullptr,  // siblingEndian
    nullptr   // swapEndian
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_RGBA8_STRAIGHT_H
