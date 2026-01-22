#include "pixel_format.h"
#include "../core/format_metrics.h"
#include <cstring>
#include <array>

namespace FLEXIMG_NAMESPACE {

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

// ========================================================================
// 組み込みフォーマットの変換関数
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

    for (int i = 0; i < pixelCount; i++) {
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
        uint_fast16_t a_tmp = srcA8 + 1;
        uint32_t srcRG = (s[0] + (static_cast<uint32_t>(s[1]) << 16)) * a_tmp;
        uint32_t srcBA = (s[2] + (static_cast<uint32_t>(srcA8) << 16)) * a_tmp;

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

// blendUnderStraight: srcフォーマット(RGBA8_Straight)からStraight形式(RGBA8_Straight)のdstへunder合成
// under合成: dst = dst + src * (1 - dstA)
// - dst が不透明なら何もしない（スキップ）
// - dst が透明なら単純コピー
// - dst が半透明ならunder合成（unpremultiply含む）
//
// 最適化:
// - ポインタインクリメント方式（idx計算削減）
// - 32bitメモリアクセス（透明→コピー時）
// - ESP32: 除算パイプラインを活用（RGB計算でレイテンシ隠蔽）
static void rgba8Straight_blendUnderStraight(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA8_Straight, BlendUnderStraight, pixelCount);
    uint8_t* __restrict__ d = static_cast<uint8_t*>(dst);
    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);

    // プリデクリメント: ループ先頭でインクリメントするため事前に戻す
    d -= 4;
    s -= 4;

    for (int i = 0; i < pixelCount; i++) {
        d += 4;
        s += 4;

        uint_fast16_t dstA = d[3];

        // dst が不透明 → スキップ
        if (dstA == 255) continue;

        uint_fast16_t srcA = s[3];

        // src が透明 → スキップ
        if (srcA == 0) continue;

        // dst が透明 → 32bit単位で単純コピー
        if (dstA == 0) {
            *reinterpret_cast<uint32_t*>(d) = *reinterpret_cast<const uint32_t*>(s);
            continue;
        }

        // under合成（Straight形式）
        uint_fast16_t invDstA = 255 - dstA;

        // resultA = dstA + srcA * invDstA / 255
        uint_fast16_t resultA = dstA + (srcA * invDstA + 127) / 255;

        // Premultiplied計算:
        // resultC_premul = dstC * dstA + srcC * srcA * invDstA / 255
        // ESP32: 除算パイプラインがRGB計算でレイテンシ隠蔽
        uint_fast32_t resultR_premul = d[0] * dstA + (s[0] * srcA * invDstA + 127) / 255;
        uint_fast32_t resultG_premul = d[1] * dstA + (s[1] * srcA * invDstA + 127) / 255;
        uint_fast32_t resultB_premul = d[2] * dstA + (s[2] * srcA * invDstA + 127) / 255;

        // Unpremultiply (除算)
        d[0] = static_cast<uint8_t>(resultR_premul / resultA);
        d[1] = static_cast<uint8_t>(resultG_premul / resultA);
        d[2] = static_cast<uint8_t>(resultB_premul / resultA);
        d[3] = static_cast<uint8_t>(resultA);
    }
}

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

// ========================================================================
// Alpha8: 単一アルファチャンネル ↔ RGBA8_Straight 変換
// ========================================================================

// Alpha8 → RGBA8_Straight（可視化のため全チャンネルにアルファ値を展開）
static void alpha8_toStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(Alpha8, ToStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t alpha = s[i];
        d[i*4 + 0] = alpha;  // R
        d[i*4 + 1] = alpha;  // G
        d[i*4 + 2] = alpha;  // B
        d[i*4 + 3] = alpha;  // A
    }
}

