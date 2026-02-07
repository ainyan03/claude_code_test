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

// ========================================================================
// ビット単位アクセスヘルパー（LovyanGFXスタイル）
// ========================================================================

// 指定座標のピクセルを bit-packed データから直接読み取り
template<int BitsPerPixel, BitOrder Order>
inline uint8_t readPixelDirect(const uint8_t* srcData, int32_t x, int32_t y, int32_t stride) {
    constexpr uint8_t Mask = (1 << BitsPerPixel) - 1;

    // ビット単位のオフセット計算
    int32_t bitOffset = (y * stride * 8) + (x * BitsPerPixel);
    int32_t byteIdx = bitOffset >> 3;
    int32_t bitPos = bitOffset & 7;

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
// DDA転写関数（bit-packed専用、ピクセル単位直接アクセス）
// ========================================================================

// copyRowDDA: ピクセル単位で bit-packed から直接読み取り
template<int BitsPerPixel, BitOrder Order>
static void indexN_copyRowDDA(
    uint8_t* dst,
    const uint8_t* srcData,
    int count,
    const DDAParam* param
) {
    // LovyanGFXスタイル: ピクセル単位で直接読み取り
    // 境界チェックは呼び出し側が保証（calcScanlineRange）
    int_fixed srcX = param->srcX;
    int_fixed srcY = param->srcY;
    const int_fixed incrX = param->incrX;
    const int_fixed incrY = param->incrY;
    const int32_t srcStride = param->srcStride;

    for (int i = 0; i < count; ++i) {
        int32_t sx = srcX >> INT_FIXED_SHIFT;
        int32_t sy = srcY >> INT_FIXED_SHIFT;
        srcX += incrX;
        srcY += incrY;

#ifdef FLEXIMG_DEBUG
        // デバッグビルドのみ: width/heightが設定されている場合は境界チェック
        if (param->srcWidth > 0 && param->srcHeight > 0) {
            FLEXIMG_REQUIRE(sx >= 0 && sx < param->srcWidth &&
                          sy >= 0 && sy < param->srcHeight,
                          "DDA out of bounds access");
        }
#endif

        dst[i] = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(
            srcData, sx, sy, srcStride);
    }
}

// copyQuadDDA: 2x2グリッドをピクセル単位で直接読み取り
template<int BitsPerPixel, BitOrder Order>
static void indexN_copyQuadDDA(
    uint8_t* dst,
    const uint8_t* srcData,
    int count,
    const DDAParam* param
) {
    // LovyanGFXスタイル: 2x2グリッドを直接読み取り
    int_fixed srcX = param->srcX;
    int_fixed srcY = param->srcY;
    const int_fixed incrX = param->incrX;
    const int_fixed incrY = param->incrY;
    const int32_t srcWidth = param->srcWidth;
    const int32_t srcHeight = param->srcHeight;
    const int32_t srcStride = param->srcStride;
    BilinearWeightXY* weightsXY = param->weightsXY;
    uint8_t* edgeFlags = param->edgeFlags;

    for (int i = 0; i < count; ++i) {
        int32_t sx = srcX >> INT_FIXED_SHIFT;
        int32_t sy = srcY >> INT_FIXED_SHIFT;

        // バイリニア補間用の重み計算
        if (weightsXY) {
            weightsXY[i].fx = static_cast<uint8_t>(static_cast<uint32_t>(srcX) >> (INT_FIXED_SHIFT - 8));
            weightsXY[i].fy = static_cast<uint8_t>(static_cast<uint32_t>(srcY) >> (INT_FIXED_SHIFT - 8));
        }

        srcX += incrX;
        srcY += incrY;

        // 境界チェック（2x2グリッドが全て範囲内か）
        bool x_valid = (sx >= 0 && sx + 1 < srcWidth);
        bool y_valid = (sy >= 0 && sy + 1 < srcHeight);

        if (x_valid && y_valid) {
            // 全て範囲内: 2x2グリッドを読み取り
            dst[0] = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(srcData, sx,     sy,     srcStride);
            dst[1] = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(srcData, sx + 1, sy,     srcStride);
            dst[2] = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(srcData, sx,     sy + 1, srcStride);
            dst[3] = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(srcData, sx + 1, sy + 1, srcStride);
            if (edgeFlags) edgeFlags[i] = 0;
        } else {
            // 境界外を含む: copyQuadDDA_bpp と同じロジック
            uint8_t flag_x = EdgeFade_Right;
            uint8_t flag_y = EdgeFade_Bottom;

            // 座標をクランプ
            if (sx < 0) {
                sx = 0;
                flag_x = EdgeFade_Left;
            }
            if (sy < 0) {
                sy = 0;
                flag_y = EdgeFade_Top;
            }

            // 基準ピクセル（クランプした座標）を読む
            uint8_t val = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(
                srcData, sx, sy, srcStride);

            // 全て基準値で初期化
            dst[0] = val;
            dst[1] = val;
            dst[2] = val;

            // x方向が有効なら右隣を読む
            if (x_valid) {
                val = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(
                    srcData, sx + 1, sy, srcStride);
                dst[1] = val;
                flag_x = 0;
            } else if (y_valid) {
                // x方向無効でy方向有効なら下隣を読む
                val = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(
                    srcData, sx, sy + 1, srcStride);
                dst[2] = val;
                flag_y = 0;
            }

            dst[3] = val;  // 最後に読んだ値

            if (edgeFlags) edgeFlags[i] = flag_x + flag_y;
        }

        dst += 4;
    }
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
    // パレットフォーマットのバイト数を取得
    int_fast8_t paletteBpp = static_cast<int_fast8_t>(aux->paletteFormat->bytesPerPixel);

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
    indexN_copyRowDDA<1, BitOrder::MSBFirst>,   // copyRowDDA
    indexN_copyQuadDDA<1, BitOrder::MSBFirst>   // copyQuadDDA
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
    indexN_copyRowDDA<1, BitOrder::LSBFirst>,
    indexN_copyQuadDDA<1, BitOrder::LSBFirst>
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
    indexN_copyRowDDA<2, BitOrder::MSBFirst>,
    indexN_copyQuadDDA<2, BitOrder::MSBFirst>
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
    indexN_copyRowDDA<2, BitOrder::LSBFirst>,
    indexN_copyQuadDDA<2, BitOrder::LSBFirst>
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
    indexN_copyRowDDA<4, BitOrder::MSBFirst>,
    indexN_copyQuadDDA<4, BitOrder::MSBFirst>
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
    indexN_copyRowDDA<4, BitOrder::LSBFirst>,
    indexN_copyQuadDDA<4, BitOrder::LSBFirst>
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_BIT_PACKED_INDEX_H
