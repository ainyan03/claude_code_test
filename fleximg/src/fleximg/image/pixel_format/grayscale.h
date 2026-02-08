#ifndef FLEXIMG_PIXEL_FORMAT_GRAYSCALE_H
#define FLEXIMG_PIXEL_FORMAT_GRAYSCALE_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
    // 8-bit Grayscale
    extern const PixelFormatDescriptor Grayscale8;

    // Bit-packed Grayscale formats
    extern const PixelFormatDescriptor Grayscale1_MSB;
    extern const PixelFormatDescriptor Grayscale1_LSB;
    extern const PixelFormatDescriptor Grayscale2_MSB;
    extern const PixelFormatDescriptor Grayscale2_LSB;
    extern const PixelFormatDescriptor Grayscale4_MSB;
    extern const PixelFormatDescriptor Grayscale4_LSB;
}

namespace PixelFormatIDs {
    inline const PixelFormatID Grayscale8 = &BuiltinFormats::Grayscale8;

    inline const PixelFormatID Grayscale1_MSB = &BuiltinFormats::Grayscale1_MSB;
    inline const PixelFormatID Grayscale1_LSB = &BuiltinFormats::Grayscale1_LSB;
    inline const PixelFormatID Grayscale2_MSB = &BuiltinFormats::Grayscale2_MSB;
    inline const PixelFormatID Grayscale2_LSB = &BuiltinFormats::Grayscale2_LSB;
    inline const PixelFormatID Grayscale4_MSB = &BuiltinFormats::Grayscale4_MSB;
    inline const PixelFormatID Grayscale4_LSB = &BuiltinFormats::Grayscale4_LSB;
}

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

#include "../../core/format_metrics.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// Grayscale8: 単一輝度チャンネル ↔ RGBA8_Straight 変換
// ========================================================================

// Grayscale8 → RGBA8_Straight（L → R=G=B=L, A=255）
static void grayscale8_toStraight(void* dst, const void* src, size_t pixelCount, const PixelAuxInfo*) {
    FLEXIMG_FMT_METRICS(Grayscale8, ToStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < pixelCount; ++i) {
        uint8_t lum = s[i];
        d[i*4 + 0] = lum;   // R
        d[i*4 + 1] = lum;   // G
        d[i*4 + 2] = lum;   // B
        d[i*4 + 3] = 255;   // A
    }
}

// RGBA8_Straight → Grayscale8（BT.601 輝度計算）
static void grayscale8_fromStraight(void* dst, const void* src, size_t pixelCount, const PixelAuxInfo*) {
    FLEXIMG_FMT_METRICS(Grayscale8, FromStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);

    // BT.601: Y = 0.299*R + 0.587*G + 0.114*B
    // 整数近似: (77*R + 150*G + 29*B + 128) >> 8

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
// Grayscale8 フォーマット定義
// ------------------------------------------------------------------------

namespace BuiltinFormats {

const PixelFormatDescriptor Grayscale8 = {
    "Grayscale8",
    grayscale8_toStraight,
    grayscale8_fromStraight,
    nullptr,  // expandIndex
    nullptr,  // blendUnderStraight
    nullptr,  // siblingEndian
    nullptr,  // swapEndian
    pixel_format::detail::copyRowDDA_1Byte,  // copyRowDDA
    pixel_format::detail::copyQuadDDA_1Byte, // copyQuadDDA
    BitOrder::MSBFirst,
    ByteOrder::Native,
    0,      // maxPaletteSize
    8,   // bitsPerPixel
    1,   // bytesPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    1,   // channelCount
    false,  // hasAlpha
    false,  // isIndexed
};

} // namespace BuiltinFormats

// ========================================================================
// ビット操作ヘルパー関数（bit-packed Grayscale/Index共用）
// ========================================================================

namespace bit_packed_detail {

// ========================================================================
// unpackIndexBits: packed bytes → 8bit value array
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
// packIndexBits: 8bit value array → packed bytes
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
// GrayscaleN: bit-packed Grayscale ↔ RGBA8_Straight 変換
// ========================================================================

// GrayscaleN → RGBA8_Straight（bit-packed → RGBA8）
// 末尾詰め方式: 出力バッファ(RGBA8=4byte/pixel)の末尾にGrayscale8データをunpackし、
// スケーリング後に grayscale8_toStraight でin-place展開する
template<int BitsPerPixel, BitOrder Order>
static void grayscaleN_toStraight(
    void* __restrict__ dst,
    const void* __restrict__ src,
    size_t pixelCount,
    const PixelAuxInfo* aux
) {
    FLEXIMG_FMT_METRICS(GrayscaleN, ToStraight, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);

    // 末尾詰め: RGBA8出力(4byte/pixel)の後方にGrayscale8(1byte/pixel)をunpack
    uint8_t* grayData = d + static_cast<size_t>(pixelCount) * 3;

    const uint8_t pixelOffsetInByte = aux ? aux->pixelOffsetInByte : 0;
    bit_packed_detail::unpackIndexBits<BitsPerPixel, Order>(
        grayData, s, pixelCount, pixelOffsetInByte);

    // スケーリング: GrayscaleN値(0-MaxVal) → 0-255 (Grayscale8相当)
    constexpr int MaxVal = (1 << BitsPerPixel) - 1;
    constexpr int Scale = 255 / MaxVal;
    for (size_t i = 0; i < pixelCount; ++i) {
        grayData[i] = static_cast<uint8_t>(grayData[i] * Scale);
    }

    // grayscale8_toStraight に委譲（in-place: grayData → d）
    grayscale8_toStraight(dst, grayData, pixelCount, nullptr);
}

// RGBA8_Straight → GrayscaleN（BT.601輝度計算 + 量子化 → bit-packed）
template<int BitsPerPixel, BitOrder Order>
static void grayscaleN_fromStraight(
    void* __restrict__ dst,
    const void* __restrict__ src,
    size_t pixelCount,
    const PixelAuxInfo*
) {
    FLEXIMG_FMT_METRICS(GrayscaleN, FromStraight, pixelCount);
    constexpr size_t MaxPixelsPerByte = 8 / BitsPerPixel;
    constexpr size_t ChunkSize = 64;
    uint8_t grayBuf[ChunkSize];

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
            grayBuf[i] = lum >> QuantizeShift;
        }

        // パック
        bit_packed_detail::packIndexBits<BitsPerPixel, Order>(dstPtr, grayBuf, chunk);

        srcPtr += chunk * 4;
        dstPtr += (chunk + MaxPixelsPerByte - 1) / MaxPixelsPerByte;
        remaining -= chunk;
    }
}