// RGBA8_Straight → Alpha8（Aチャンネルのみ抽出）
static void alpha8_fromStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(Alpha8, FromStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        d[i] = s[i*4 + 3];  // Aチャンネル抽出
    }
}

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
    for (int i = 0; i < pixelCount; i++) {
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
    for (int i = 0; i < pixelCount; i++) {
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
static void rgba16Premul_blendUnderPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA16_Premultiplied, BlendUnder, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint16_t* s = static_cast<const uint16_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;
        uint16_t dstA = d[idx + 3];

        // dst が不透明 → スキップ
        if (RGBA16Premul::isOpaque(dstA)) continue;

        uint16_t srcR = s[idx];
        uint16_t srcG = s[idx + 1];
        uint16_t srcB = s[idx + 2];
        uint16_t srcA = s[idx + 3];

        // src が透明 → スキップ
        if (RGBA16Premul::isTransparent(srcA)) continue;

        // dst が透明 → 単純コピー
        if (RGBA16Premul::isTransparent(dstA)) {
            d[idx]     = srcR;
            d[idx + 1] = srcG;
            d[idx + 2] = srcB;
            d[idx + 3] = srcA;
            continue;
        }

        // under合成: dst = dst + src * (1 - dstA)
        // invDstA = ALPHA_OPAQUE_MIN - dstA (範囲: 0-65280)
        uint16_t invDstA = RGBA16Premul::ALPHA_OPAQUE_MIN - dstA;

        // src * (1 - dstA) を計算（16bit精度を維持）
        d[idx]     = static_cast<uint16_t>(d[idx]     + ((srcR * invDstA) >> 16));
        d[idx + 1] = static_cast<uint16_t>(d[idx + 1] + ((srcG * invDstA) >> 16));
        d[idx + 2] = static_cast<uint16_t>(d[idx + 2] + ((srcB * invDstA) >> 16));
        d[idx + 3] = static_cast<uint16_t>(dstA       + ((srcA * invDstA) >> 16));
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

// ========================================================================
// RGB565_LE: 16bit RGB (Little Endian)
// ========================================================================

static void rgb565le_toStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_LE, ToStraight, pixelCount);
    const uint16_t* s = static_cast<const uint16_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        uint16_t pixel = s[i];
        uint8_t r5 = (pixel >> 11) & 0x1F;
        uint8_t g6 = (pixel >> 5) & 0x3F;
        uint8_t b5 = pixel & 0x1F;

        // ビット拡張（5bit/6bit → 8bit）
        d[i*4 + 0] = static_cast<uint8_t>((r5 << 3) + (r5 >> 2));
        d[i*4 + 1] = static_cast<uint8_t>((g6 << 2) + (g6 >> 4));
        d[i*4 + 2] = static_cast<uint8_t>((b5 << 3) + (b5 >> 2));
        d[i*4 + 3] = 255;
    }
}

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

// blendUnderPremul: srcフォーマット(RGB565_LE)からPremul形式のdstへunder合成
// RGB565はアルファなし→常に不透明として扱う
static void rgb565le_blendUnderPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_LE, BlendUnder, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint16_t* s = static_cast<const uint16_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        int idx16 = i * 4;
        uint16_t dstA = d[idx16 + 3];

        // dst が不透明 → スキップ
        if (RGBA16Premul::isOpaque(dstA)) continue;

        // RGB565 → RGB8 変換
        uint16_t pixel = s[i];
        uint8_t r5 = (pixel >> 11) & 0x1F;
        uint8_t g6 = (pixel >> 5) & 0x3F;
        uint8_t b5 = pixel & 0x1F;
        uint16_t r8 = static_cast<uint16_t>((r5 << 3) + (r5 >> 2));
        uint16_t g8 = static_cast<uint16_t>((g6 << 2) + (g6 >> 4));
        uint16_t b8 = static_cast<uint16_t>((b5 << 3) + (b5 >> 2));

        // RGB8 → RGBA16_Premultiplied 変換（A=255、完全不透明）
        // A_tmp = 256, A16 = 65280
        uint16_t srcR = static_cast<uint16_t>(r8 << 8);
        uint16_t srcG = static_cast<uint16_t>(g8 << 8);
        uint16_t srcB = static_cast<uint16_t>(b8 << 8);
        constexpr uint16_t srcA = RGBA16Premul::ALPHA_OPAQUE_MIN;

        // dst が透明 → 単純コピー
        if (RGBA16Premul::isTransparent(dstA)) {
            d[idx16]     = srcR;
            d[idx16 + 1] = srcG;
            d[idx16 + 2] = srcB;
            d[idx16 + 3] = srcA;
            continue;
        }

        // under合成: dst = dst + src * (1 - dstA)
        uint16_t invDstA = RGBA16Premul::ALPHA_OPAQUE_MIN - dstA;
        d[idx16]     = static_cast<uint16_t>(d[idx16]     + ((srcR * invDstA) >> 16));
        d[idx16 + 1] = static_cast<uint16_t>(d[idx16 + 1] + ((srcG * invDstA) >> 16));
        d[idx16 + 2] = static_cast<uint16_t>(d[idx16 + 2] + ((srcB * invDstA) >> 16));
        d[idx16 + 3] = static_cast<uint16_t>(dstA         + ((srcA * invDstA) >> 16));
    }
}

