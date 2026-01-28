#ifndef FLEXIMG_PIXEL_FORMAT_RGB888_H
#define FLEXIMG_PIXEL_FORMAT_RGB888_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
    extern const PixelFormatDescriptor RGB888;
    extern const PixelFormatDescriptor BGR888;
}

namespace PixelFormatIDs {
    inline const PixelFormatID RGB888 = &BuiltinFormats::RGB888;
    inline const PixelFormatID BGR888 = &BuiltinFormats::BGR888;
}

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

#include "../../core/format_metrics.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// RGB888: 24bit RGB (mem[0]=R, mem[1]=G, mem[2]=B)
// ========================================================================

static void rgb888_toStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB888, ToStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        d[0] = s[0];  // R
        d[1] = s[1];  // G
        d[2] = s[2];  // B
        d[3] = 255;   // A
        s += 3;
        d += 4;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        d[0] = s[0];  d[1] = s[1];  d[2] = s[2];  d[3] = 255;
        d[4] = s[3];  d[5] = s[4];  d[6] = s[5];  d[7] = 255;
        d[8] = s[6];  d[9] = s[7];  d[10] = s[8];  d[11] = 255;
        d[12] = s[9];  d[13] = s[10];  d[14] = s[11];  d[15] = 255;
        s += 12;
        d += 16;
    }
}

static void rgb888_fromStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB888, FromStraight, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        d[0] = s[0];  // R
        d[1] = s[1];  // G
        d[2] = s[2];  // B
        s += 4;
        d += 3;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        d[0] = s[0];  d[1] = s[1];  d[2] = s[2];
        d[3] = s[4];  d[4] = s[5];  d[5] = s[6];
        d[6] = s[8];  d[7] = s[9];  d[8] = s[10];
        d[9] = s[12];  d[10] = s[13];  d[11] = s[14];
        s += 16;
        d += 12;
    }
}

// ========================================================================
// BGR888: 24bit BGR (mem[0]=B, mem[1]=G, mem[2]=R)
// ========================================================================

static void bgr888_toStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(BGR888, ToStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        d[0] = s[2];  // R (src の B 位置)
        d[1] = s[1];  // G
        d[2] = s[0];  // B (src の R 位置)
        d[3] = 255;
        s += 3;
        d += 4;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        d[0] = s[2];  d[1] = s[1];  d[2] = s[0];  d[3] = 255;
        d[4] = s[5];  d[5] = s[4];  d[6] = s[3];  d[7] = 255;
        d[8] = s[8];  d[9] = s[7];  d[10] = s[6];  d[11] = 255;
        d[12] = s[11];  d[13] = s[10];  d[14] = s[9];  d[15] = 255;
        s += 12;
        d += 16;
    }
}

static void bgr888_fromStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(BGR888, FromStraight, pixelCount);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);

    // 端数処理（1〜3ピクセル）
    int remainder = pixelCount & 3;
    while (remainder--) {
        d[0] = s[2];  // B
        d[1] = s[1];  // G
        d[2] = s[0];  // R
        s += 4;
        d += 3;
    }

    // 4ピクセル単位でループ
    pixelCount >>= 2;
    while (pixelCount--) {
        d[0] = s[2];  d[1] = s[1];  d[2] = s[0];
        d[3] = s[6];  d[4] = s[5];  d[5] = s[4];
        d[6] = s[10];  d[7] = s[9];  d[8] = s[8];
        d[9] = s[14];  d[10] = s[13];  d[11] = s[12];
        s += 16;
        d += 12;
    }
}

// ========================================================================
// エンディアン・バイトスワップ関数
// ========================================================================

// 24bit用チャンネルスワップ（RGB888 ↔ BGR888）
static void swap24(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    const uint8_t* srcPtr = static_cast<const uint8_t*>(src);
    uint8_t* dstPtr = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; ++i) {
        int idx = i * 3;
        dstPtr[idx + 0] = srcPtr[idx + 2];
        dstPtr[idx + 1] = srcPtr[idx + 1];
        dstPtr[idx + 2] = srcPtr[idx + 0];
    }
}

// ------------------------------------------------------------------------
// フォーマット定義
// ------------------------------------------------------------------------

namespace BuiltinFormats {

// Forward declarations for sibling references
extern const PixelFormatDescriptor BGR888;

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
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    rgb888_toStraight,
    rgb888_fromStraight,
    nullptr,  // expandIndex
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
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    bgr888_toStraight,
    bgr888_fromStraight,
    nullptr,  // expandIndex
    nullptr,  // blendUnderStraight
    &RGB888,  // siblingEndian
    swap24    // swapEndian
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_RGB888_H