// ------------------------------------------------------------------------
// Bit-packed Grayscale Formats フォーマット定義
// ------------------------------------------------------------------------

namespace BuiltinFormats {

// Forward declarations for sibling references
extern const PixelFormatDescriptor Grayscale1_LSB;
extern const PixelFormatDescriptor Grayscale2_LSB;
extern const PixelFormatDescriptor Grayscale4_LSB;

const PixelFormatDescriptor Grayscale1_MSB = {
    "Grayscale1_MSB",
    grayscaleN_toStraight<1, BitOrder::MSBFirst>,
    grayscaleN_fromStraight<1, BitOrder::MSBFirst>,
    nullptr,  // expandIndex
    nullptr,  // blendUnderStraight
    &Grayscale1_LSB,  // siblingEndian
    nullptr,  // swapEndian
    pixel_format::detail::copyRowDDA_Bit<1, BitOrder::MSBFirst>,   // copyRowDDA
    pixel_format::detail::copyQuadDDA_Bit<1, BitOrder::MSBFirst>,  // copyQuadDDA
    BitOrder::MSBFirst,
    ByteOrder::Native,
    0,      // maxPaletteSize
    1,   // bitsPerPixel
    1,   // bytesPerPixel
    8,   // pixelsPerUnit
    1,   // bytesPerUnit
    1,   // channelCount
    false,  // hasAlpha
    false,  // isIndexed
};

const PixelFormatDescriptor Grayscale1_LSB = {
    "Grayscale1_LSB",
    grayscaleN_toStraight<1, BitOrder::LSBFirst>,
    grayscaleN_fromStraight<1, BitOrder::LSBFirst>,
    nullptr, nullptr, &Grayscale1_MSB, nullptr,
    pixel_format::detail::copyRowDDA_Bit<1, BitOrder::LSBFirst>,
    pixel_format::detail::copyQuadDDA_Bit<1, BitOrder::LSBFirst>,
    BitOrder::LSBFirst,
    ByteOrder::Native,
    0, 1, 1, 8, 1, 1,
    false, false,
};

const PixelFormatDescriptor Grayscale2_MSB = {
    "Grayscale2_MSB",
    grayscaleN_toStraight<2, BitOrder::MSBFirst>,
    grayscaleN_fromStraight<2, BitOrder::MSBFirst>,
    nullptr, nullptr, &Grayscale2_LSB, nullptr,
    pixel_format::detail::copyRowDDA_Bit<2, BitOrder::MSBFirst>,
    pixel_format::detail::copyQuadDDA_Bit<2, BitOrder::MSBFirst>,
    BitOrder::MSBFirst,
    ByteOrder::Native,
    0, 2, 1, 4, 1, 1,
    false, false,
};

const PixelFormatDescriptor Grayscale2_LSB = {
    "Grayscale2_LSB",
    grayscaleN_toStraight<2, BitOrder::LSBFirst>,
    grayscaleN_fromStraight<2, BitOrder::LSBFirst>,
    nullptr, nullptr, &Grayscale2_MSB, nullptr,
    pixel_format::detail::copyRowDDA_Bit<2, BitOrder::LSBFirst>,
    pixel_format::detail::copyQuadDDA_Bit<2, BitOrder::LSBFirst>,
    BitOrder::LSBFirst,
    ByteOrder::Native,
    0, 2, 1, 4, 1, 1,
    false, false,
};

const PixelFormatDescriptor Grayscale4_MSB = {
    "Grayscale4_MSB",
    grayscaleN_toStraight<4, BitOrder::MSBFirst>,
    grayscaleN_fromStraight<4, BitOrder::MSBFirst>,
    nullptr, nullptr, &Grayscale4_LSB, nullptr,
    pixel_format::detail::copyRowDDA_Bit<4, BitOrder::MSBFirst>,
    pixel_format::detail::copyQuadDDA_Bit<4, BitOrder::MSBFirst>,
    BitOrder::MSBFirst,
    ByteOrder::Native,
    0, 4, 1, 2, 1, 1,
    false, false,
};

const PixelFormatDescriptor Grayscale4_LSB = {
    "Grayscale4_LSB",
    grayscaleN_toStraight<4, BitOrder::LSBFirst>,
    grayscaleN_fromStraight<4, BitOrder::LSBFirst>,
    nullptr, nullptr, &Grayscale4_MSB, nullptr,
    pixel_format::detail::copyRowDDA_Bit<4, BitOrder::LSBFirst>,
    pixel_format::detail::copyQuadDDA_Bit<4, BitOrder::LSBFirst>,
    BitOrder::LSBFirst,
    ByteOrder::Native,
    0, 4, 1, 2, 1, 1,
    false, false,
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_GRAYSCALE_H