// toPremul: RGB565_LEのsrcからPremul形式のdstへ変換コピー
// アルファなし→完全不透明として変換
// RGB565_LE→RGBA16_Premul 1ピクセル変換マクロ
// s: uint16_t*, d: uint16_t*, s_off: srcオフセット, d_off: dstオフセット(uint16_t単位)
#define RGB565LE_TO_PREMUL_PIXEL(s_off, d_off) \
    do { \
        uint16_t pixel = s[s_off]; \
        uint8_t r5 = (pixel >> 11) & 0x1F; \
        uint8_t g6 = (pixel >> 5) & 0x3F; \
        uint8_t b5 = pixel & 0x1F; \
        d[d_off]     = static_cast<uint16_t>(((r5 << 3) + (r5 >> 2)) << 8); \
        d[d_off + 1] = static_cast<uint16_t>(((g6 << 2) + (g6 >> 4)) << 8); \
        d[d_off + 2] = static_cast<uint16_t>(((b5 << 3) + (b5 >> 2)) << 8); \
        d[d_off + 3] = RGBA16Premul::ALPHA_OPAQUE_MIN; \
    } while(0)

static void rgb565le_toPremul(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_LE, ToPremul, pixelCount);
    uint16_t* __restrict__ d = static_cast<uint16_t*>(dst);
    const uint16_t* __restrict__ s = static_cast<const uint16_t*>(src);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        RGB565LE_TO_PREMUL_PIXEL(0, 0);
        s += 1;
        d += 4;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        RGB565LE_TO_PREMUL_PIXEL(0, 0);
        RGB565LE_TO_PREMUL_PIXEL(1, 4);
        RGB565LE_TO_PREMUL_PIXEL(2, 8);
        RGB565LE_TO_PREMUL_PIXEL(3, 12);
        s += 4;
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

// ========================================================================
// RGB565_BE: 16bit RGB (Big Endian)
// ========================================================================

static void rgb565be_toStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_BE, ToStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        // ビッグエンディアン: 上位バイトが先
        uint16_t pixel = static_cast<uint16_t>((static_cast<uint16_t>(s[i*2]) << 8) | s[i*2 + 1]);
        uint8_t r5 = (pixel >> 11) & 0x1F;
        uint8_t g6 = (pixel >> 5) & 0x3F;
        uint8_t b5 = pixel & 0x1F;

        d[i*4 + 0] = static_cast<uint8_t>((r5 << 3) + (r5 >> 2));
        d[i*4 + 1] = static_cast<uint8_t>((g6 << 2) + (g6 >> 4));
        d[i*4 + 2] = static_cast<uint8_t>((b5 << 3) + (b5 >> 2));
        d[i*4 + 3] = 255;
    }
}

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

