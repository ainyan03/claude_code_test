#ifndef FLEXIMG_PIXEL_FORMAT_INDEX_H
#define FLEXIMG_PIXEL_FORMAT_INDEX_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言（Indexフォーマット全般）
// ========================================================================
//
// このファイルは以下のインデックスフォーマットを定義:
// - Index1_MSB/LSB: 1ビット/ピクセル（bit-packed）
// - Index2_MSB/LSB: 2ビット/ピクセル（bit-packed）
// - Index4_MSB/LSB: 4ビット/ピクセル（bit-packed）
// - Index8: 8ビット/ピクセル
//

namespace BuiltinFormats {
    // Bit-packed Index formats
    extern const PixelFormatDescriptor Index1_MSB;
    extern const PixelFormatDescriptor Index1_LSB;
    extern const PixelFormatDescriptor Index2_MSB;
    extern const PixelFormatDescriptor Index2_LSB;
    extern const PixelFormatDescriptor Index4_MSB;
    extern const PixelFormatDescriptor Index4_LSB;

    // 8-bit Index format
    extern const PixelFormatDescriptor Index8;
}

namespace PixelFormatIDs {
    inline const PixelFormatID Index1_MSB = &BuiltinFormats::Index1_MSB;
    inline const PixelFormatID Index1_LSB = &BuiltinFormats::Index1_LSB;
    inline const PixelFormatID Index2_MSB = &BuiltinFormats::Index2_MSB;
    inline const PixelFormatID Index2_LSB = &BuiltinFormats::Index2_LSB;
    inline const PixelFormatID Index4_MSB = &BuiltinFormats::Index4_MSB;
    inline const PixelFormatID Index4_LSB = &BuiltinFormats::Index4_LSB;

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
// 共通パレットLUT関数（__restrict__ なし、in-place安全）
// ========================================================================
//
// インデックス値（uint8_t配列）をパレットフォーマットのピクセルに展開する。
// index8_expandIndex / indexN_expandIndex 双方から呼ばれる共通実装。
// __restrict__ なしのため、末尾詰め方式のin-place展開にも対応。
//
// lut8toN は4ピクセル単位で「全読み→全書き」するため、
// src が dst の末尾に配置されている場合でも読み出しが書き込みより先行し安全。

static void applyPaletteLUT(void* dst, const void* src,
                            size_t pixelCount, const PixelAuxInfo* aux) {
    if (!aux || !aux->palette || !aux->paletteFormat) {
        std::memset(dst, 0, pixelCount);
        return;
    }

    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* p = static_cast<const uint8_t*>(aux->palette);
    int_fast8_t bpc = static_cast<int_fast8_t>(aux->paletteFormat->bytesPerPixel);

    if (bpc == 4) {
        pixel_format::detail::lut8to32(reinterpret_cast<uint32_t*>(d), s, pixelCount,
                         reinterpret_cast<const uint32_t*>(p));
    } else if (bpc == 2) {
        pixel_format::detail::lut8to16(reinterpret_cast<uint16_t*>(d), s, pixelCount,
                         reinterpret_cast<const uint16_t*>(p));
    } else {
        for (size_t i = 0; i < pixelCount; ++i) {
            std::memcpy(d + static_cast<size_t>(i) * static_cast<size_t>(bpc),
                        p + static_cast<size_t>(s[i]) * static_cast<size_t>(bpc),
                        static_cast<size_t>(bpc));
        }
    }
}

// ========================================================================
// Index8: パレットインデックス（8bit） → パレットフォーマットのピクセルデータ
// ========================================================================

static void index8_expandIndex(void* __restrict__ dst, const void* __restrict__ src,
                               size_t pixelCount, const PixelAuxInfo* __restrict__ aux) {
    FLEXIMG_FMT_METRICS(Index8, ToStraight, pixelCount);
    applyPaletteLUT(dst, src, pixelCount, aux);
}

// ========================================================================
// Index8: RGBA8_Straight → Index8 変換
// ========================================================================
//
// RGBカラーからインデックス値への変換。
// パレットなし時はBT.601輝度計算にフォールバック（grayscale8_fromStraightに委譲）。
// 将来的にパレットへの最近傍色マッチングに拡張予定。
//

static void index8_fromStraight(void* dst, const void* src,
                                 size_t pixelCount, const PixelAuxInfo* aux) {
    FLEXIMG_FMT_METRICS(Index8, FromStraight, pixelCount);
    // パレットなし: グレースケール変換にフォールバック
    grayscale8_fromStraight(dst, src, pixelCount, aux);
}

// ------------------------------------------------------------------------
// フォーマット定義
// ------------------------------------------------------------------------

namespace BuiltinFormats {

const PixelFormatDescriptor Index8 = {
    "Index8",
    grayscale8_toStraight,  // toStraight (パレットなし時はGrayscale8にフォールバック)
    index8_fromStraight,   // fromStraight (BT.601 輝度抽出)
    index8_expandIndex,  // expandIndex
    nullptr,  // blendUnderStraight
    nullptr,  // siblingEndian
    nullptr,  // swapEndian
    pixel_format::detail::copyRowDDA_1Byte,  // copyRowDDA
    pixel_format::detail::copyQuadDDA_1Byte, // copyQuadDDA（インデックス抽出、パレット展開はconvertFormatで実施）
    BitOrder::MSBFirst,
    ByteOrder::Native,
    256,     // maxPaletteSize
    8,   // bitsPerPixel
    1,   // bytesPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    1,   // channelCount
    false,   // hasAlpha
    true,    // isIndexed
};

// ========================================================================
// Bit-packed Index Formats (Index1/2/4 MSB/LSB)
// ========================================================================

// 変換関数: expandIndex (パレット展開)
// 末尾詰め方式: 出力バッファ末尾にIndex8データをunpackし、
// applyPaletteLUTでin-place展開する（チャンクバッファ不要）
template<int BitsPerPixel, BitOrder Order>
static void indexN_expandIndex(
    void* __restrict__ dst,
    const void* __restrict__ src,
    size_t pixelCount,
    const PixelAuxInfo* __restrict__ aux
) {
    if (!aux || !aux->palette || !aux->paletteFormat) {
        std::memset(dst, 0, pixelCount);
        return;
    }

    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    int palBpp = aux->paletteFormat->bytesPerPixel;

    // 末尾詰め: dstの後方にIndex8データをunpack
    // palBpp=4: offset=3N, palBpp=2: offset=N, palBpp=1: offset=0
    uint8_t* indexData = d + static_cast<size_t>(pixelCount) * static_cast<size_t>(palBpp - 1);

    bit_packed_detail::unpackIndexBits<BitsPerPixel, Order>(
        indexData, s, pixelCount, aux->pixelOffsetInByte);

    // 共通パレットLUTでin-place展開
    applyPaletteLUT(dst, indexData, pixelCount, aux);
}

// 変換関数: fromStraight (RGBA8 → Index, 輝度計算 + 量子化)
// grayscaleN_fromStraight への委譲ラッパー
template<int BitsPerPixel, BitOrder Order>
static void indexN_fromStraight(
    void* __restrict__ dst,
    const void* __restrict__ src,
    size_t pixelCount,
    const PixelAuxInfo* aux
) {
    FLEXIMG_FMT_METRICS(IndexN, FromStraight, pixelCount);
    grayscaleN_fromStraight<BitsPerPixel, Order>(dst, src, pixelCount, aux);
}

// ------------------------------------------------------------------------
// Bit-packed Index Formats フォーマット定義
// ------------------------------------------------------------------------

// Forward declarations for sibling references
extern const PixelFormatDescriptor Index1_LSB;
extern const PixelFormatDescriptor Index2_LSB;
extern const PixelFormatDescriptor Index4_LSB;

const PixelFormatDescriptor Index1_MSB = {
    "Index1_MSB",
    grayscaleN_toStraight<1, BitOrder::MSBFirst>,
    indexN_fromStraight<1, BitOrder::MSBFirst>,
    indexN_expandIndex<1, BitOrder::MSBFirst>,
    nullptr,  // blendUnderStraight
    &Index1_LSB,  // siblingEndian
    nullptr,  // swapEndian
    pixel_format::detail::copyRowDDA_Bit<1, BitOrder::MSBFirst>,   // copyRowDDA
    pixel_format::detail::copyQuadDDA_Bit<1, BitOrder::MSBFirst>,  // copyQuadDDA
    BitOrder::MSBFirst,
    ByteOrder::Native,
    2,      // maxPaletteSize
    1,   // bitsPerPixel
    1,   // bytesPerPixel
    8,   // pixelsPerUnit
    1,   // bytesPerUnit
    1,   // channelCount
    false,  // hasAlpha
    true,   // isIndexed
};

const PixelFormatDescriptor Index1_LSB = {
    "Index1_LSB",
    grayscaleN_toStraight<1, BitOrder::LSBFirst>,
    indexN_fromStraight<1, BitOrder::LSBFirst>,
    indexN_expandIndex<1, BitOrder::LSBFirst>,
    nullptr, &Index1_MSB, nullptr,
    pixel_format::detail::copyRowDDA_Bit<1, BitOrder::LSBFirst>,
    pixel_format::detail::copyQuadDDA_Bit<1, BitOrder::LSBFirst>,
    BitOrder::LSBFirst,
    ByteOrder::Native,
    2,      // maxPaletteSize
    1, 1, 8, 1, 1,
    false, true,
};

const PixelFormatDescriptor Index2_MSB = {
    "Index2_MSB",
    grayscaleN_toStraight<2, BitOrder::MSBFirst>,
    indexN_fromStraight<2, BitOrder::MSBFirst>,
    indexN_expandIndex<2, BitOrder::MSBFirst>,
    nullptr, &Index2_LSB, nullptr,
    pixel_format::detail::copyRowDDA_Bit<2, BitOrder::MSBFirst>,
    pixel_format::detail::copyQuadDDA_Bit<2, BitOrder::MSBFirst>,
    BitOrder::MSBFirst,
    ByteOrder::Native,
    4,      // maxPaletteSize
    2, 1, 4, 1, 1,
    false, true,
};

const PixelFormatDescriptor Index2_LSB = {
    "Index2_LSB",
    grayscaleN_toStraight<2, BitOrder::LSBFirst>,
    indexN_fromStraight<2, BitOrder::LSBFirst>,
    indexN_expandIndex<2, BitOrder::LSBFirst>,
    nullptr, &Index2_MSB, nullptr,
    pixel_format::detail::copyRowDDA_Bit<2, BitOrder::LSBFirst>,
    pixel_format::detail::copyQuadDDA_Bit<2, BitOrder::LSBFirst>,
    BitOrder::LSBFirst,
    ByteOrder::Native,
    4,      // maxPaletteSize
    2, 1, 4, 1, 1,
    false, true,
};

const PixelFormatDescriptor Index4_MSB = {
    "Index4_MSB",
    grayscaleN_toStraight<4, BitOrder::MSBFirst>,
    indexN_fromStraight<4, BitOrder::MSBFirst>,
    indexN_expandIndex<4, BitOrder::MSBFirst>,
    nullptr, &Index4_LSB, nullptr,
    pixel_format::detail::copyRowDDA_Bit<4, BitOrder::MSBFirst>,
    pixel_format::detail::copyQuadDDA_Bit<4, BitOrder::MSBFirst>,
    BitOrder::MSBFirst,
    ByteOrder::Native,
    16,     // maxPaletteSize
    4, 1, 2, 1, 1,
    false, true,
};

const PixelFormatDescriptor Index4_LSB = {
    "Index4_LSB",
    grayscaleN_toStraight<4, BitOrder::LSBFirst>,
    indexN_fromStraight<4, BitOrder::LSBFirst>,
    indexN_expandIndex<4, BitOrder::LSBFirst>,
    nullptr, &Index4_MSB, nullptr,
    pixel_format::detail::copyRowDDA_Bit<4, BitOrder::LSBFirst>,
    pixel_format::detail::copyQuadDDA_Bit<4, BitOrder::LSBFirst>,
    BitOrder::LSBFirst,
    ByteOrder::Native,
    16,     // maxPaletteSize
    4, 1, 2, 1, 1,
    false, true,
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_INDEX_H
