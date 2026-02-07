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
// ビット操作ヘルパー関数（bit-packed Index用）
// ========================================================================

namespace bit_packed_detail {

// ========================================================================
// unpackIndexBits: packed bytes → 8bit index array
// ========================================================================

template<int BitsPerPixel, BitOrder Order>
inline void unpackIndexBits(uint8_t* dst, const uint8_t* src, size_t pixelCount, uint8_t pixelOffset = 0) {
    constexpr int PixelsPerByte = 8 / BitsPerPixel;
    constexpr uint8_t Mask = (1 << BitsPerPixel) - 1;

    // pixelOffsetは1バイト内でのピクセル位置 (0 - PixelsPerByte-1)
    // 最初のバイトでの開始位置を調整
    size_t pixelIdx = pixelOffset;
    size_t byteIdx = 0;
    size_t dstIdx = 0;

    while (dstIdx < pixelCount) {
        uint8_t b = src[byteIdx];
        size_t remainingInByte = static_cast<size_t>(PixelsPerByte) - pixelIdx;
        size_t pixelsToRead = (pixelCount - dstIdx < remainingInByte) ? (pixelCount - dstIdx) : remainingInByte;

        for (size_t j = 0; j < pixelsToRead; ++j) {
            size_t bitPos = pixelIdx + j;
            if constexpr (Order == BitOrder::MSBFirst) {
                dst[dstIdx++] = (b >> ((PixelsPerByte - 1 - bitPos) * BitsPerPixel)) & Mask;
            } else {
                dst[dstIdx++] = (b >> (bitPos * BitsPerPixel)) & Mask;
            }
        }

        ++byteIdx;
        pixelIdx = 0;  // 次のバイトからは先頭から読む
    }
}

// ========================================================================
// packIndexBits: 8bit index array → packed bytes
// ========================================================================

template<int BitsPerPixel, BitOrder Order>
inline void packIndexBits(uint8_t* dst, const uint8_t* src, size_t pixelCount) {
    constexpr size_t PixelsPerByte = 8 / BitsPerPixel;
    constexpr uint8_t Mask = (1 << BitsPerPixel) - 1;

    size_t bytes = (pixelCount + PixelsPerByte - 1) / PixelsPerByte;
    for (size_t i = 0; i < bytes; ++i) {
        uint8_t b = 0;
        size_t pixels_in_byte = (pixelCount >= PixelsPerByte) ? PixelsPerByte : pixelCount;
        for (size_t j = 0; j < pixels_in_byte; ++j) {
            if constexpr (Order == BitOrder::MSBFirst) {
                b |= ((src[j] & Mask) << ((PixelsPerByte - 1 - j) * BitsPerPixel));
            } else {
                b |= ((src[j] & Mask) << (j * BitsPerPixel));
            }
        }
        dst[i] = b;
        src += PixelsPerByte;
        pixelCount -= PixelsPerByte;
    }
}

// ========================================================================
// ビット単位アクセスヘルパー（LovyanGFXスタイル）
// ========================================================================

// 指定座標のピクセルを bit-packed データから直接読み取り
template<int BitsPerPixel, BitOrder Order>
inline uint8_t readPixelDirect(const uint8_t* srcData, int32_t x, int32_t y, int32_t stride) {
    constexpr uint8_t Mask = (1 << BitsPerPixel) - 1;

    // ビット単位のオフセット計算
    int32_t pixelOffsetInByte = (y * stride * 8) + (x * BitsPerPixel);
    int32_t byteIdx = pixelOffsetInByte >> 3;
    int32_t bitPos = pixelOffsetInByte & 7;

    uint8_t byte = srcData[byteIdx];

    if constexpr (Order == BitOrder::MSBFirst) {
        // MSBFirst: 上位ビットから読む
        return (byte >> (8 - bitPos - BitsPerPixel)) & Mask;
    } else {
        // LSBFirst: 下位ビットから読む
        return (byte >> bitPos) & Mask;
    }
}

} // namespace bit_packed_detail

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
// Index8: Index8 → RGBA8_Straight 変換（パレットなし時のフォールバック）
// ========================================================================
//
// パレットが利用できない場合、インデックス値をグレースケールとして展開。
// convertFormat内では expandIndex+パレット のパスが先に評価されるため、
// パレットが設定されている場合はこの関数は呼ばれない。
//