// blendUnderPremul: srcフォーマット(RGB565_BE)からPremul形式のdstへunder合成
static void rgb565be_blendUnderPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_BE, BlendUnder, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        int idx16 = i * 4;
        uint16_t dstA = d[idx16 + 3];

        // dst が不透明 → スキップ
        if (RGBA16Premul::isOpaque(dstA)) continue;

        // RGB565_BE → RGB8 変換
        uint16_t pixel = static_cast<uint16_t>((static_cast<uint16_t>(s[i*2]) << 8) | s[i*2 + 1]);
        uint8_t r5 = (pixel >> 11) & 0x1F;
        uint8_t g6 = (pixel >> 5) & 0x3F;
        uint8_t b5 = pixel & 0x1F;
        uint16_t r8 = static_cast<uint16_t>((r5 << 3) + (r5 >> 2));
        uint16_t g8 = static_cast<uint16_t>((g6 << 2) + (g6 >> 4));
        uint16_t b8 = static_cast<uint16_t>((b5 << 3) + (b5 >> 2));

        // RGB8 → RGBA16_Premultiplied 変換（A=255）
        uint16_t srcR = static_cast<uint16_t>(r8 << 8);
        uint16_t srcG = static_cast<uint16_t>(g8 << 8);
        uint16_t srcB = static_cast<uint16_t>(b8 << 8);
        constexpr uint16_t srcA = RGBA16Premul::ALPHA_OPAQUE_MIN;

        // dst が透明 → 単純コピー
        if (RGBA16Premul::isTransparent(dstA)) {
            d[idx16]     = srcR;
            d[idx16 + 1] = srcG;
            d[idx16 + 2] = srcB;
            d[idx16 + 3] = srcA;
            continue;
        }

        // under合成
        uint16_t invDstA = RGBA16Premul::ALPHA_OPAQUE_MIN - dstA;
        d[idx16]     = static_cast<uint16_t>(d[idx16]     + ((srcR * invDstA) >> 16));
        d[idx16 + 1] = static_cast<uint16_t>(d[idx16 + 1] + ((srcG * invDstA) >> 16));
        d[idx16 + 2] = static_cast<uint16_t>(d[idx16 + 2] + ((srcB * invDstA) >> 16));
        d[idx16 + 3] = static_cast<uint16_t>(dstA         + ((srcA * invDstA) >> 16));
    }
}

// toPremul: RGB565_BEのsrcからPremul形式のdstへ変換コピー
// RGB565_BE→RGBA16_Premul 1ピクセル変換マクロ
// s: uint8_t*, d: uint16_t*, s_off: srcオフセット, d_off: dstオフセット(uint16_t単位)
#define RGB565BE_TO_PREMUL_PIXEL(s_off, d_off) \
    do { \
        uint16_t pixel = static_cast<uint16_t>((static_cast<uint16_t>(s[s_off]) << 8) | s[s_off + 1]); \
        uint8_t r5 = (pixel >> 11) & 0x1F; \
        uint8_t g6 = (pixel >> 5) & 0x3F; \
        uint8_t b5 = pixel & 0x1F; \
        d[d_off]     = static_cast<uint16_t>(((r5 << 3) + (r5 >> 2)) << 8); \
        d[d_off + 1] = static_cast<uint16_t>(((g6 << 2) + (g6 >> 4)) << 8); \
        d[d_off + 2] = static_cast<uint16_t>(((b5 << 3) + (b5 >> 2)) << 8); \
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

// ========================================================================
// RGB332: 8bit RGB (3-3-2)
// ========================================================================

static void rgb332_toStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB332, ToStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t pixel = s[i];
        uint8_t r3 = (pixel >> 5) & 0x07;
        uint8_t g3 = (pixel >> 2) & 0x07;
        uint8_t b2 = pixel & 0x03;

        // 乗算＋少量シフト（マイコン最適化）
        d[i*4 + 0] = (r3 * 0x49) >> 1;  // 3bit → 8bit
        d[i*4 + 1] = (g3 * 0x49) >> 1;  // 3bit → 8bit
        d[i*4 + 2] = b2 * 0x55;          // 2bit → 8bit
        d[i*4 + 3] = 255;
    }
}

