#include "pixel_format.h"
#include <cstring>
#include <algorithm>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマットの変換関数
// 標準フォーマット: RGBA8_Straight（8bit RGBA、ストレートアルファ）
// ========================================================================

// RGBA8_Straight: 標準フォーマットなのでコピー
static void rgba8Straight_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    std::memcpy(dst, src, pixelCount * 4);
}

static void rgba8Straight_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    std::memcpy(dst, src, pixelCount * 4);
}

// ========================================================================
// RGBA16_Premultiplied: 16bit Premultiplied ↔ 8bit Straight 変換
// ========================================================================
// 変換方式: A_tmp = A8 + 1 を使用
// - Forward変換: 除算ゼロ（乗算のみ）
// - Reverse変換: 除数が1-256に限定（テーブル化やSIMD最適化が容易）
// - A8=0 でもRGB情報を保持

static void rgba16Premul_toStandard(const void* src, uint8_t* dst, int pixelCount) {
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
// RGB565_LE: 16bit RGB (Little Endian)
// ========================================================================

static void rgb565le_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    const uint16_t* s = static_cast<const uint16_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        uint16_t pixel = s[i];
        uint8_t r5 = (pixel >> 11) & 0x1F;
        uint8_t g6 = (pixel >> 5) & 0x3F;
        uint8_t b5 = pixel & 0x1F;

        // ビット拡張（5bit/6bit → 8bit）
        dst[i*4 + 0] = (r5 << 3) | (r5 >> 2);
        dst[i*4 + 1] = (g6 << 2) | (g6 >> 4);
        dst[i*4 + 2] = (b5 << 3) | (b5 >> 2);
        dst[i*4 + 3] = 255;
    }
}

static void rgb565le_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    uint16_t* d = static_cast<uint16_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t r = src[i*4 + 0];
        uint8_t g = src[i*4 + 1];
        uint8_t b = src[i*4 + 2];
        d[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
}

// ========================================================================
// RGB565_BE: 16bit RGB (Big Endian)
// ========================================================================

static void rgb565be_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        // ビッグエンディアン: 上位バイトが先
        uint16_t pixel = (static_cast<uint16_t>(s[i*2]) << 8) | s[i*2 + 1];
        uint8_t r5 = (pixel >> 11) & 0x1F;
        uint8_t g6 = (pixel >> 5) & 0x3F;
        uint8_t b5 = pixel & 0x1F;

        dst[i*4 + 0] = (r5 << 3) | (r5 >> 2);
        dst[i*4 + 1] = (g6 << 2) | (g6 >> 4);
        dst[i*4 + 2] = (b5 << 3) | (b5 >> 2);
        dst[i*4 + 3] = 255;
    }
}

static void rgb565be_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t r = src[i*4 + 0];
        uint8_t g = src[i*4 + 1];
        uint8_t b = src[i*4 + 2];
        uint16_t pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        // ビッグエンディアン: 上位バイトを先に
        d[i*2] = pixel >> 8;
        d[i*2 + 1] = pixel & 0xFF;
    }
}

// ========================================================================
// RGB332: 8bit RGB (3-3-2)
// ========================================================================

static void rgb332_toStandard(const void* src, uint8_t* dst, int pixelCount) {
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
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t r = src[i*4 + 0];
        uint8_t g = src[i*4 + 1];
        uint8_t b = src[i*4 + 2];
        d[i] = (r & 0xE0) | ((g >> 5) << 2) | (b >> 6);
    }
}

// ========================================================================
// RGB888: 24bit RGB (mem[0]=R, mem[1]=G, mem[2]=B)
// ========================================================================

static void rgb888_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        dst[i*4 + 0] = s[i*3 + 0];  // R
        dst[i*4 + 1] = s[i*3 + 1];  // G
        dst[i*4 + 2] = s[i*3 + 2];  // B
        dst[i*4 + 3] = 255;          // A
    }
}

static void rgb888_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        d[i*3 + 0] = src[i*4 + 0];  // R
        d[i*3 + 1] = src[i*4 + 1];  // G
        d[i*3 + 2] = src[i*4 + 2];  // B
    }
}

// ========================================================================
// BGR888: 24bit BGR (mem[0]=B, mem[1]=G, mem[2]=R)
// ========================================================================

static void bgr888_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        dst[i*4 + 0] = s[i*3 + 2];  // R (src の B 位置)
        dst[i*4 + 1] = s[i*3 + 1];  // G
        dst[i*4 + 2] = s[i*3 + 0];  // B (src の R 位置)
        dst[i*4 + 3] = 255;
    }
}

static void bgr888_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        d[i*3 + 0] = src[i*4 + 2];  // B
        d[i*3 + 1] = src[i*4 + 1];  // G
        d[i*3 + 2] = src[i*4 + 0];  // R
    }
}

// ========================================================================
// 組み込みフォーマット定義
// ========================================================================

namespace BuiltinFormats {

const PixelFormatDescriptor RGBA16_Premultiplied = {
    "RGBA16_Premultiplied",
    64,  // bitsPerPixel
    1,   // pixelsPerUnit
    8,   // bytesPerUnit
    { ChannelDescriptor(16, 0), ChannelDescriptor(16, 0),
      ChannelDescriptor(16, 0), ChannelDescriptor(16, 0) },  // R, G, B, A
    true,   // hasAlpha
    true,   // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    rgba16Premul_toStandard,
    rgba16Premul_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr   // fromStandardIndexed
};

const PixelFormatDescriptor RGBA8_Straight = {
    "RGBA8_Straight",
    32,  // bitsPerPixel
    1,   // pixelsPerUnit
    4,   // bytesPerUnit
    { ChannelDescriptor(8, 0), ChannelDescriptor(8, 0),
      ChannelDescriptor(8, 0), ChannelDescriptor(8, 0) },  // R, G, B, A
    true,   // hasAlpha
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    rgba8Straight_toStandard,
    rgba8Straight_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr   // fromStandardIndexed
};

const PixelFormatDescriptor RGB565_LE = {
    "RGB565_LE",
    16,  // bitsPerPixel
    1,   // pixelsPerUnit
    2,   // bytesPerUnit
    { ChannelDescriptor(5, 11), ChannelDescriptor(6, 5),
      ChannelDescriptor(5, 0), ChannelDescriptor(0, 0) },  // R, G, B, (no A)
    false,  // hasAlpha
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::LittleEndian,
    rgb565le_toStandard,
    rgb565le_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr   // fromStandardIndexed
};

const PixelFormatDescriptor RGB565_BE = {
    "RGB565_BE",
    16,  // bitsPerPixel
    1,   // pixelsPerUnit
    2,   // bytesPerUnit
    { ChannelDescriptor(5, 11), ChannelDescriptor(6, 5),
      ChannelDescriptor(5, 0), ChannelDescriptor(0, 0) },  // R, G, B, (no A)
    false,  // hasAlpha
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::BigEndian,
    rgb565be_toStandard,
    rgb565be_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr   // fromStandardIndexed
};

const PixelFormatDescriptor RGB332 = {
    "RGB332",
    8,   // bitsPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    { ChannelDescriptor(3, 5), ChannelDescriptor(3, 2),
      ChannelDescriptor(2, 0), ChannelDescriptor(0, 0) },  // R, G, B, (no A)
    false,  // hasAlpha
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    rgb332_toStandard,
    rgb332_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr   // fromStandardIndexed
};

const PixelFormatDescriptor RGB888 = {
    "RGB888",
    24,  // bitsPerPixel
    1,   // pixelsPerUnit
    3,   // bytesPerUnit
    { ChannelDescriptor(8, 16), ChannelDescriptor(8, 8),
      ChannelDescriptor(8, 0), ChannelDescriptor(0, 0) },  // R, G, B, (no A)
    false,  // hasAlpha
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    rgb888_toStandard,
    rgb888_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr   // fromStandardIndexed
};

const PixelFormatDescriptor BGR888 = {
    "BGR888",
    24,  // bitsPerPixel
    1,   // pixelsPerUnit
    3,   // bytesPerUnit
    { ChannelDescriptor(8, 0), ChannelDescriptor(8, 8),
      ChannelDescriptor(8, 16), ChannelDescriptor(0, 0) },  // R, G, B, (no A)
    false,  // hasAlpha
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    bgr888_toStandard,
    bgr888_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr   // fromStandardIndexed
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE
