#include "pixel_format_registry.h"
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

// RGBA16_Premultiplied → RGBA8_Straight（直接変換）
static void directConvert_rgba16PremulToRgba8Straight(const void* src, void* dst, int pixelCount) {
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

// RGBA8_Straight → RGBA16_Premultiplied（直接変換）
static void directConvert_rgba8StraightToRgba16Premul(const void* src, void* dst, int pixelCount) {
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

static PixelFormatDescriptor createRGBA16_Premultiplied() {
    PixelFormatDescriptor desc;
    desc.id = PixelFormatIDs::RGBA16_Premultiplied;
    desc.name = "RGBA16_Premultiplied";
    desc.bitsPerPixel = 64;
    desc.pixelsPerUnit = 1;
    desc.bytesPerUnit = 8;
    desc.channels[0] = ChannelDescriptor(16, 0);  // R
    desc.channels[1] = ChannelDescriptor(16, 0);  // G
    desc.channels[2] = ChannelDescriptor(16, 0);  // B
    desc.channels[3] = ChannelDescriptor(16, 0);  // A
    desc.hasAlpha = true;
    desc.isPremultiplied = true;
    desc.isIndexed = false;
    desc.maxPaletteSize = 0;
    desc.bitOrder = BitOrder::MSBFirst;
    desc.byteOrder = ByteOrder::Native;
    desc.toStandard = rgba16Premul_toStandard;
    desc.fromStandard = rgba16Premul_fromStandard;
    return desc;
}

static PixelFormatDescriptor createRGBA8_Straight() {
    PixelFormatDescriptor desc;
    desc.id = PixelFormatIDs::RGBA8_Straight;
    desc.name = "RGBA8_Straight";
    desc.bitsPerPixel = 32;
    desc.pixelsPerUnit = 1;
    desc.bytesPerUnit = 4;
    desc.channels[0] = ChannelDescriptor(8, 0);   // R
    desc.channels[1] = ChannelDescriptor(8, 0);   // G
    desc.channels[2] = ChannelDescriptor(8, 0);   // B
    desc.channels[3] = ChannelDescriptor(8, 0);   // A
    desc.hasAlpha = true;
    desc.isPremultiplied = false;
    desc.isIndexed = false;
    desc.maxPaletteSize = 0;
    desc.bitOrder = BitOrder::MSBFirst;
    desc.byteOrder = ByteOrder::Native;
    desc.toStandard = rgba8Straight_toStandard;
    desc.fromStandard = rgba8Straight_fromStandard;
    return desc;
}

static PixelFormatDescriptor createRGB565_LE() {
    PixelFormatDescriptor desc;
    desc.id = PixelFormatIDs::RGB565_LE;
    desc.name = "RGB565_LE";
    desc.bitsPerPixel = 16;
    desc.pixelsPerUnit = 1;
    desc.bytesPerUnit = 2;
    desc.channels[0] = ChannelDescriptor(5, 11);  // R
    desc.channels[1] = ChannelDescriptor(6, 5);   // G
    desc.channels[2] = ChannelDescriptor(5, 0);   // B
    desc.hasAlpha = false;
    desc.isPremultiplied = false;
    desc.isIndexed = false;
    desc.maxPaletteSize = 0;
    desc.bitOrder = BitOrder::MSBFirst;
    desc.byteOrder = ByteOrder::LittleEndian;
    desc.toStandard = rgb565le_toStandard;
    desc.fromStandard = rgb565le_fromStandard;
    return desc;
}

static PixelFormatDescriptor createRGB565_BE() {
    PixelFormatDescriptor desc;
    desc.id = PixelFormatIDs::RGB565_BE;
    desc.name = "RGB565_BE";
    desc.bitsPerPixel = 16;
    desc.pixelsPerUnit = 1;
    desc.bytesPerUnit = 2;
    desc.channels[0] = ChannelDescriptor(5, 11);  // R
    desc.channels[1] = ChannelDescriptor(6, 5);   // G
    desc.channels[2] = ChannelDescriptor(5, 0);   // B
    desc.hasAlpha = false;
    desc.isPremultiplied = false;
    desc.isIndexed = false;
    desc.maxPaletteSize = 0;
    desc.bitOrder = BitOrder::MSBFirst;
    desc.byteOrder = ByteOrder::BigEndian;
    desc.toStandard = rgb565be_toStandard;
    desc.fromStandard = rgb565be_fromStandard;
    return desc;
}

static PixelFormatDescriptor createRGB332() {
    PixelFormatDescriptor desc;
    desc.id = PixelFormatIDs::RGB332;
    desc.name = "RGB332";
    desc.bitsPerPixel = 8;
    desc.pixelsPerUnit = 1;
    desc.bytesPerUnit = 1;
    desc.channels[0] = ChannelDescriptor(3, 5);   // R
    desc.channels[1] = ChannelDescriptor(3, 2);   // G
    desc.channels[2] = ChannelDescriptor(2, 0);   // B
    desc.hasAlpha = false;
    desc.isPremultiplied = false;
    desc.isIndexed = false;
    desc.maxPaletteSize = 0;
    desc.bitOrder = BitOrder::MSBFirst;
    desc.byteOrder = ByteOrder::Native;
    desc.toStandard = rgb332_toStandard;
    desc.fromStandard = rgb332_fromStandard;
    return desc;
}

static PixelFormatDescriptor createRGB888() {
    PixelFormatDescriptor desc;
    desc.id = PixelFormatIDs::RGB888;
    desc.name = "RGB888";
    desc.bitsPerPixel = 24;
    desc.pixelsPerUnit = 1;
    desc.bytesPerUnit = 3;
    desc.channels[0] = ChannelDescriptor(8, 16);  // R
    desc.channels[1] = ChannelDescriptor(8, 8);   // G
    desc.channels[2] = ChannelDescriptor(8, 0);   // B
    desc.hasAlpha = false;
    desc.isPremultiplied = false;
    desc.isIndexed = false;
    desc.maxPaletteSize = 0;
    desc.bitOrder = BitOrder::MSBFirst;
    desc.byteOrder = ByteOrder::Native;
    desc.toStandard = rgb888_toStandard;
    desc.fromStandard = rgb888_fromStandard;
    return desc;
}

static PixelFormatDescriptor createBGR888() {
    PixelFormatDescriptor desc;
    desc.id = PixelFormatIDs::BGR888;
    desc.name = "BGR888";
    desc.bitsPerPixel = 24;
    desc.pixelsPerUnit = 1;
    desc.bytesPerUnit = 3;
    desc.channels[0] = ChannelDescriptor(8, 0);   // R (at offset 2)
    desc.channels[1] = ChannelDescriptor(8, 8);   // G (at offset 1)
    desc.channels[2] = ChannelDescriptor(8, 16);  // B (at offset 0)
    desc.hasAlpha = false;
    desc.isPremultiplied = false;
    desc.isIndexed = false;
    desc.maxPaletteSize = 0;
    desc.bitOrder = BitOrder::MSBFirst;
    desc.byteOrder = ByteOrder::Native;
    desc.toStandard = bgr888_toStandard;
    desc.fromStandard = bgr888_fromStandard;
    return desc;
}

static const PixelFormatDescriptor RGBA16_Premultiplied = createRGBA16_Premultiplied();
static const PixelFormatDescriptor RGBA8_Straight = createRGBA8_Straight();
static const PixelFormatDescriptor RGB565_LE = createRGB565_LE();
static const PixelFormatDescriptor RGB565_BE = createRGB565_BE();
static const PixelFormatDescriptor RGB332 = createRGB332();
static const PixelFormatDescriptor RGB888 = createRGB888();
static const PixelFormatDescriptor BGR888 = createBGR888();

} // namespace BuiltinFormats

// ========================================================================
// PixelFormatRegistry実装
// ========================================================================

PixelFormatRegistry::PixelFormatRegistry()
    : nextUserFormatID_(PixelFormatIDs::USER_DEFINED_BASE) {

    // 組み込みフォーマットを登録
    formats_[PixelFormatIDs::RGBA16_Premultiplied] = BuiltinFormats::RGBA16_Premultiplied;
    formats_[PixelFormatIDs::RGBA8_Straight] = BuiltinFormats::RGBA8_Straight;
    formats_[PixelFormatIDs::RGB565_LE] = BuiltinFormats::RGB565_LE;
    formats_[PixelFormatIDs::RGB565_BE] = BuiltinFormats::RGB565_BE;
    formats_[PixelFormatIDs::RGB332] = BuiltinFormats::RGB332;
    formats_[PixelFormatIDs::RGB888] = BuiltinFormats::RGB888;
    formats_[PixelFormatIDs::BGR888] = BuiltinFormats::BGR888;

    // 頻出パターンの直接変換を登録
    registerDirectConversion(
        PixelFormatIDs::RGBA16_Premultiplied,
        PixelFormatIDs::RGBA8_Straight,
        directConvert_rgba16PremulToRgba8Straight);
    registerDirectConversion(
        PixelFormatIDs::RGBA8_Straight,
        PixelFormatIDs::RGBA16_Premultiplied,
        directConvert_rgba8StraightToRgba16Premul);
}

PixelFormatRegistry& PixelFormatRegistry::getInstance() {
    static PixelFormatRegistry instance;
    return instance;
}

PixelFormatID PixelFormatRegistry::registerFormat(const PixelFormatDescriptor& descriptor) {
    PixelFormatID newID = nextUserFormatID_++;
    PixelFormatDescriptor desc = descriptor;
    desc.id = newID;
    formats_[newID] = desc;
    return newID;
}

const PixelFormatDescriptor* PixelFormatRegistry::getFormat(PixelFormatID id) const {
    auto it = formats_.find(id);
    return (it != formats_.end()) ? &it->second : nullptr;
}

void PixelFormatRegistry::registerDirectConversion(
    PixelFormatID srcFormat, PixelFormatID dstFormat, DirectConvertFunc func) {
    directConversions_[{srcFormat, dstFormat}] = func;
}

PixelFormatRegistry::DirectConvertFunc PixelFormatRegistry::getDirectConversion(
    PixelFormatID srcFormat, PixelFormatID dstFormat) const {
    auto it = directConversions_.find({srcFormat, dstFormat});
    return (it != directConversions_.end()) ? it->second : nullptr;
}

void PixelFormatRegistry::convert(const void* src, PixelFormatID srcFormatID,
                                   void* dst, PixelFormatID dstFormatID,
                                   int pixelCount,
                                   const uint16_t* srcPalette,
                                   const uint16_t* dstPalette) {
    // 同じフォーマットの場合はコピー
    if (srcFormatID == dstFormatID) {
        const PixelFormatDescriptor* desc = getFormat(srcFormatID);
        if (desc) {
            int units = (pixelCount + desc->pixelsPerUnit - 1) / desc->pixelsPerUnit;
            std::memcpy(dst, src, units * desc->bytesPerUnit);
        }
        return;
    }

    // 直接変換が登録されていれば使用（最適化パス）
    DirectConvertFunc directFunc = getDirectConversion(srcFormatID, dstFormatID);
    if (directFunc) {
        directFunc(src, dst, pixelCount);
        return;
    }

    // 標準フォーマット（RGBA8_Straight）経由で変換
    const PixelFormatDescriptor* srcDesc = getFormat(srcFormatID);
    const PixelFormatDescriptor* dstDesc = getFormat(dstFormatID);

    if (!srcDesc || !dstDesc) return;

    // 一時バッファを確保
    conversionBuffer_.resize(pixelCount * 4);

    // src → RGBA8_Straight
    if (srcDesc->isIndexed && srcDesc->toStandardIndexed && srcPalette) {
        srcDesc->toStandardIndexed(src, conversionBuffer_.data(), pixelCount, srcPalette);
    } else if (!srcDesc->isIndexed && srcDesc->toStandard) {
        srcDesc->toStandard(src, conversionBuffer_.data(), pixelCount);
    }

    // RGBA8_Straight → dst
    if (dstDesc->isIndexed && dstDesc->fromStandardIndexed && dstPalette) {
        dstDesc->fromStandardIndexed(conversionBuffer_.data(), dst, pixelCount, dstPalette);
    } else if (!dstDesc->isIndexed && dstDesc->fromStandard) {
        dstDesc->fromStandard(conversionBuffer_.data(), dst, pixelCount);
    }
}

} // namespace FLEXIMG_NAMESPACE