static void index8_toStraight(void* dst, const void* src,
                               size_t pixelCount, const PixelAuxInfo*) {
    FLEXIMG_FMT_METRICS(Index8, ToStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < pixelCount; ++i) {
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
                                 size_t pixelCount, const PixelAuxInfo*) {
    FLEXIMG_FMT_METRICS(Index8, FromStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);

    // 端数処理（1〜3ピクセル）
    size_t remainder = pixelCount & 3;
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
    index8_toStraight,     // toStraight (パレットなし時のグレースケールフォールバック)
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

// 変換関数: toStraight (パレットなし時のグレースケール展開)
// 末尾詰め方式: 出力バッファ(RGBA8=4byte/pixel)の末尾にIndex8データをunpackし、
// スケーリング後に index8_toStraight でin-place展開する
template<int BitsPerPixel, BitOrder Order>
static void indexN_toStraight(
    void* __restrict__ dst,
    const void* __restrict__ src,
    size_t pixelCount,
    const PixelAuxInfo* aux
) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);

    // 末尾詰め: RGBA8出力(4byte/pixel)の後方にIndex8(1byte/pixel)をunpack
    uint8_t* indexData = d + static_cast<size_t>(pixelCount) * 3;

    const uint8_t pixelOffsetInByte = aux ? aux->pixelOffsetInByte : 0;
    bit_packed_detail::unpackIndexBits<BitsPerPixel, Order>(
        indexData, s, pixelCount, pixelOffsetInByte);

    // スケーリング: IndexN値(0-MaxIndex) → 0-255 (Index8相当)
    constexpr int MaxIndex = (1 << BitsPerPixel) - 1;
    constexpr int Scale = 255 / MaxIndex;
    for (size_t i = 0; i < pixelCount; ++i) {
        indexData[i] = static_cast<uint8_t>(indexData[i] * Scale);
    }

    // index8_toStraight に委譲（in-place: indexData → d）
    index8_toStraight(dst, indexData, pixelCount, nullptr);
}

// 変換関数: fromStraight (RGBA8 → Index, 輝度計算 + 量子化)
template<int BitsPerPixel, BitOrder Order>
static void indexN_fromStraight(
    void* __restrict__ dst,
    const void* __restrict__ src,
    size_t pixelCount,
    const PixelAuxInfo*
) {
    constexpr size_t MaxPixelsPerByte = 8 / BitsPerPixel;
    constexpr size_t ChunkSize = 64;
    uint8_t indexBuf[ChunkSize];

    const uint8_t* srcPtr = static_cast<const uint8_t*>(src);
    uint8_t* dstPtr = static_cast<uint8_t*>(dst);

    // 量子化シフト量
    constexpr int QuantizeShift = 8 - BitsPerPixel;

    size_t remaining = pixelCount;
    while (remaining > 0) {
        size_t chunk = (remaining < ChunkSize) ? remaining : ChunkSize;

        // BT.601 輝度計算 + 量子化
        for (size_t i = 0; i < chunk; ++i) {
            uint_fast16_t r = srcPtr[i * 4 + 0];
            uint_fast16_t g = srcPtr[i * 4 + 1];
            uint_fast16_t b = srcPtr[i * 4 + 2];
            // BT.601: Y = (77*R + 150*G + 29*B + 128) >> 8
            uint8_t lum = static_cast<uint8_t>((77 * r + 150 * g + 29 * b + 128) >> 8);
            // 量子化
            indexBuf[i] = lum >> QuantizeShift;
        }

        // パック
        bit_packed_detail::packIndexBits<BitsPerPixel, Order>(dstPtr, indexBuf, chunk);

        srcPtr += chunk * 4;
        dstPtr += (chunk + MaxPixelsPerByte - 1) / MaxPixelsPerByte;
        remaining -= chunk;
    }
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
    indexN_toStraight<1, BitOrder::MSBFirst>,
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
    indexN_toStraight<1, BitOrder::LSBFirst>,
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
    indexN_toStraight<2, BitOrder::MSBFirst>,
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
    indexN_toStraight<2, BitOrder::LSBFirst>,
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
    indexN_toStraight<4, BitOrder::MSBFirst>,
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
    indexN_toStraight<4, BitOrder::LSBFirst>,
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
