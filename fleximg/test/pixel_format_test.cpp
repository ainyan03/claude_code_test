// fleximg pixel_format.h / pixel_format_registry.h Unit Tests
// ピクセルフォーマットID、記述子、変換のテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/image/pixel_format.h"
#include "fleximg/image/pixel_format_registry.h"

using namespace fleximg;

// =============================================================================
// PixelFormatID Constants Tests
// =============================================================================

TEST_CASE("PixelFormatID constants") {
    SUBCASE("RGBA16 formats") {
        CHECK(PixelFormatIDs::RGBA16_Straight == 0x0001);
        CHECK(PixelFormatIDs::RGBA16_Premultiplied == 0x0002);
    }

    SUBCASE("Packed RGB formats") {
        CHECK(PixelFormatIDs::RGB565_LE == 0x0100);
        CHECK(PixelFormatIDs::RGB565_BE == 0x0101);
        CHECK(PixelFormatIDs::RGB332 == 0x0102);
    }

    SUBCASE("RGBA8 formats") {
        CHECK(PixelFormatIDs::RGBA8_Straight == 0x0200);
        CHECK(PixelFormatIDs::RGBA8_Premultiplied == 0x0201);
        CHECK(PixelFormatIDs::RGB888 == 0x0202);
        CHECK(PixelFormatIDs::BGR888 == 0x0203);
    }

    SUBCASE("Grayscale formats") {
        CHECK(PixelFormatIDs::Grayscale8 == 0x0300);
        CHECK(PixelFormatIDs::Grayscale16 == 0x0301);
    }

    SUBCASE("Mono formats") {
        CHECK(PixelFormatIDs::Mono1bit_MSB == 0x0400);
        CHECK(PixelFormatIDs::Mono1bit_LSB == 0x0401);
    }

    SUBCASE("Indexed formats") {
        CHECK(PixelFormatIDs::Indexed4bit == 0x0500);
        CHECK(PixelFormatIDs::Indexed8bit == 0x0501);
    }

    SUBCASE("User defined base") {
        CHECK(PixelFormatIDs::USER_DEFINED_BASE == 0x10000000);
    }
}

// =============================================================================
// RGBA16 Premultiplied Alpha Thresholds
// =============================================================================

TEST_CASE("RGBA16 Premultiplied alpha thresholds") {
    using namespace PixelFormatIDs::RGBA16Premul;

    SUBCASE("threshold constants") {
        CHECK(ALPHA_TRANSPARENT_MAX == 255);
        CHECK(ALPHA_OPAQUE_MIN == 65280);
    }

    SUBCASE("isTransparent") {
        CHECK(isTransparent(0) == true);
        CHECK(isTransparent(255) == true);
        CHECK(isTransparent(256) == false);
        CHECK(isTransparent(65535) == false);
    }

    SUBCASE("isOpaque") {
        CHECK(isOpaque(0) == false);
        CHECK(isOpaque(65279) == false);
        CHECK(isOpaque(65280) == true);
        CHECK(isOpaque(65535) == true);
    }
}

// =============================================================================
// getBytesPerPixel Tests
// =============================================================================

TEST_CASE("getBytesPerPixel") {
    // 注: 現在レジストリに登録されているフォーマットのみテスト
    // 未登録フォーマットはフォールバック値(4)を返す

    SUBCASE("RGBA16_Premultiplied - 8 bytes") {
        CHECK(getBytesPerPixel(PixelFormatIDs::RGBA16_Premultiplied) == 8);
    }

    SUBCASE("RGBA8_Straight - 4 bytes") {
        CHECK(getBytesPerPixel(PixelFormatIDs::RGBA8_Straight) == 4);
    }

    SUBCASE("RGB formats - 3 bytes") {
        CHECK(getBytesPerPixel(PixelFormatIDs::RGB888) == 3);
        CHECK(getBytesPerPixel(PixelFormatIDs::BGR888) == 3);
    }

    SUBCASE("Packed RGB formats - 2 bytes") {
        CHECK(getBytesPerPixel(PixelFormatIDs::RGB565_LE) == 2);
        CHECK(getBytesPerPixel(PixelFormatIDs::RGB565_BE) == 2);
    }

    SUBCASE("RGB332 - 1 byte") {
        CHECK(getBytesPerPixel(PixelFormatIDs::RGB332) == 1);
    }

    SUBCASE("unknown format returns fallback (4)") {
        CHECK(getBytesPerPixel(0xFFFFFFFF) == 4);
    }
}

// =============================================================================
// PixelFormatRegistry Tests
// =============================================================================

TEST_CASE("PixelFormatRegistry") {
    auto& registry = PixelFormatRegistry::getInstance();

    SUBCASE("singleton instance") {
        auto& registry2 = PixelFormatRegistry::getInstance();
        CHECK(&registry == &registry2);
    }

    SUBCASE("getFormat returns valid descriptor") {
        const auto* desc = registry.getFormat(PixelFormatIDs::RGBA8_Straight);
        REQUIRE(desc != nullptr);
        CHECK(desc->id == PixelFormatIDs::RGBA8_Straight);
        CHECK(desc->bitsPerPixel == 32);
        CHECK(desc->hasAlpha == true);
        CHECK(desc->isPremultiplied == false);
    }

    SUBCASE("getFormat for RGBA16_Premultiplied") {
        const auto* desc = registry.getFormat(PixelFormatIDs::RGBA16_Premultiplied);
        REQUIRE(desc != nullptr);
        CHECK(desc->id == PixelFormatIDs::RGBA16_Premultiplied);
        CHECK(desc->bitsPerPixel == 64);
        CHECK(desc->hasAlpha == true);
        CHECK(desc->isPremultiplied == true);
    }

    SUBCASE("getFormat for unknown format returns nullptr") {
        const auto* desc = registry.getFormat(0xFFFFFFFF);
        CHECK(desc == nullptr);
    }
}

// =============================================================================
// ChannelDescriptor Tests
// =============================================================================

TEST_CASE("ChannelDescriptor") {
    SUBCASE("default construction") {
        ChannelDescriptor ch;
        CHECK(ch.bits == 0);
        CHECK(ch.shift == 0);
        CHECK(ch.mask == 0);
    }

    SUBCASE("8-bit channel at shift 0") {
        ChannelDescriptor ch(8, 0);
        CHECK(ch.bits == 8);
        CHECK(ch.shift == 0);
        CHECK(ch.mask == 0x00FF);
    }

    SUBCASE("8-bit channel at shift 8") {
        ChannelDescriptor ch(8, 8);
        CHECK(ch.bits == 8);
        CHECK(ch.shift == 8);
        CHECK(ch.mask == 0xFF00);
    }

    SUBCASE("5-bit channel (RGB565 style)") {
        ChannelDescriptor ch(5, 11);  // R in RGB565
        CHECK(ch.bits == 5);
        CHECK(ch.shift == 11);
        CHECK(ch.mask == 0xF800);
    }
}
