#include "pixel_format.h"
#include "../core/format_metrics.h"
#include <cstring>
#include <algorithm>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマットの変換関数
// 標準フォーマット: RGBA8_Straight（8bit RGBA、ストレートアルファ）
// ========================================================================

// RGBA8_Straight: 標準フォーマットなのでコピー
static void rgba8Straight_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(RGBA8_Straight, ToStandard, pixelCount);
    std::memcpy(dst, src, static_cast<size_t>(pixelCount) * 4);
}

static void rgba8Straight_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(RGBA8_Straight, FromStandard, pixelCount);
    std::memcpy(dst, src, static_cast<size_t>(pixelCount) * 4);
}

#if 1  // RGBA16_Premultiplied サポート有効
// RGBA8_Straight: Premul形式のブレンド・変換関数

// blendUnderPremul: srcフォーマット(RGBA8_Straight)からPremul形式のdstへunder合成
// RGBA8_Straight → RGBA16_Premultiplied変換しながらunder合成
static void rgba8Straight_blendUnderPremul(void* dst, const void* src, int pixelCount, const BlendParams* /*params*/) {
    FLEXIMG_FMT_METRICS(RGBA8_Straight, BlendUnder, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        int idx16 = i * 4;
        int idx8 = i * 4;
        uint16_t dstA = d[idx16 + 3];

        // dst が不透明 → スキップ
        if (RGBA16Premul::isOpaque(dstA)) continue;

        uint8_t srcA8 = s[idx8 + 3];

        // src が透明 → スキップ
        if (srcA8 == 0) continue;

        // RGBA8_Straight → RGBA16_Premultiplied 変換
        // A_tmp = A8 + 1 (範囲: 1-256)
        uint16_t a_tmp = srcA8 + 1;
        uint16_t srcR = s[idx8 + 0] * a_tmp;
        uint16_t srcG = s[idx8 + 1] * a_tmp;
        uint16_t srcB = s[idx8 + 2] * a_tmp;
        uint16_t srcA = 255 * a_tmp;

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

// fromPremul: Premul形式(RGBA16_Premultiplied)のsrcからRGBA8_Straightのdstへ変換コピー
static void rgba8Straight_fromPremul(void* dst, const void* src, int pixelCount) {
    FLEXIMG_FMT_METRICS(RGBA8_Straight, FromPremul, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint16_t* s = static_cast<const uint16_t*>(src);

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

        // Unpremultiply: RGB / A_tmp
        d[idx]     = static_cast<uint8_t>(r16 / a_tmp);
        d[idx + 1] = static_cast<uint8_t>(g16 / a_tmp);
        d[idx + 2] = static_cast<uint8_t>(b16 / a_tmp);
        d[idx + 3] = a8;
    }
}
#endif

// ========================================================================
// Alpha8: 単一アルファチャンネル ↔ RGBA8_Straight 変換
// ========================================================================

// Alpha8 → RGBA8_Straight（可視化のため全チャンネルにアルファ値を展開）
static void alpha8_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(Alpha8, ToStandard, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t alpha = s[i];
        dst[i*4 + 0] = alpha;  // R
        dst[i*4 + 1] = alpha;  // G
        dst[i*4 + 2] = alpha;  // B
        dst[i*4 + 3] = alpha;  // A
    }
}

// RGBA8_Straight → Alpha8（Aチャンネルのみ抽出）
static void alpha8_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(Alpha8, FromStandard, pixelCount);
    const uint8_t* s = src;
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        d[i] = s[i*4 + 3];  // Aチャンネル抽出
    }
}

#if 1  // RGBA16_Premultiplied サポート有効
// ========================================================================
// RGBA16_Premultiplied: 16bit Premultiplied ↔ 8bit Straight 変換
// ========================================================================
// 変換方式: A_tmp = A8 + 1 を使用
// - Forward変換: 除算ゼロ（乗算のみ）
// - Reverse変換: 除数が1-256に限定（テーブル化やSIMD最適化が容易）
// - A8=0 でもRGB情報を保持

static void rgba16Premul_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(RGBA16_Premultiplied, ToStandard, pixelCount);
    const uint16_t* s = static_cast<const uint16_t*>(src);
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
        dst[idx]     = static_cast<uint8_t>(r16 / a_tmp);
        dst[idx + 1] = static_cast<uint8_t>(g16 / a_tmp);
        dst[idx + 2] = static_cast<uint8_t>(b16 / a_tmp);
        dst[idx + 3] = a8;
    }
}

static void rgba16Premul_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(RGBA16_Premultiplied, FromStandard, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;
        uint16_t r8 = src[idx];
        uint16_t g8 = src[idx + 1];
        uint16_t b8 = src[idx + 2];
        uint16_t a8 = src[idx + 3];

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
// 直接変換関数（最適化用）
// ========================================================================

namespace DirectConvertFuncs {

void rgba16PremulToRgba8Straight(const void* src, void* dst, int pixelCount) {
    const uint16_t* s = static_cast<const uint16_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;
        uint16_t r16 = s[idx];
        uint16_t g16 = s[idx + 1];
        uint16_t b16 = s[idx + 2];
        uint16_t a16 = s[idx + 3];

        uint8_t a8 = a16 >> 8;
        uint16_t a_tmp = a8 + 1;

        d[idx]     = static_cast<uint8_t>(r16 / a_tmp);
        d[idx + 1] = static_cast<uint8_t>(g16 / a_tmp);
        d[idx + 2] = static_cast<uint8_t>(b16 / a_tmp);
        d[idx + 3] = a8;
    }
}

void rgba8StraightToRgba16Premul(const void* src, void* dst, int pixelCount) {
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint16_t* d = static_cast<uint16_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;
        uint16_t r8 = s[idx];
        uint16_t g8 = s[idx + 1];
        uint16_t b8 = s[idx + 2];
        uint16_t a8 = s[idx + 3];

        uint16_t a_tmp = a8 + 1;

        d[idx]     = static_cast<uint16_t>(r8 * a_tmp);
        d[idx + 1] = static_cast<uint16_t>(g8 * a_tmp);
        d[idx + 2] = static_cast<uint16_t>(b8 * a_tmp);
        d[idx + 3] = static_cast<uint16_t>(255 * a_tmp);
    }
}

} // namespace DirectConvertFuncs

// ========================================================================
// RGBA16_Premultiplied: Premul形式のブレンド・変換関数
// ========================================================================

// blendUnderPremul: srcフォーマット(RGBA16_Premultiplied)からPremul形式のdstへunder合成
// under合成: dst = dst + src * (1 - dstA)
// - dst が不透明なら何もしない（スキップ）
// - dst が透明なら単純コピー
// - dst が半透明ならunder合成
static void rgba16Premul_blendUnderPremul(void* dst, const void* src, int pixelCount, const BlendParams* /*params*/) {
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
static void rgba16Premul_fromPremul(void* dst, const void* src, int pixelCount) {
    FLEXIMG_FMT_METRICS(RGBA16_Premultiplied, FromPremul, pixelCount);
    std::memcpy(dst, src, static_cast<size_t>(pixelCount) * 8);
}
#endif

// ========================================================================
// RGB565_LE: 16bit RGB (Little Endian)
// ========================================================================

static void rgb565le_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(RGB565_LE, ToStandard, pixelCount);
    const uint16_t* s = static_cast<const uint16_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        uint16_t pixel = s[i];
        uint8_t r5 = (pixel >> 11) & 0x1F;
        uint8_t g6 = (pixel >> 5) & 0x3F;
        uint8_t b5 = pixel & 0x1F;

        // ビット拡張（5bit/6bit → 8bit）
        dst[i*4 + 0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
        dst[i*4 + 1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
        dst[i*4 + 2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
        dst[i*4 + 3] = 255;
    }
}

static void rgb565le_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(RGB565_LE, FromStandard, pixelCount);
    uint16_t* d = static_cast<uint16_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t r = src[i*4 + 0];
        uint8_t g = src[i*4 + 1];
        uint8_t b = src[i*4 + 2];
        d[i] = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
}

#if 1  // RGBA16_Premultiplied サポート有効
// blendUnderPremul: srcフォーマット(RGB565_LE)からPremul形式のdstへunder合成
// RGB565はアルファなし→常に不透明として扱う
static void rgb565le_blendUnderPremul(void* dst, const void* src, int pixelCount, const BlendParams* /*params*/) {
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
        uint16_t r8 = static_cast<uint16_t>((r5 << 3) | (r5 >> 2));
        uint16_t g8 = static_cast<uint16_t>((g6 << 2) | (g6 >> 4));
        uint16_t b8 = static_cast<uint16_t>((b5 << 3) | (b5 >> 2));

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
#endif

// ========================================================================
// RGB565_BE: 16bit RGB (Big Endian)
// ========================================================================

static void rgb565be_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(RGB565_BE, ToStandard, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        // ビッグエンディアン: 上位バイトが先
        uint16_t pixel = static_cast<uint16_t>((static_cast<uint16_t>(s[i*2]) << 8) | s[i*2 + 1]);
        uint8_t r5 = (pixel >> 11) & 0x1F;
        uint8_t g6 = (pixel >> 5) & 0x3F;
        uint8_t b5 = pixel & 0x1F;

        dst[i*4 + 0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
        dst[i*4 + 1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
        dst[i*4 + 2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
        dst[i*4 + 3] = 255;
    }
}

static void rgb565be_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(RGB565_BE, FromStandard, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t r = src[i*4 + 0];
        uint8_t g = src[i*4 + 1];
        uint8_t b = src[i*4 + 2];
        uint16_t pixel = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        // ビッグエンディアン: 上位バイトを先に
        d[i*2] = static_cast<uint8_t>(pixel >> 8);
        d[i*2 + 1] = static_cast<uint8_t>(pixel & 0xFF);
    }
}

#if 1  // RGBA16_Premultiplied サポート有効
// blendUnderPremul: srcフォーマット(RGB565_BE)からPremul形式のdstへunder合成
static void rgb565be_blendUnderPremul(void* dst, const void* src, int pixelCount, const BlendParams* /*params*/) {
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
        uint16_t r8 = static_cast<uint16_t>((r5 << 3) | (r5 >> 2));
        uint16_t g8 = static_cast<uint16_t>((g6 << 2) | (g6 >> 4));
        uint16_t b8 = static_cast<uint16_t>((b5 << 3) | (b5 >> 2));

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
#endif

// ========================================================================
// RGB332: 8bit RGB (3-3-2)
// ========================================================================

static void rgb332_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(RGB332, ToStandard, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t pixel = s[i];
        uint8_t r3 = (pixel >> 5) & 0x07;
        uint8_t g3 = (pixel >> 2) & 0x07;
        uint8_t b2 = pixel & 0x03;

        // 乗算＋少量シフト（マイコン最適化）
        dst[i*4 + 0] = (r3 * 0x49) >> 1;  // 3bit → 8bit
        dst[i*4 + 1] = (g3 * 0x49) >> 1;  // 3bit → 8bit
        dst[i*4 + 2] = b2 * 0x55;          // 2bit → 8bit
        dst[i*4 + 3] = 255;
    }
}

static void rgb332_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(RGB332, FromStandard, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t r = src[i*4 + 0];
        uint8_t g = src[i*4 + 1];
        uint8_t b = src[i*4 + 2];
        d[i] = static_cast<uint8_t>((r & 0xE0) | ((g >> 5) << 2) | (b >> 6));
    }
}

#if 1  // RGBA16_Premultiplied サポート有効
// blendUnderPremul: srcフォーマット(RGB332)からPremul形式のdstへunder合成
static void rgb332_blendUnderPremul(void* dst, const void* src, int pixelCount, const BlendParams* /*params*/) {
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
#endif

// ========================================================================
// RGB888: 24bit RGB (mem[0]=R, mem[1]=G, mem[2]=B)
// ========================================================================

static void rgb888_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(RGB888, ToStandard, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        dst[i*4 + 0] = s[i*3 + 0];  // R
        dst[i*4 + 1] = s[i*3 + 1];  // G
        dst[i*4 + 2] = s[i*3 + 2];  // B
        dst[i*4 + 3] = 255;          // A
    }
}

static void rgb888_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(RGB888, FromStandard, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        d[i*3 + 0] = src[i*4 + 0];  // R
        d[i*3 + 1] = src[i*4 + 1];  // G
        d[i*3 + 2] = src[i*4 + 2];  // B
    }
}

#if 1  // RGBA16_Premultiplied サポート有効
// blendUnderPremul: srcフォーマット(RGB888)からPremul形式のdstへunder合成
static void rgb888_blendUnderPremul(void* dst, const void* src, int pixelCount, const BlendParams* /*params*/) {
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
#endif

// ========================================================================
// BGR888: 24bit BGR (mem[0]=B, mem[1]=G, mem[2]=R)
// ========================================================================

static void bgr888_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(BGR888, ToStandard, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        dst[i*4 + 0] = s[i*3 + 2];  // R (src の B 位置)
        dst[i*4 + 1] = s[i*3 + 1];  // G
        dst[i*4 + 2] = s[i*3 + 0];  // B (src の R 位置)
        dst[i*4 + 3] = 255;
    }
}

static void bgr888_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    FLEXIMG_FMT_METRICS(BGR888, FromStandard, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        d[i*3 + 0] = src[i*4 + 2];  // B
        d[i*3 + 1] = src[i*4 + 1];  // G
        d[i*3 + 2] = src[i*4 + 0];  // R
    }
}

#if 1  // RGBA16_Premultiplied サポート有効
// blendUnderPremul: srcフォーマット(BGR888)からPremul形式のdstへunder合成
static void bgr888_blendUnderPremul(void* dst, const void* src, int pixelCount, const BlendParams* /*params*/) {
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
#endif

// ========================================================================
// 組み込みフォーマット定義
// ========================================================================

namespace BuiltinFormats {

#if 1  // RGBA16_Premultiplied サポート有効
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
    rgba16Premul_toStandard,
    rgba16Premul_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr,  // fromStandardIndexed
    rgba16Premul_blendUnderPremul,
    rgba16Premul_fromPremul
};
#endif

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
    rgba8Straight_toStandard,
    rgba8Straight_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr,  // fromStandardIndexed
#if 1  // RGBA16_Premultiplied サポート有効
    rgba8Straight_blendUnderPremul,
    rgba8Straight_fromPremul
#else
    nullptr,  // blendUnderPremul
    nullptr   // fromPremul
#endif
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
    rgb565le_toStandard,
    rgb565le_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr,  // fromStandardIndexed
#if 1  // RGBA16_Premultiplied サポート有効
    rgb565le_blendUnderPremul,
    nullptr   // fromPremul (RGB565にはアルファがないのでPremulからの変換は不要)
#else
    nullptr,  // blendUnderPremul
    nullptr   // fromPremul
#endif
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
    rgb565be_toStandard,
    rgb565be_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr,  // fromStandardIndexed
#if 1  // RGBA16_Premultiplied サポート有効
    rgb565be_blendUnderPremul,
    nullptr   // fromPremul
#else
    nullptr,  // blendUnderPremul
    nullptr   // fromPremul
#endif
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
    rgb332_toStandard,
    rgb332_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr,  // fromStandardIndexed
#if 1  // RGBA16_Premultiplied サポート有効
    rgb332_blendUnderPremul,
    nullptr   // fromPremul
#else
    nullptr,  // blendUnderPremul
    nullptr   // fromPremul
#endif
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
    rgb888_toStandard,
    rgb888_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr,  // fromStandardIndexed
#if 1  // RGBA16_Premultiplied サポート有効
    rgb888_blendUnderPremul,
    nullptr   // fromPremul
#else
    nullptr,  // blendUnderPremul
    nullptr   // fromPremul
#endif
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
    bgr888_toStandard,
    bgr888_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr,  // fromStandardIndexed
#if 1  // RGBA16_Premultiplied サポート有効
    bgr888_blendUnderPremul,
    nullptr   // fromPremul
#else
    nullptr,  // blendUnderPremul
    nullptr   // fromPremul
#endif
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
    alpha8_toStandard,
    alpha8_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr,  // fromStandardIndexed
    nullptr,  // blendUnderPremul
    nullptr   // fromPremul
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE
