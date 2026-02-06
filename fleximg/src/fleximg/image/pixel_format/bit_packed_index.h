#ifndef FLEXIMG_PIXEL_FORMAT_BIT_PACKED_INDEX_H
#define FLEXIMG_PIXEL_FORMAT_BIT_PACKED_INDEX_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
    extern const PixelFormatDescriptor Index1_MSB;
    extern const PixelFormatDescriptor Index1_LSB;
    extern const PixelFormatDescriptor Index2_MSB;
    extern const PixelFormatDescriptor Index2_LSB;
    extern const PixelFormatDescriptor Index4_MSB;
    extern const PixelFormatDescriptor Index4_LSB;
}

namespace PixelFormatIDs {
    inline const PixelFormatID Index1_MSB = &BuiltinFormats::Index1_MSB;
    inline const PixelFormatID Index1_LSB = &BuiltinFormats::Index1_LSB;
    inline const PixelFormatID Index2_MSB = &BuiltinFormats::Index2_MSB;
    inline const PixelFormatID Index2_LSB = &BuiltinFormats::Index2_LSB;
    inline const PixelFormatID Index4_MSB = &BuiltinFormats::Index4_MSB;
    inline const PixelFormatID Index4_LSB = &BuiltinFormats::Index4_LSB;
}

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

#include "../../core/format_metrics.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// ビット操作ヘルパー関数
// ========================================================================

namespace bit_packed_detail {

// ========================================================================
// unpackIndexBits: packed bytes → 8bit index array
// ========================================================================

template<int BitsPerPixel, BitOrder Order>
inline void unpackIndexBits(uint8_t* dst, const uint8_t* src, int pixelCount) {
    constexpr int PixelsPerByte = 8 / BitsPerPixel;
    constexpr uint8_t Mask = (1 << BitsPerPixel) - 1;

    int bytes = (pixelCount + PixelsPerByte - 1) / PixelsPerByte;
    for (int i = 0; i < bytes; ++i) {
        uint8_t b = src[i];
        int pixels_in_byte = (pixelCount >= PixelsPerByte) ? PixelsPerByte : pixelCount;
        for (int j = 0; j < pixels_in_byte; ++j) {
            if constexpr (Order == BitOrder::MSBFirst) {
                dst[j] = (b >> ((PixelsPerByte - 1 - j) * BitsPerPixel)) & Mask;
            } else {
                dst[j] = (b >> (j * BitsPerPixel)) & Mask;
            }
        }
        dst += PixelsPerByte;
        pixelCount -= PixelsPerByte;
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

} // namespace bit_packed_detail

// ========================================================================
// DDA転写関数（bit-packed専用）
// ========================================================================

// copyRowDDA: bit-packed → アンパック → 既存1bpp DDA
template<int BitsPerPixel, BitOrder Order>
static void indexN_copyRowDDA(
    uint8_t* dst,
    const uint8_t* srcData,
    int count,
    const DDAParam* param
) {
    // srcDataの必要範囲を計算
    // DDAでアクセスされる最大座標を求める
    int_fixed srcX = param->srcX;
    int_fixed incrX = param->incrX;
    int_fixed srcY = param->srcY;
    int_fixed incrY = param->incrY;

    // 開始座標と終了座標（ピクセル単位）
    int32_t minX = (srcX >> INT_FIXED_SHIFT);
    int32_t maxX = ((srcX + incrX * (count - 1)) >> INT_FIXED_SHIFT);
    int32_t minY = (srcY >> INT_FIXED_SHIFT);
    int32_t maxY = ((srcY + incrY * (count - 1)) >> INT_FIXED_SHIFT);

    if (minX > maxX) { int32_t tmp = minX; minX = maxX; maxX = tmp; }
    if (minY > maxY) { int32_t tmp = minY; minY = maxY; maxY = tmp; }

    // バイト境界に合わせて範囲を拡張
    // minXをバイト境界に切り下げ、maxXを切り上げ
    constexpr int PixelsPerUnit = 8 / BitsPerPixel;
    int32_t alignedMinX = (minX / PixelsPerUnit) * PixelsPerUnit;
    int32_t alignedMaxX = ((maxX + PixelsPerUnit) / PixelsPerUnit) * PixelsPerUnit - 1;

    // 必要なソース範囲のサイズ（バイト境界に合わせた）
    int32_t srcWidth = alignedMaxX - alignedMinX + 1;
    int32_t srcHeight = maxY - minY + 1;

    // アンパックバッファ（スタック）
    // 最大サイズを制限（例: 256x256ピクセル = 64KB）
    constexpr int MaxBufferPixels = 256 * 256;
    if (srcWidth * srcHeight > MaxBufferPixels) {
        // バッファサイズ超過: ピクセルごとにアンパック（遅いが安全）
        // PixelsPerUnitは既に定義済み
        uint8_t pixelBuf[PixelsPerUnit];

        for (int i = 0; i < count; ++i) {
            int32_t sx = (srcX >> INT_FIXED_SHIFT);
            int32_t sy = (srcY >> INT_FIXED_SHIFT);
            srcX += incrX;
            srcY += incrY;

            // ピクセルを含むバイトをアンパック
            int32_t byteIdx = sx / PixelsPerUnit;
            int32_t pixelInByte = sx % PixelsPerUnit;
            const uint8_t* srcRow = srcData + sy * param->srcStride;

            bit_packed_detail::unpackIndexBits<BitsPerPixel, Order>(
                pixelBuf, srcRow + byteIdx, PixelsPerUnit);

            dst[i] = pixelBuf[pixelInByte];
        }
        return;
    }

    // 小さい範囲: アンパック → 既存DDA
    uint8_t* unpackBuf = new uint8_t[static_cast<size_t>(srcWidth * srcHeight)];

    // 行ごとにアンパック（バイト境界から、PixelsPerUnitは既に定義済み）
    for (int32_t y = 0; y < srcHeight; ++y) {
        int32_t srcY_abs = minY + y;
        const uint8_t* srcRow = srcData + srcY_abs * param->srcStride;
        uint8_t* dstRow = unpackBuf + y * srcWidth;

        // alignedMinXからのバイトオフセット
        int32_t byteOffset = alignedMinX / PixelsPerUnit;
        bit_packed_detail::unpackIndexBits<BitsPerPixel, Order>(
            dstRow, srcRow + byteOffset, srcWidth);
    }

    // DDAパラメータを調整（アンパックバッファ座標系、alignedMinXを基準に）
    DDAParam adjustedParam = *param;
    adjustedParam.srcX = (param->srcX - (alignedMinX << INT_FIXED_SHIFT));
    adjustedParam.srcY = (param->srcY - (minY << INT_FIXED_SHIFT));
    adjustedParam.srcStride = srcWidth;

    // 既存の1bpp DDA関数を使用
    pixel_format::detail::copyRowDDA_1bpp(dst, unpackBuf, count, &adjustedParam);

    delete[] unpackBuf;
}

// copyQuadDDA: bit-packed → アンパック → 既存1bpp DDA
template<int BitsPerPixel, BitOrder Order>
static void indexN_copyQuadDDA(
    uint8_t* dst,
    const uint8_t* srcData,
    int count,
    const DDAParam* param
) {
    // copyRowDDAと同様のアプローチ
    int_fixed srcX = param->srcX;
    int_fixed incrX = param->incrX;
    int_fixed srcY = param->srcY;
    int_fixed incrY = param->incrY;

    // copyQuadDDAは2x2グリッドが必要なので+1
    int32_t minX = (srcX >> INT_FIXED_SHIFT);
    int32_t maxX = ((srcX + incrX * (count - 1)) >> INT_FIXED_SHIFT) + 1;
    int32_t minY = (srcY >> INT_FIXED_SHIFT);
    int32_t maxY = ((srcY + incrY * (count - 1)) >> INT_FIXED_SHIFT) + 1;

    if (minX > maxX) { int32_t tmp = minX; minX = maxX; maxX = tmp; }
    if (minY > maxY) { int32_t tmp = minY; minY = maxY; maxY = tmp; }

    // バイト境界に合わせて範囲を拡張（PixelsPerUnitは既に定義済み）
    constexpr int PixelsPerUnit = 8 / BitsPerPixel;
    int32_t alignedMinX = (minX / PixelsPerUnit) * PixelsPerUnit;
    int32_t alignedMaxX = ((maxX + PixelsPerUnit) / PixelsPerUnit) * PixelsPerUnit - 1;

    int32_t srcWidth = alignedMaxX - alignedMinX + 1;
    int32_t srcHeight = maxY - minY + 1;

    // バッファサイズ制限
    constexpr int MaxBufferPixels = 256 * 256;
    if (srcWidth * srcHeight > MaxBufferPixels) {
        // 大きすぎる: エラー処理またはフォールバック
        std::memset(dst, 0, static_cast<size_t>(count * 4));
        return;
    }

    // アンパックバッファ
    uint8_t* unpackBuf = new uint8_t[static_cast<size_t>(srcWidth * srcHeight)];

    // 行ごとにアンパック（バイト境界から）
    for (int32_t y = 0; y < srcHeight; ++y) {
        int32_t srcY_abs = minY + y;
        uint8_t* dstRow = unpackBuf + y * srcWidth;

        // 範囲外チェック（画像境界外の場合はゼロ埋め）
        if (srcY_abs < 0 || srcY_abs >= param->srcHeight) {
            std::memset(dstRow, 0, static_cast<size_t>(srcWidth));
            continue;
        }

        const uint8_t* srcRow = srcData + srcY_abs * param->srcStride;
        int32_t byteOffset = alignedMinX / PixelsPerUnit;

        // X方向の範囲チェック
        int32_t srcXStart = alignedMinX;
        int32_t srcXEnd = alignedMaxX;
        if (srcXStart < 0 || srcXEnd >= param->srcWidth) {
            // 境界外を含む場合、部分的にアンパック
            for (int32_t x = 0; x < srcWidth; ++x) {
                int32_t srcX_abs = alignedMinX + x;
                if (srcX_abs >= 0 && srcX_abs < param->srcWidth) {
                    int32_t srcByteIdx = srcX_abs / PixelsPerUnit;
                    int32_t pixelInByte = srcX_abs % PixelsPerUnit;
                    uint8_t pixelBuf[PixelsPerUnit];
                    bit_packed_detail::unpackIndexBits<BitsPerPixel, Order>(
                        pixelBuf, srcRow + srcByteIdx, PixelsPerUnit);
                    dstRow[x] = pixelBuf[pixelInByte];
                } else {
                    dstRow[x] = 0;
                }
            }
        } else {
            // 全て範囲内
            bit_packed_detail::unpackIndexBits<BitsPerPixel, Order>(
                dstRow, srcRow + byteOffset, srcWidth);
        }
    }

    // DDAパラメータ調整（alignedMinXを基準に）
    DDAParam adjustedParam = *param;
    adjustedParam.srcX = (param->srcX - (alignedMinX << INT_FIXED_SHIFT));
    adjustedParam.srcY = (param->srcY - (minY << INT_FIXED_SHIFT));
    adjustedParam.srcStride = srcWidth;
    // unpackBuf内での元画像の有効範囲（境界判定用）
    adjustedParam.srcWidth = param->srcWidth - alignedMinX;
    adjustedParam.srcHeight = param->srcHeight - minY;

    // 既存の1bpp DDA関数を使用
    pixel_format::detail::copyQuadDDA_1bpp(dst, unpackBuf, count, &adjustedParam);

    delete[] unpackBuf;
}

// ========================================================================
// 変換関数: expandIndex (パレット展開)
// ========================================================================

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
    // bitsPerPixel / 8 でバイト数を取得（getBytesPerPixelはまだ定義前）
    int_fast8_t paletteBpp = static_cast<int_fast8_t>((aux->paletteFormat->bitsPerPixel + 7) / 8);

    int remaining = pixelCount;
    while (remaining > 0) {
        int chunk = (remaining < ChunkSize) ? remaining : ChunkSize;

        // アンパック
        bit_packed_detail::unpackIndexBits<BitsPerPixel, Order>(indexBuf, srcPtr, chunk);

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

// ========================================================================
// 変換関数: toStraight (パレットなし時のグレースケール展開)
// ========================================================================

template<int BitsPerPixel, BitOrder Order>
static void indexN_toStraight(
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

    // スケーリング係数（インデックス値を0-255に拡張）
    constexpr int MaxIndex = (1 << BitsPerPixel) - 1;
    constexpr int Scale = 255 / MaxIndex;

    int remaining = pixelCount;
    while (remaining > 0) {
        int chunk = (remaining < ChunkSize) ? remaining : ChunkSize;

        // アンパック
        bit_packed_detail::unpackIndexBits<BitsPerPixel, Order>(indexBuf, srcPtr, chunk);

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

// ========================================================================
// 変換関数: fromStraight (RGBA8 → Index, 輝度計算 + 量子化)
// ========================================================================

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
// フォーマット定義
// ------------------------------------------------------------------------

namespace BuiltinFormats {

// Forward declarations for sibling references
extern const PixelFormatDescriptor Index1_LSB;
extern const PixelFormatDescriptor Index2_LSB;
extern const PixelFormatDescriptor Index4_LSB;

const PixelFormatDescriptor Index1_MSB = {
    "Index1_MSB",
    1,   // bitsPerPixel
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
    indexN_copyRowDDA<1, BitOrder::MSBFirst>,   // copyRowDDA
    indexN_copyQuadDDA<1, BitOrder::MSBFirst>   // copyQuadDDA
};

const PixelFormatDescriptor Index1_LSB = {
    "Index1_LSB",
    1, 8, 1, 1,
    { ChannelDescriptor(ChannelType::Index, 1, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },
    false, true, 2,
    BitOrder::LSBFirst,
    ByteOrder::Native,
    indexN_toStraight<1, BitOrder::LSBFirst>,
    indexN_fromStraight<1, BitOrder::LSBFirst>,
    indexN_expandIndex<1, BitOrder::LSBFirst>,
    nullptr, &Index1_MSB, nullptr,
    indexN_copyRowDDA<1, BitOrder::LSBFirst>,
    indexN_copyQuadDDA<1, BitOrder::LSBFirst>
};

const PixelFormatDescriptor Index2_MSB = {
    "Index2_MSB",
    2, 4, 1, 1,
    { ChannelDescriptor(ChannelType::Index, 2, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },
    false, true, 4,
    BitOrder::MSBFirst,
    ByteOrder::Native,
    indexN_toStraight<2, BitOrder::MSBFirst>,
    indexN_fromStraight<2, BitOrder::MSBFirst>,
    indexN_expandIndex<2, BitOrder::MSBFirst>,
    nullptr, &Index2_LSB, nullptr,
    indexN_copyRowDDA<2, BitOrder::MSBFirst>,
    indexN_copyQuadDDA<2, BitOrder::MSBFirst>
};

const PixelFormatDescriptor Index2_LSB = {
    "Index2_LSB",
    2, 4, 1, 1,
    { ChannelDescriptor(ChannelType::Index, 2, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },
    false, true, 4,
    BitOrder::LSBFirst,
    ByteOrder::Native,
    indexN_toStraight<2, BitOrder::LSBFirst>,
    indexN_fromStraight<2, BitOrder::LSBFirst>,
    indexN_expandIndex<2, BitOrder::LSBFirst>,
    nullptr, &Index2_MSB, nullptr,
    indexN_copyRowDDA<2, BitOrder::LSBFirst>,
    indexN_copyQuadDDA<2, BitOrder::LSBFirst>
};

const PixelFormatDescriptor Index4_MSB = {
    "Index4_MSB",
    4, 2, 1, 1,
    { ChannelDescriptor(ChannelType::Index, 4, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },
    false, true, 16,
    BitOrder::MSBFirst,
    ByteOrder::Native,
    indexN_toStraight<4, BitOrder::MSBFirst>,
    indexN_fromStraight<4, BitOrder::MSBFirst>,
    indexN_expandIndex<4, BitOrder::MSBFirst>,
    nullptr, &Index4_LSB, nullptr,
    indexN_copyRowDDA<4, BitOrder::MSBFirst>,
    indexN_copyQuadDDA<4, BitOrder::MSBFirst>
};

const PixelFormatDescriptor Index4_LSB = {
    "Index4_LSB",
    4, 2, 1, 1,
    { ChannelDescriptor(ChannelType::Index, 4, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },
    false, true, 16,
    BitOrder::LSBFirst,
    ByteOrder::Native,
    indexN_toStraight<4, BitOrder::LSBFirst>,
    indexN_fromStraight<4, BitOrder::LSBFirst>,
    indexN_expandIndex<4, BitOrder::LSBFirst>,
    nullptr, &Index4_MSB, nullptr,
    indexN_copyRowDDA<4, BitOrder::LSBFirst>,
    indexN_copyQuadDDA<4, BitOrder::LSBFirst>
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_BIT_PACKED_INDEX_H
