#include "pixel_format_registry.h"
#include <cstring>
#include <algorithm>

namespace ImageTransform {

// ========================================================================
// 組み込みフォーマットの変換関数
// ========================================================================

// RGBA16_Straight: 自分自身なのでコピー
static void rgba16Straight_toStandard(const void* src, uint16_t* dst, int pixelCount) {
    std::memcpy(dst, src, pixelCount * 4 * sizeof(uint16_t));
}

static void rgba16Straight_fromStandard(const uint16_t* src, void* dst, int pixelCount) {
    std::memcpy(dst, src, pixelCount * 4 * sizeof(uint16_t));
}

// RGBA16_Premultiplied: アンプリマルチプライド/プリマルチプライド変換
static void rgba16Premul_toStandard(const void* src, uint16_t* dst, int pixelCount) {
    const uint16_t* s = static_cast<const uint16_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;
        uint16_t a = s[idx + 3];

        if (a > 0) {
            // アンプリマルチプライド: RGB / alpha
            dst[idx]     = ((uint32_t)s[idx]     * 65535) / a;
            dst[idx + 1] = ((uint32_t)s[idx + 1] * 65535) / a;
            dst[idx + 2] = ((uint32_t)s[idx + 2] * 65535) / a;
        } else {
            dst[idx] = dst[idx + 1] = dst[idx + 2] = 0;
        }
        dst[idx + 3] = a;
    }
}

static void rgba16Premul_fromStandard(const uint16_t* src, void* dst, int pixelCount) {
    uint16_t* d = static_cast<uint16_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;
        uint16_t a = src[idx + 3];

        // プリマルチプライド: RGB * alpha
        d[idx]     = (src[idx]     * a) >> 16;
        d[idx + 1] = (src[idx + 1] * a) >> 16;
        d[idx + 2] = (src[idx + 2] * a) >> 16;
        d[idx + 3] = a;
    }
}

// ========================================================================
// 組み込みフォーマット定義
// ========================================================================

namespace BuiltinFormats {

static PixelFormatDescriptor createRGBA16_Straight() {
    PixelFormatDescriptor desc;
    desc.id = PixelFormatIDs::RGBA16_Straight;
    desc.name = "RGBA16_Straight";
    desc.bitsPerPixel = 64;
    desc.pixelsPerUnit = 1;
    desc.bytesPerUnit = 8;
    desc.channels[0] = ChannelDescriptor(16, 0);  // R
    desc.channels[1] = ChannelDescriptor(16, 0);  // G
    desc.channels[2] = ChannelDescriptor(16, 0);  // B
    desc.channels[3] = ChannelDescriptor(16, 0);  // A
    desc.hasAlpha = true;
    desc.isPremultiplied = false;
    desc.isIndexed = false;
    desc.maxPaletteSize = 0;
    desc.bitOrder = BitOrder::MSBFirst;
    desc.byteOrder = ByteOrder::Native;
    desc.toStandard = rgba16Straight_toStandard;
    desc.fromStandard = rgba16Straight_fromStandard;
    return desc;
}

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

static const PixelFormatDescriptor RGBA16_Straight = createRGBA16_Straight();
static const PixelFormatDescriptor RGBA16_Premultiplied = createRGBA16_Premultiplied();

} // namespace BuiltinFormats

// ========================================================================
// PixelFormatRegistry実装
// ========================================================================

PixelFormatRegistry::PixelFormatRegistry()
    : nextUserFormatID_(PixelFormatIDs::USER_DEFINED_BASE) {

    // 組み込みフォーマットを登録
    formats_[PixelFormatIDs::RGBA16_Straight] = BuiltinFormats::RGBA16_Straight;
    formats_[PixelFormatIDs::RGBA16_Premultiplied] = BuiltinFormats::RGBA16_Premultiplied;
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

    // 標準フォーマット（RGBA16_Straight）を経由して変換
    conversionBuffer_.resize(pixelCount * 4);

    // src → RGBA16_Straight
    if (srcDesc->isIndexed && srcDesc->toStandardIndexed && srcPalette) {
        srcDesc->toStandardIndexed(src, conversionBuffer_.data(), pixelCount, srcPalette);
    } else if (!srcDesc->isIndexed && srcDesc->toStandard) {
        srcDesc->toStandard(src, conversionBuffer_.data(), pixelCount);
    }

    // RGBA16_Straight → dst
    if (dstDesc->isIndexed && dstDesc->fromStandardIndexed && dstPalette) {
        dstDesc->fromStandardIndexed(conversionBuffer_.data(), dst, pixelCount, dstPalette);
    } else if (!dstDesc->isIndexed && dstDesc->fromStandard) {
        dstDesc->fromStandard(conversionBuffer_.data(), dst, pixelCount);
    }
}

} // namespace ImageTransform
