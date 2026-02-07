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
inline void unpackIndexBits(uint8_t* dst, const uint8_t* src, int pixelCount, uint8_t pixelOffset = 0) {
    constexpr int PixelsPerByte = 8 / BitsPerPixel;
    constexpr uint8_t Mask = (1 << BitsPerPixel) - 1;

    // pixelOffsetは1バイト内でのピクセル位置 (0 - PixelsPerByte-1)
    // 最初のバイトでの開始位置を調整
    int pixelIdx = pixelOffset;
    int byteIdx = 0;
    int dstIdx = 0;

    while (dstIdx < pixelCount) {
        uint8_t b = src[byteIdx];
        int remainingInByte = PixelsPerByte - pixelIdx;
        int pixelsToRead = (pixelCount - dstIdx < remainingInByte) ? (pixelCount - dstIdx) : remainingInByte;

        for (int j = 0; j < pixelsToRead; ++j) {
            int bitPos = pixelIdx + j;
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
inline void packIndexBits(uint8_t* dst, const uint8_t* src, int pixelCount) {
    constexpr int PixelsPerByte = 8 / BitsPerPixel;
    constexpr uint8_t Mask = (1 << BitsPerPixel) - 1;

    int bytes = (pixelCount + PixelsPerByte - 1) / PixelsPerByte;
    for (int i = 0; i < bytes; ++i) {
        uint8_t b = 0;
        int pixels_in_byte = (pixelCount >= PixelsPerByte) ? PixelsPerByte : pixelCount;
        for (int j = 0; j < pixels_in_byte; ++j) {
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
    pixel_format::detail::copyRowDDA_1Byte,  // copyRowDDA
    pixel_format::detail::copyQuadDDA_1Byte  // copyQuadDDA（インデックス抽出、パレット展開はconvertFormatで実施）
};

// ========================================================================
// Bit-packed Index Formats (Index1/2/4 MSB/LSB)
// ========================================================================

// 変換関数: expandIndex (パレット展開)
template<int BitsPerPixel, BitOrder Order>
static void indexN_expandIndex(
    void* __restrict__ dst,
    const void* __restrict__ src,
    int pixelCount,
    const PixelAuxInfo* __restrict__ aux
) {
    if (!aux || !aux->palette || !aux->paletteFormat) {
        // パレットなし: ゼロ埋め
        std::memset(dst, 0, static_cast<size_t>(pixelCount));
        return;
    }

    // ビットアンパック: packed → 8bit index array
    constexpr int MaxPixelsPerByte = 8 / BitsPerPixel;
    constexpr int ChunkSize = 64;  // スタックバッファサイズ
    uint8_t indexBuf[ChunkSize];

    const uint8_t* srcPtr = static_cast<const uint8_t*>(src);
    uint8_t* dstPtr = static_cast<uint8_t*>(dst);
    const uint8_t* palette = static_cast<const uint8_t*>(aux->palette);
    // パレットフォーマットのバイト数を取得
    int_fast8_t paletteBpp = static_cast<int_fast8_t>(aux->paletteFormat->bytesPerPixel);

    const uint8_t pixelOffsetInByte = aux->pixelOffsetInByte;  // bit-packed用のビットオフセット取得
    int remaining = pixelCount;
    bool isFirstChunk = true;

    while (remaining > 0) {
        int chunk = (remaining < ChunkSize) ? remaining : ChunkSize;

        // アンパック（最初のチャンクのみpixelOffsetInByteを使用）
        if (isFirstChunk && pixelOffsetInByte != 0) {
            bit_packed_detail::unpackIndexBits<BitsPerPixel, Order>(indexBuf, srcPtr, chunk, pixelOffsetInByte);
            isFirstChunk = false;
        } else {
            bit_packed_detail::unpackIndexBits<BitsPerPixel, Order>(indexBuf, srcPtr, chunk);
        }

        // パレット展開
        if (paletteBpp == 4) {
            pixel_format::detail::lut8to32(
                reinterpret_cast<uint32_t*>(dstPtr),
                indexBuf,
                chunk,
                reinterpret_cast<const uint32_t*>(palette)
            );
        } else if (paletteBpp == 2) {
            pixel_format::detail::lut8to16(
                reinterpret_cast<uint16_t*>(dstPtr),
                indexBuf,
                chunk,
                reinterpret_cast<const uint16_t*>(palette)
            );
        } else {
            // 汎用パス (1, 3 byte等)
            for (int i = 0; i < chunk; ++i) {
                std::memcpy(
                    dstPtr + static_cast<size_t>(i) * static_cast<size_t>(paletteBpp),
                    palette + static_cast<size_t>(indexBuf[i]) * static_cast<size_t>(paletteBpp),
                    static_cast<size_t>(paletteBpp)
                );
            }
        }

        srcPtr += (chunk + MaxPixelsPerByte - 1) / MaxPixelsPerByte;
        dstPtr += chunk * paletteBpp;
        remaining -= chunk;
    }
}

// 変換関数: toStraight (パレットなし時のグレースケール展開)
template<int BitsPerPixel, BitOrder Order>
static void indexN_toStraight(
    void* __restrict__ dst,
    const void* __restrict__ src,
    int pixelCount,
    const PixelAuxInfo* aux
) {
    constexpr int MaxPixelsPerByte = 8 / BitsPerPixel;
    constexpr int ChunkSize = 64;
    uint8_t indexBuf[ChunkSize];

    const uint8_t* srcPtr = static_cast<const uint8_t*>(src);
    uint8_t* dstPtr = static_cast<uint8_t*>(dst);

    // スケーリング係数（インデックス値を0-255に拡張）
    constexpr int MaxIndex = (1 << BitsPerPixel) - 1;
    constexpr int Scale = 255 / MaxIndex;

    const uint8_t pixelOffsetInByte = aux ? aux->pixelOffsetInByte : 0;  // bit-packed用のビットオフセット取得
    int remaining = pixelCount;
    bool isFirstChunk = true;

    while (remaining > 0) {
        int chunk = (remaining < ChunkSize) ? remaining : ChunkSize;

        // アンパック（最初のチャンクのみpixelOffsetInByteを使用）
        if (isFirstChunk && pixelOffsetInByte != 0) {
            bit_packed_detail::unpackIndexBits<BitsPerPixel, Order>(indexBuf, srcPtr, chunk, pixelOffsetInByte);
            isFirstChunk = false;
        } else {
            bit_packed_detail::unpackIndexBits<BitsPerPixel, Order>(indexBuf, srcPtr, chunk);
        }

        // グレースケール展開 (RGBA8)
        for (int i = 0; i < chunk; ++i) {
            uint8_t gray = static_cast<uint8_t>(indexBuf[i] * Scale);
            dstPtr[i * 4 + 0] = gray;  // R
            dstPtr[i * 4 + 1] = gray;  // G
            dstPtr[i * 4 + 2] = gray;  // B
            dstPtr[i * 4 + 3] = 255;   // A
        }

        srcPtr += (chunk + MaxPixelsPerByte - 1) / MaxPixelsPerByte;
        dstPtr += chunk * 4;
        remaining -= chunk;
    }
}

// 変換関数: fromStraight (RGBA8 → Index, 輝度計算 + 量子化)
template<int BitsPerPixel, BitOrder Order>
static void indexN_fromStraight(
    void* __restrict__ dst,
    const void* __restrict__ src,
    int pixelCount,
    const PixelAuxInfo*
) {
    constexpr int MaxPixelsPerByte = 8 / BitsPerPixel;
    constexpr int ChunkSize = 64;
    uint8_t indexBuf[ChunkSize];

    const uint8_t* srcPtr = static_cast<const uint8_t*>(src);
    uint8_t* dstPtr = static_cast<uint8_t*>(dst);

    // 量子化シフト量
    constexpr int QuantizeShift = 8 - BitsPerPixel;

    int remaining = pixelCount;
    while (remaining > 0) {
        int chunk = (remaining < ChunkSize) ? remaining : ChunkSize;

        // BT.601 輝度計算 + 量子化
        for (int i = 0; i < chunk; ++i) {
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
    1,   // bitsPerPixel
    1,   // bytesPerPixel
    8,   // pixelsPerUnit
    1,   // bytesPerUnit
    1,   // channelCount
    { ChannelDescriptor(ChannelType::Index, 1, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },
    false,  // hasAlpha
    true,   // isIndexed
    2,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    indexN_toStraight<1, BitOrder::MSBFirst>,
    indexN_fromStraight<1, BitOrder::MSBFirst>,
    indexN_expandIndex<1, BitOrder::MSBFirst>,
    nullptr,  // blendUnderStraight
    &Index1_LSB,  // siblingEndian
    nullptr,  // swapEndian
    pixel_format::detail::copyRowDDA_Bit<1, BitOrder::MSBFirst>,   // copyRowDDA
    pixel_format::detail::copyQuadDDA_Bit<1, BitOrder::MSBFirst>   // copyQuadDDA
};

const PixelFormatDescriptor Index1_LSB = {
    "Index1_LSB",
    1, 1, 8, 1, 1,
    { ChannelDescriptor(ChannelType::Index, 1, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },
    false, true, 2,
    BitOrder::LSBFirst,
    ByteOrder::Native,
    indexN_toStraight<1, BitOrder::LSBFirst>,
    indexN_fromStraight<1, BitOrder::LSBFirst>,
    indexN_expandIndex<1, BitOrder::LSBFirst>,
    nullptr, &Index1_MSB, nullptr,
    pixel_format::detail::copyRowDDA_Bit<1, BitOrder::LSBFirst>,
    pixel_format::detail::copyQuadDDA_Bit<1, BitOrder::LSBFirst>
};

const PixelFormatDescriptor Index2_MSB = {
    "Index2_MSB",
    2, 1, 4, 1, 1,
    { ChannelDescriptor(ChannelType::Index, 2, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },
    false, true, 4,
    BitOrder::MSBFirst,
    ByteOrder::Native,
    indexN_toStraight<2, BitOrder::MSBFirst>,
    indexN_fromStraight<2, BitOrder::MSBFirst>,
    indexN_expandIndex<2, BitOrder::MSBFirst>,
    nullptr, &Index2_LSB, nullptr,
    pixel_format::detail::copyRowDDA_Bit<2, BitOrder::MSBFirst>,
    pixel_format::detail::copyQuadDDA_Bit<2, BitOrder::MSBFirst>
};

const PixelFormatDescriptor Index2_LSB = {
    "Index2_LSB",
    2, 1, 4, 1, 1,
    { ChannelDescriptor(ChannelType::Index, 2, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },
    false, true, 4,
    BitOrder::LSBFirst,
    ByteOrder::Native,
    indexN_toStraight<2, BitOrder::LSBFirst>,
    indexN_fromStraight<2, BitOrder::LSBFirst>,
    indexN_expandIndex<2, BitOrder::LSBFirst>,
    nullptr, &Index2_MSB, nullptr,
    pixel_format::detail::copyRowDDA_Bit<2, BitOrder::LSBFirst>,
    pixel_format::detail::copyQuadDDA_Bit<2, BitOrder::LSBFirst>
};

const PixelFormatDescriptor Index4_MSB = {
    "Index4_MSB",
    4, 1, 2, 1, 1,
    { ChannelDescriptor(ChannelType::Index, 4, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },
    false, true, 16,
    BitOrder::MSBFirst,
    ByteOrder::Native,
    indexN_toStraight<4, BitOrder::MSBFirst>,
    indexN_fromStraight<4, BitOrder::MSBFirst>,
    indexN_expandIndex<4, BitOrder::MSBFirst>,
    nullptr, &Index4_LSB, nullptr,
    pixel_format::detail::copyRowDDA_Bit<4, BitOrder::MSBFirst>,
    pixel_format::detail::copyQuadDDA_Bit<4, BitOrder::MSBFirst>
};

const PixelFormatDescriptor Index4_LSB = {
    "Index4_LSB",
    4, 1, 2, 1, 1,
    { ChannelDescriptor(ChannelType::Index, 4, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },
    false, true, 16,
    BitOrder::LSBFirst,
    ByteOrder::Native,
    indexN_toStraight<4, BitOrder::LSBFirst>,
    indexN_fromStraight<4, BitOrder::LSBFirst>,
    indexN_expandIndex<4, BitOrder::LSBFirst>,
    nullptr, &Index4_MSB, nullptr,
    pixel_format::detail::copyRowDDA_Bit<4, BitOrder::LSBFirst>,
    pixel_format::detail::copyQuadDDA_Bit<4, BitOrder::LSBFirst>
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_INDEX_H