static void rgb332_fromStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB332, FromStraight, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t r = s[i*4 + 0];
        uint8_t g = s[i*4 + 1];
        uint8_t b = s[i*4 + 2];
        d[i] = static_cast<uint8_t>((r & 0xE0) | ((g >> 5) << 2) | (b >> 6));
    }
}

// blendUnderPremul: srcフォーマット(RGB332)からPremul形式のdstへunder合成
static void rgb332_blendUnderPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB332, BlendUnder, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        int idx16 = i * 4;
        uint16_t dstA = d[idx16 + 3];

        // dst が不透明 → スキップ
        if (RGBA16Premul::isOpaque(dstA)) continue;

        // RGB332 → RGB8 変換
        uint8_t pixel = s[i];
        uint8_t r3 = (pixel >> 5) & 0x07;
        uint8_t g3 = (pixel >> 2) & 0x07;
        uint8_t b2 = pixel & 0x03;
        uint16_t r8 = static_cast<uint16_t>((r3 * 0x49) >> 1);
        uint16_t g8 = static_cast<uint16_t>((g3 * 0x49) >> 1);
        uint16_t b8 = static_cast<uint16_t>(b2 * 0x55);

        // RGB8 → RGBA16_Premultiplied 変換（A=255）
        uint16_t srcR = static_cast<uint16_t>(r8 << 8);
        uint16_t srcG = static_cast<uint16_t>(g8 << 8);
        uint16_t srcB = static_cast<uint16_t>(b8 << 8);
        constexpr uint16_t srcA = RGBA16Premul::ALPHA_OPAQUE_MIN;

        // dst が透明 → 単純コピー
        if (RGBA16Premul::isTransparent(dstA)) {
            d[idx16]     = srcR;
            d[idx16 + 1] = srcG;
            d[idx16 + 2] = srcB;
            d[idx16 + 3] = srcA;
            continue;
        }

        // under合成
        uint16_t invDstA = RGBA16Premul::ALPHA_OPAQUE_MIN - dstA;
        d[idx16]     = static_cast<uint16_t>(d[idx16]     + ((srcR * invDstA) >> 16));
        d[idx16 + 1] = static_cast<uint16_t>(d[idx16 + 1] + ((srcG * invDstA) >> 16));
        d[idx16 + 2] = static_cast<uint16_t>(d[idx16 + 2] + ((srcB * invDstA) >> 16));
        d[idx16 + 3] = static_cast<uint16_t>(dstA         + ((srcA * invDstA) >> 16));
    }
}

// toPremul: RGB332のsrcからPremul形式のdstへ変換コピー
static void rgb332_toPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB332, ToPremul, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        uint8_t pixel = s[i];
        uint8_t r3 = (pixel >> 5) & 0x07;
        uint8_t g3 = (pixel >> 2) & 0x07;
        uint8_t b2 = pixel & 0x03;

        uint16_t r8 = static_cast<uint16_t>((r3 * 0x49) >> 1);
        uint16_t g8 = static_cast<uint16_t>((g3 * 0x49) >> 1);
        uint16_t b8 = static_cast<uint16_t>(b2 * 0x55);

        int idx = i * 4;
        d[idx]     = static_cast<uint16_t>(r8 << 8);
        d[idx + 1] = static_cast<uint16_t>(g8 << 8);
        d[idx + 2] = static_cast<uint16_t>(b8 << 8);
        d[idx + 3] = RGBA16Premul::ALPHA_OPAQUE_MIN;
    }
}

// fromPremul: Premul形式のsrcからRGB332のdstへ変換コピー
static void rgb332_fromPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB332, FromPremul, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint16_t* s = static_cast<const uint16_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
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

// ========================================================================
// RGB888: 24bit RGB (mem[0]=R, mem[1]=G, mem[2]=B)
// ========================================================================

static void rgb888_toStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB888, ToStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        d[i*4 + 0] = s[i*3 + 0];  // R
        d[i*4 + 1] = s[i*3 + 1];  // G
        d[i*4 + 2] = s[i*3 + 2];  // B
        d[i*4 + 3] = 255;          // A
    }
}

static void rgb888_fromStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB888, FromStraight, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        d[i*3 + 0] = s[i*4 + 0];  // R
        d[i*3 + 1] = s[i*4 + 1];  // G
        d[i*3 + 2] = s[i*4 + 2];  // B
    }
}

// blendUnderPremul: srcフォーマット(RGB888)からPremul形式のdstへunder合成
static void rgb888_blendUnderPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB888, BlendUnder, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        int idx16 = i * 4;
        uint16_t dstA = d[idx16 + 3];

        // dst が不透明 → スキップ
        if (RGBA16Premul::isOpaque(dstA)) continue;

        // RGB888 → RGBA16_Premultiplied 変換（A=255）
        uint16_t srcR = static_cast<uint16_t>(s[i*3 + 0] << 8);
        uint16_t srcG = static_cast<uint16_t>(s[i*3 + 1] << 8);
        uint16_t srcB = static_cast<uint16_t>(s[i*3 + 2] << 8);
        constexpr uint16_t srcA = RGBA16Premul::ALPHA_OPAQUE_MIN;

        // dst が透明 → 単純コピー
        if (RGBA16Premul::isTransparent(dstA)) {
            d[idx16]     = srcR;
            d[idx16 + 1] = srcG;
            d[idx16 + 2] = srcB;
            d[idx16 + 3] = srcA;
            continue;
        }

        // under合成
        uint16_t invDstA = RGBA16Premul::ALPHA_OPAQUE_MIN - dstA;
        d[idx16]     = static_cast<uint16_t>(d[idx16]     + ((srcR * invDstA) >> 16));
        d[idx16 + 1] = static_cast<uint16_t>(d[idx16 + 1] + ((srcG * invDstA) >> 16));
        d[idx16 + 2] = static_cast<uint16_t>(d[idx16 + 2] + ((srcB * invDstA) >> 16));
        d[idx16 + 3] = static_cast<uint16_t>(dstA         + ((srcA * invDstA) >> 16));
    }
}

// toPremul: RGB888のsrcからPremul形式のdstへ変換コピー
static void rgb888_toPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB888, ToPremul, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;
        d[idx]     = static_cast<uint16_t>(s[i*3 + 0] << 8);
        d[idx + 1] = static_cast<uint16_t>(s[i*3 + 1] << 8);
        d[idx + 2] = static_cast<uint16_t>(s[i*3 + 2] << 8);
        d[idx + 3] = RGBA16Premul::ALPHA_OPAQUE_MIN;
    }
}

// fromPremul: Premul形式のsrcからRGB888のdstへ変換コピー
static void rgb888_fromPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB888, FromPremul, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint16_t* s = static_cast<const uint16_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;
        uint16_t r16 = s[idx];
        uint16_t g16 = s[idx + 1];
        uint16_t b16 = s[idx + 2];
        uint16_t a16 = s[idx + 3];

        uint8_t a8 = a16 >> 8;
        uint16_t a_tmp = a8 + 1;

        d[i*3 + 0] = static_cast<uint8_t>(r16 / a_tmp);
        d[i*3 + 1] = static_cast<uint8_t>(g16 / a_tmp);
        d[i*3 + 2] = static_cast<uint8_t>(b16 / a_tmp);
    }
}

// ========================================================================
// BGR888: 24bit BGR (mem[0]=B, mem[1]=G, mem[2]=R)
// ========================================================================

static void bgr888_toStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(BGR888, ToStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        d[i*4 + 0] = s[i*3 + 2];  // R (src の B 位置)
        d[i*4 + 1] = s[i*3 + 1];  // G
        d[i*4 + 2] = s[i*3 + 0];  // B (src の R 位置)
        d[i*4 + 3] = 255;
    }
}

