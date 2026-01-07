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

// RGBA16_Premultiplied: 16bit Premultiplied ↔ 8bit Straight 変換
static void rgba16Premul_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    const uint16_t* s = static_cast<const uint16_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;
        uint16_t r16 = s[idx];
        uint16_t g16 = s[idx + 1];
        uint16_t b16 = s[idx + 2];
        uint16_t a16 = s[idx + 3];

        // Unpremultiply + 16bit → 8bit
        if (a16 > 0) {
            uint32_t r_unpre = ((uint32_t)r16 * 65535) / a16;
            uint32_t g_unpre = ((uint32_t)g16 * 65535) / a16;
            uint32_t b_unpre = ((uint32_t)b16 * 65535) / a16;
            dst[idx]     = std::min(r_unpre >> 8, 255u);
            dst[idx + 1] = std::min(g_unpre >> 8, 255u);
            dst[idx + 2] = std::min(b_unpre >> 8, 255u);
        } else {
            dst[idx] = dst[idx + 1] = dst[idx + 2] = 0;
        }
        dst[idx + 3] = a16 >> 8;
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

        // 8bit → 16bit 拡張（0-255 → 0-65535）
        uint16_t r16 = (r8 << 8) | r8;
        uint16_t g16 = (g8 << 8) | g8;
        uint16_t b16 = (b8 << 8) | b8;
        uint16_t a16 = (a8 << 8) | a8;

        // Premultiply: RGB * alpha / 65535
        d[idx]     = (r16 * a16) >> 16;
        d[idx + 1] = (g16 * a16) >> 16;
        d[idx + 2] = (b16 * a16) >> 16;
        d[idx + 3] = a16;
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

static const PixelFormatDescriptor RGBA16_Premultiplied = createRGBA16_Premultiplied();
static const PixelFormatDescriptor RGBA8_Straight = createRGBA8_Straight();

} // namespace BuiltinFormats

// ========================================================================
// PixelFormatRegistry実装
// ========================================================================

PixelFormatRegistry::PixelFormatRegistry()
    : nextUserFormatID_(PixelFormatIDs::USER_DEFINED_BASE) {

    // 組み込みフォーマットを登録
    formats_[PixelFormatIDs::RGBA16_Premultiplied] = BuiltinFormats::RGBA16_Premultiplied;
    formats_[PixelFormatIDs::RGBA8_Straight] = BuiltinFormats::RGBA8_Straight;
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

    const PixelFormatDescriptor* srcDesc = getFormat(srcFormatID);
    const PixelFormatDescriptor* dstDesc = getFormat(dstFormatID);

    if (!srcDesc || !dstDesc) return;

    // 標準フォーマット（RGBA8_Straight）を経由して変換
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
