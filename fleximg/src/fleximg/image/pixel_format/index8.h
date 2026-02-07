#ifndef FLEXIMG_PIXEL_FORMAT_INDEX8_H
#define FLEXIMG_PIXEL_FORMAT_INDEX8_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
    extern const PixelFormatDescriptor Index8;
}

namespace PixelFormatIDs {
    inline const PixelFormatID Index8 = &BuiltinFormats::Index8;
}

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

#include "../../core/format_metrics.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// Index8: パレットインデックス（8bit） → パレットフォーマットのピクセルデータ
// ========================================================================

// expandIndex: インデックス値をパレットフォーマットのピクセルに展開
// aux->palette, aux->paletteFormat を参照
// 出力はパレットフォーマットのピクセルデータ
static void index8_expandIndex(void* __restrict__ dst, const void* __restrict__ src,
                               int pixelCount, const PixelAuxInfo* __restrict__ aux) {
    FLEXIMG_FMT_METRICS(Index8, ToStraight, pixelCount);
    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);
    uint8_t* __restrict__ d = static_cast<uint8_t*>(dst);

    if (!aux || !aux->palette || !aux->paletteFormat) {
        // パレットなし: ゼロ埋め
        std::memset(dst, 0, static_cast<size_t>(pixelCount));
        return;
    }

    const uint8_t* p = static_cast<const uint8_t*>(aux->palette);
    // パレットフォーマットのバイト数を取得
    // 注意: インデックス値の境界チェックは行わない（呼び出し側の責務）
    int_fast8_t bpc = static_cast<int_fast8_t>(aux->paletteFormat->bytesPerPixel);

    if (bpc == 4) {
        // 4バイト（RGBA8等）高速パス
        pixel_format::detail::lut8to32(reinterpret_cast<uint32_t*>(d), s, pixelCount,
                         reinterpret_cast<const uint32_t*>(p));
    } else if (bpc == 2) {
        // 2バイト（RGB565等）高速パス
        pixel_format::detail::lut8to16(reinterpret_cast<uint16_t*>(d), s, pixelCount,
                         reinterpret_cast<const uint16_t*>(p));
    } else {
        // 汎用パス（1, 3バイト等）
        for (int i = 0; i < pixelCount; ++i) {
            std::memcpy(d + static_cast<size_t>(i) * static_cast<size_t>(bpc),
                        p + static_cast<size_t>(s[i]) * static_cast<size_t>(bpc),
                        static_cast<size_t>(bpc));
        }
    }
}

// ========================================================================
// Index8: Index8 → RGBA8_Straight 変換（パレットなし時のフォールバック）
// ========================================================================
//
// パレットが利用できない場合、インデックス値をグレースケールとして展開。
// convertFormat内では expandIndex+パレット のパスが先に評価されるため、
// パレットが設定されている場合はこの関数は呼ばれない。
//

static void index8_toStraight(void* dst, const void* src,
                               int pixelCount, const PixelAuxInfo*) {
    FLEXIMG_FMT_METRICS(Index8, ToStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; ++i) {
        uint8_t v = s[i];
        d[i*4 + 0] = v;    // R
        d[i*4 + 1] = v;    // G
        d[i*4 + 2] = v;    // B
        d[i*4 + 3] = 255;  // A
    }
}

// ========================================================================
// Index8: RGBA8_Straight → Index8 変換（BT.601 輝度抽出）
// ========================================================================
//
// RGBカラーからインデックス値への変換。
// パレットへの最近傍色マッチングではなく、BT.601輝度計算を使用。
// Grayscale8のfromStraightと同一の計算式: index = (77*R + 150*G + 29*B + 128) >> 8
//

static void index8_fromStraight(void* dst, const void* src,
                                 int pixelCount, const PixelAuxInfo*) {
    FLEXIMG_FMT_METRICS(Index8, FromStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        d[0] = static_cast<uint8_t>((77 * s[0] + 150 * s[1] + 29 * s[2] + 128) >> 8);
        s += 4;
        d += 1;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        d[0] = static_cast<uint8_t>((77 * s[0] + 150 * s[1] + 29 * s[2] + 128) >> 8);
        d[1] = static_cast<uint8_t>((77 * s[4] + 150 * s[5] + 29 * s[6] + 128) >> 8);
        d[2] = static_cast<uint8_t>((77 * s[8] + 150 * s[9] + 29 * s[10] + 128) >> 8);
        d[3] = static_cast<uint8_t>((77 * s[12] + 150 * s[13] + 29 * s[14] + 128) >> 8);
        s += 16;
        d += 4;
    }
}

// ------------------------------------------------------------------------
// フォーマット定義
// ------------------------------------------------------------------------

namespace BuiltinFormats {

const PixelFormatDescriptor Index8 = {
    "Index8",
    8,   // bitsPerPixel
    1,   // bytesPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    1,   // channelCount
    { ChannelDescriptor(ChannelType::Index, 8, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },  // Index only
    false,   // hasAlpha
    true,    // isIndexed
    256,     // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    index8_toStraight,     // toStraight (パレットなし時のグレースケールフォールバック)
    index8_fromStraight,   // fromStraight (BT.601 輝度抽出)
    index8_expandIndex,  // expandIndex
    nullptr,  // blendUnderStraight
    nullptr,  // siblingEndian
    nullptr,  // swapEndian
    pixel_format::detail::copyRowDDA_1bpp,  // copyRowDDA
    pixel_format::detail::copyQuadDDA_1bpp  // copyQuadDDA（インデックス抽出、パレット展開はconvertFormatで実施）
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_INDEX8_H