static void bgr888_fromStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(BGR888, FromStraight, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        d[i*3 + 0] = s[i*4 + 2];  // B
        d[i*3 + 1] = s[i*4 + 1];  // G
        d[i*3 + 2] = s[i*4 + 0];  // R
    }
}

// blendUnderPremul: srcフォーマット(BGR888)からPremul形式のdstへunder合成
static void bgr888_blendUnderPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(BGR888, BlendUnder, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        int idx16 = i * 4;
        uint16_t dstA = d[idx16 + 3];

        // dst が不透明 → スキップ
        if (RGBA16Premul::isOpaque(dstA)) continue;

        // BGR888 → RGBA16_Premultiplied 変換（A=255）
        uint16_t srcR = static_cast<uint16_t>(s[i*3 + 2] << 8);  // R (src の B 位置)
        uint16_t srcG = static_cast<uint16_t>(s[i*3 + 1] << 8);  // G
        uint16_t srcB = static_cast<uint16_t>(s[i*3 + 0] << 8);  // B (src の R 位置)
        constexpr uint16_t srcA = RGBA16Premul::ALPHA_OPAQUE_MIN;

        // dst が透明 → 単純コピー
        if (RGBA16Premul::isTransparent(dstA)) {
            d[idx16]     = srcR;
            d[idx16 + 1] = srcG;
            d[idx16 + 2] = srcB;
            d[idx16 + 3] = srcA;
            continue;
        }

        // under合成
        uint16_t invDstA = RGBA16Premul::ALPHA_OPAQUE_MIN - dstA;
        d[idx16]     = static_cast<uint16_t>(d[idx16]     + ((srcR * invDstA) >> 16));
        d[idx16 + 1] = static_cast<uint16_t>(d[idx16 + 1] + ((srcG * invDstA) >> 16));
        d[idx16 + 2] = static_cast<uint16_t>(d[idx16 + 2] + ((srcB * invDstA) >> 16));
        d[idx16 + 3] = static_cast<uint16_t>(dstA         + ((srcA * invDstA) >> 16));
    }
}

// toPremul: BGR888のsrcからPremul形式のdstへ変換コピー
static void bgr888_toPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(BGR888, ToPremul, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;
        d[idx]     = static_cast<uint16_t>(s[i*3 + 2] << 8);  // R
        d[idx + 1] = static_cast<uint16_t>(s[i*3 + 1] << 8);  // G
        d[idx + 2] = static_cast<uint16_t>(s[i*3 + 0] << 8);  // B
        d[idx + 3] = RGBA16Premul::ALPHA_OPAQUE_MIN;
    }
}

// fromPremul: Premul形式のsrcからBGR888のdstへ変換コピー
static void bgr888_fromPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(BGR888, FromPremul, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint16_t* s = static_cast<const uint16_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;
        uint16_t r16 = s[idx];
        uint16_t g16 = s[idx + 1];
        uint16_t b16 = s[idx + 2];
        uint16_t a16 = s[idx + 3];

        uint8_t a8 = a16 >> 8;
        uint16_t a_tmp = a8 + 1;

        d[i*3 + 0] = static_cast<uint8_t>(b16 / a_tmp);  // B
        d[i*3 + 1] = static_cast<uint8_t>(g16 / a_tmp);  // G
        d[i*3 + 2] = static_cast<uint8_t>(r16 / a_tmp);  // R
    }
}

// ========================================================================
// エンディアン・バイトスワップ関数
// ========================================================================

// 16bit用バイトスワップ（RGB565_LE ↔ RGB565_BE）
static void swap16(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    const uint16_t* srcPtr = static_cast<const uint16_t*>(src);
    uint16_t* dstPtr = static_cast<uint16_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        uint16_t v = srcPtr[i];
        dstPtr[i] = static_cast<uint16_t>((v >> 8) | (v << 8));
    }
}

// 24bit用チャンネルスワップ（RGB888 ↔ BGR888）
static void swap24(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    const uint8_t* srcPtr = static_cast<const uint8_t*>(src);
    uint8_t* dstPtr = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 3;
        dstPtr[idx + 0] = srcPtr[idx + 2];
        dstPtr[idx + 1] = srcPtr[idx + 1];
        dstPtr[idx + 2] = srcPtr[idx + 0];
    }
}

// ========================================================================
// 組み込みフォーマット定義
// ========================================================================

namespace BuiltinFormats {

// Forward declarations for sibling references
extern const PixelFormatDescriptor RGB565_BE;
extern const PixelFormatDescriptor BGR888;

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
    rgba8Straight_toPremul,
    rgba8Straight_fromPremul,
    rgba8Straight_blendUnderPremul,
    rgba8Straight_blendUnderStraight,  // blendUnderStraight
    nullptr,  // siblingEndian
    nullptr   // swapEndian
};

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
    rgb565le_toPremul,
    rgb565le_fromPremul,
    rgb565le_blendUnderPremul,
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
    rgb565be_toPremul,
    rgb565be_fromPremul,
    rgb565be_blendUnderPremul,
    nullptr,  // blendUnderStraight
    &RGB565_LE,  // siblingEndian
    swap16       // swapEndian
};

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
    rgb332_toPremul,
    rgb332_fromPremul,
    rgb332_blendUnderPremul,
    nullptr,  // blendUnderStraight
    nullptr,  // siblingEndian
    nullptr   // swapEndian
};

const PixelFormatDescriptor RGB888 = {
    "RGB888",
    24,  // bitsPerPixel
    1,   // pixelsPerUnit
    3,   // bytesPerUnit
    3,   // channelCount
    { ChannelDescriptor(ChannelType::Red, 8, 16),
      ChannelDescriptor(ChannelType::Green, 8, 8),
      ChannelDescriptor(ChannelType::Blue, 8, 0),
      ChannelDescriptor() },  // R, G, B, (no A)
    false,  // hasAlpha
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    rgb888_toStraight,
    rgb888_fromStraight,
    nullptr,  // toStraightIndexed
    nullptr,  // fromStraightIndexed
    rgb888_toPremul,
    rgb888_fromPremul,
    rgb888_blendUnderPremul,
    nullptr,  // blendUnderStraight
    &BGR888,  // siblingEndian
    swap24    // swapEndian
};

const PixelFormatDescriptor BGR888 = {
    "BGR888",
    24,  // bitsPerPixel
    1,   // pixelsPerUnit
    3,   // bytesPerUnit
    3,   // channelCount
    { ChannelDescriptor(ChannelType::Blue, 8, 0),
      ChannelDescriptor(ChannelType::Green, 8, 8),
      ChannelDescriptor(ChannelType::Red, 8, 16),
      ChannelDescriptor() },  // B, G, R (メモリ順序)
    false,  // hasAlpha
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    bgr888_toStraight,
    bgr888_fromStraight,
    nullptr,  // toStraightIndexed
    nullptr,  // fromStraightIndexed
    bgr888_toPremul,
    bgr888_fromPremul,
    bgr888_blendUnderPremul,
    nullptr,  // blendUnderStraight
    &RGB888,  // siblingEndian
    swap24    // swapEndian
};

const PixelFormatDescriptor Alpha8 = {
    "Alpha8",
    8,   // bitsPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    1,   // channelCount
    { ChannelDescriptor(ChannelType::Alpha, 8, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },  // Alpha only
    true,   // hasAlpha
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    alpha8_toStraight,
    alpha8_fromStraight,
    nullptr,  // toStraightIndexed
    nullptr,  // fromStraightIndexed
    nullptr,  // toPremul
    nullptr,  // fromPremul
    nullptr,  // blendUnderPremul
    nullptr,  // blendUnderStraight
    nullptr,  // siblingEndian
    nullptr   // swapEndian
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE
