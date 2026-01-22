#include "pixel_format.h"
#include "../core/format_metrics.h"
#include <cstring>
#include <array>

namespace FLEXIMG_NAMESPACE {

#ifdef FLEXIMG_ENABLE_PREMUL
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
        uint_fast16_t resultA = dstA + (static_cast<uint_fast32_t>(srcA) * invDstA + 127) / 255;

        // Premultiplied計算:
        // resultC_premul = dstC * dstA + srcC * srcA * invDstA / 255
        // ESP32: 除算パイプラインがRGB計算でレイテンシ隠蔽
        // 注意: 明示的キャストで32bit演算を保証（16bit環境でのオーバーフロー防止）
        uint_fast32_t resultR_premul = d[0] * dstA + (static_cast<uint_fast32_t>(s[0]) * srcA * invDstA + 127) / 255;
        uint_fast32_t resultG_premul = d[1] * dstA + (static_cast<uint_fast32_t>(s[1]) * srcA * invDstA + 127) / 255;
        uint_fast32_t resultB_premul = d[2] * dstA + (static_cast<uint_fast32_t>(s[2]) * srcA * invDstA + 127) / 255;

        // Unpremultiply (除算)
        d[0] = static_cast<uint8_t>(resultR_premul / resultA);
        d[1] = static_cast<uint8_t>(resultG_premul / resultA);
        d[2] = static_cast<uint8_t>(resultB_premul / resultA);
        d[3] = static_cast<uint8_t>(resultA);
    }
}

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

#ifdef FLEXIMG_ENABLE_PREMUL
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
//
// 8bit精度方式: 他のblendUnderPremul関数と一貫性を保つため、
// ブレンド計算は8bit精度で行う（蓄積は16bitで精度を維持）
// TODO: SWAR最適化（RG/BAを32bitにパックして同時演算）
static void rgba16Premul_blendUnderPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA16_Premultiplied, BlendUnder, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint16_t* s = static_cast<const uint16_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
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
#endif // FLEXIMG_ENABLE_PREMUL

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

    for (int i = 0; i < pixelCount; i++) {
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

    for (int i = 0; i < pixelCount; i++) {
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
// RGB332: 8bit RGB (3-3-2)
// ========================================================================

// RGB332 → RGB8 変換ルックアップテーブル
// RGB332の256通りの値に対して、RGB8値を事前計算
// 各エントリ: [R8, G8, B8] の3バイト（1024バイト未満でキャッシュフレンドリー）
namespace {

// テーブル生成用マクロ（constexpr配列生成の代替）
#define RGB332_ENTRY(p) \
    static_cast<uint8_t>((((p) >> 5) & 0x07) * 0x49 >> 1), \
    static_cast<uint8_t>((((p) >> 2) & 0x07) * 0x49 >> 1), \
    static_cast<uint8_t>(((p) & 0x03) * 0x55)

#define RGB332_ROW(base) \
    RGB332_ENTRY(base+0), RGB332_ENTRY(base+1), RGB332_ENTRY(base+2), RGB332_ENTRY(base+3), \
    RGB332_ENTRY(base+4), RGB332_ENTRY(base+5), RGB332_ENTRY(base+6), RGB332_ENTRY(base+7), \
    RGB332_ENTRY(base+8), RGB332_ENTRY(base+9), RGB332_ENTRY(base+10), RGB332_ENTRY(base+11), \
    RGB332_ENTRY(base+12), RGB332_ENTRY(base+13), RGB332_ENTRY(base+14), RGB332_ENTRY(base+15)

// RGB332 → RGB8 変換テーブル (256 × 3 = 768 bytes)
alignas(64) static const uint8_t rgb332ToRgb8[256 * 3] = {
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
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        // RGB332 → RGB8 変換（ルックアップテーブル使用）
        const uint8_t* rgb = &rgb332ToRgb8[s[i] * 3];
        d[i*4 + 0] = rgb[0];
        d[i*4 + 1] = rgb[1];
        d[i*4 + 2] = rgb[2];
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

    for (int i = 0; i < pixelCount; i++) {
        d += 4;
        s += 1;

        // dstアルファを8bit精度で取得（リトルエンディアン前提で上位バイト直接読み取り）
        uint_fast8_t dstA8 = reinterpret_cast<uint8_t*>(d)[7];

        // dst が不透明 → スキップ
        if (dstA8 == 255) continue;

        // RGB332 → RGB8 変換（ルックアップテーブル使用）
        uint8_t pixel = *s;
        const uint8_t* rgb = &rgb332ToRgb8[pixel * 3];
        uint_fast8_t srcR8 = rgb[0];
        uint_fast8_t srcG8 = rgb[1];
        uint_fast8_t srcB8 = rgb[2];

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

    for (int i = 0; i < pixelCount; i++) {
        // RGB332 → RGB8 変換（ルックアップテーブル使用）
        const uint8_t* rgb = &rgb332ToRgb8[s[i] * 3];

        int idx = i * 4;
        d[idx]     = static_cast<uint16_t>(rgb[0] << 8);
        d[idx + 1] = static_cast<uint16_t>(rgb[1] << 8);
        d[idx + 2] = static_cast<uint16_t>(rgb[2] << 8);
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
#endif // FLEXIMG_ENABLE_PREMUL

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

#ifdef FLEXIMG_ENABLE_PREMUL
// blendUnderPremul: srcフォーマット(RGB888)からPremul形式のdstへunder合成
// 8bit精度方式（rgba8Straight_blendUnderPremulと同様のアプローチ）
static void rgb888_blendUnderPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB888, BlendUnder, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        int idx16 = i * 4;
        // 8bit精度でアルファ取得（16bitの上位バイト）
        uint_fast8_t dstA8 = static_cast<uint_fast8_t>(d[idx16 + 3] >> 8);

        // dst が不透明 → スキップ
        if (dstA8 == 255) continue;

        // src の RGB を取得（8bit）
        uint_fast8_t srcR8 = s[i*3 + 0];
        uint_fast8_t srcG8 = s[i*3 + 1];
        uint_fast8_t srcB8 = s[i*3 + 2];

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
        // src(8bit) * invDstA(8bit) = 16bit、そのまま加算
        d[idx16]     = static_cast<uint16_t>(d[idx16]     + srcR8 * invDstA8);
        d[idx16 + 1] = static_cast<uint16_t>(d[idx16 + 1] + srcG8 * invDstA8);
        d[idx16 + 2] = static_cast<uint16_t>(d[idx16 + 2] + srcB8 * invDstA8);
        d[idx16 + 3] = static_cast<uint16_t>(d[idx16 + 3] + 255 * invDstA8);
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
#endif // FLEXIMG_ENABLE_PREMUL

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

#ifdef FLEXIMG_ENABLE_PREMUL
// blendUnderPremul: srcフォーマット(BGR888)からPremul形式のdstへunder合成
// 8bit精度方式（rgba8Straight_blendUnderPremulと同様のアプローチ）
static void bgr888_blendUnderPremul(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(BGR888, BlendUnder, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        int idx16 = i * 4;
        // 8bit精度でアルファ取得（16bitの上位バイト）
        uint_fast8_t dstA8 = static_cast<uint_fast8_t>(d[idx16 + 3] >> 8);

        // dst が不透明 → スキップ
        if (dstA8 == 255) continue;

        // BGR888 の RGB を取得（8bit）
        uint_fast8_t srcR8 = s[i*3 + 2];  // R (src の B 位置)
        uint_fast8_t srcG8 = s[i*3 + 1];  // G
        uint_fast8_t srcB8 = s[i*3 + 0];  // B (src の R 位置)

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
        // src(8bit) * invDstA(8bit) = 16bit、そのまま加算
        d[idx16]     = static_cast<uint16_t>(d[idx16]     + srcR8 * invDstA8);
        d[idx16 + 1] = static_cast<uint16_t>(d[idx16 + 1] + srcG8 * invDstA8);
        d[idx16 + 2] = static_cast<uint16_t>(d[idx16 + 2] + srcB8 * invDstA8);
        d[idx16 + 3] = static_cast<uint16_t>(d[idx16 + 3] + 255 * invDstA8);
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
#endif // FLEXIMG_ENABLE_PREMUL

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

#ifdef FLEXIMG_ENABLE_PREMUL
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
#endif // FLEXIMG_ENABLE_PREMUL

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
#ifdef FLEXIMG_ENABLE_PREMUL
    rgb888_toPremul,
    rgb888_fromPremul,
    rgb888_blendUnderPremul,
#else
    nullptr,  // toPremul
    nullptr,  // fromPremul
    nullptr,  // blendUnderPremul
#endif
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
#ifdef FLEXIMG_ENABLE_PREMUL
    bgr888_toPremul,
    bgr888_fromPremul,
    bgr888_blendUnderPremul,
#else
    nullptr,  // toPremul
    nullptr,  // fromPremul
    nullptr,  // blendUnderPremul
#endif
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
