// fleximg pixel_format.h Unit Tests
// ピクセルフォーマットDescriptor、変換のテスト

#include "doctest.h"
#include <string>

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/image/pixel_format.h"

using namespace fleximg;

// =============================================================================
// PixelFormatID (Descriptor Pointer) Tests
// =============================================================================

TEST_CASE("PixelFormatID constants are valid pointers") {
    SUBCASE("RGBA16_Premultiplied") {
        CHECK(PixelFormatIDs::RGBA16_Premultiplied != nullptr);
        CHECK(PixelFormatIDs::RGBA16_Premultiplied->name != nullptr);
    }

    SUBCASE("RGBA8_Straight") {
        CHECK(PixelFormatIDs::RGBA8_Straight != nullptr);
        CHECK(PixelFormatIDs::RGBA8_Straight->name != nullptr);
    }

    SUBCASE("RGB565 formats") {
        CHECK(PixelFormatIDs::RGB565_LE != nullptr);
        CHECK(PixelFormatIDs::RGB565_BE != nullptr);
    }

    SUBCASE("RGB formats") {
        CHECK(PixelFormatIDs::RGB888 != nullptr);
        CHECK(PixelFormatIDs::BGR888 != nullptr);
        CHECK(PixelFormatIDs::RGB332 != nullptr);
    }
}

// =============================================================================
// PixelFormatDescriptor Tests
// =============================================================================

TEST_CASE("PixelFormatDescriptor properties") {
    SUBCASE("RGBA16_Premultiplied") {
        const auto* desc = PixelFormatIDs::RGBA16_Premultiplied;
        CHECK(desc->bitsPerPixel == 64);
        CHECK(desc->bytesPerUnit == 8);
        CHECK(desc->hasAlpha == true);
        CHECK(desc->isPremultiplied == true);
        CHECK(desc->isIndexed == false);
    }

    SUBCASE("RGBA8_Straight") {
        const auto* desc = PixelFormatIDs::RGBA8_Straight;
        CHECK(desc->bitsPerPixel == 32);
        CHECK(desc->bytesPerUnit == 4);
        CHECK(desc->hasAlpha == true);
        CHECK(desc->isPremultiplied == false);
        CHECK(desc->isIndexed == false);
    }

    SUBCASE("RGB565_LE") {
        const auto* desc = PixelFormatIDs::RGB565_LE;
        CHECK(desc->bitsPerPixel == 16);
        CHECK(desc->bytesPerUnit == 2);
        CHECK(desc->hasAlpha == false);
    }

    SUBCASE("RGB888") {
        const auto* desc = PixelFormatIDs::RGB888;
        CHECK(desc->bitsPerPixel == 24);
        CHECK(desc->bytesPerUnit == 3);
        CHECK(desc->hasAlpha == false);
    }
}

// =============================================================================
// RGBA16 Premultiplied Alpha Thresholds
// =============================================================================

TEST_CASE("RGBA16 Premultiplied alpha thresholds") {
    using namespace RGBA16Premul;

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

    SUBCASE("nullptr returns fallback (4)") {
        CHECK(getBytesPerPixel(nullptr) == 4);
    }
}

// =============================================================================
// Direct Conversion Tests
// =============================================================================

TEST_CASE("getDirectConversion") {
    SUBCASE("RGBA16_Premul to RGBA8_Straight has direct conversion") {
        auto func = getDirectConversion(
            PixelFormatIDs::RGBA16_Premultiplied,
            PixelFormatIDs::RGBA8_Straight
        );
        CHECK(func != nullptr);
    }

    SUBCASE("RGBA8_Straight to RGBA16_Premul has direct conversion") {
        auto func = getDirectConversion(
            PixelFormatIDs::RGBA8_Straight,
            PixelFormatIDs::RGBA16_Premultiplied
        );
        CHECK(func != nullptr);
    }

    SUBCASE("unsupported conversion returns nullptr") {
        auto func = getDirectConversion(
            PixelFormatIDs::RGB565_LE,
            PixelFormatIDs::RGB888
        );
        CHECK(func == nullptr);
    }
}

// =============================================================================
// getFormatByName Tests
// =============================================================================

TEST_CASE("getFormatByName") {
    SUBCASE("finds builtin formats by name") {
        CHECK(getFormatByName("RGBA8_Straight") == PixelFormatIDs::RGBA8_Straight);
        CHECK(getFormatByName("RGBA16_Premultiplied") == PixelFormatIDs::RGBA16_Premultiplied);
        CHECK(getFormatByName("RGB565_LE") == PixelFormatIDs::RGB565_LE);
        CHECK(getFormatByName("RGB888") == PixelFormatIDs::RGB888);
    }

    SUBCASE("returns nullptr for unknown name") {
        CHECK(getFormatByName("NonExistent") == nullptr);
        CHECK(getFormatByName("") == nullptr);
        CHECK(getFormatByName(nullptr) == nullptr);
    }
}

TEST_CASE("getFormatName") {
    SUBCASE("returns correct names") {
        CHECK(std::string(getFormatName(PixelFormatIDs::RGBA8_Straight)) == "RGBA8_Straight");
        CHECK(std::string(getFormatName(PixelFormatIDs::RGB565_LE)) == "RGB565_LE");
    }

    SUBCASE("returns unknown for nullptr") {
        CHECK(std::string(getFormatName(nullptr)) == "unknown");
    }
}

// =============================================================================
// convertFormat Tests
// =============================================================================

TEST_CASE("convertFormat") {
    SUBCASE("same format just copies") {
        uint8_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        uint8_t dst[8] = {0};

        convertFormat(src, PixelFormatIDs::RGBA8_Straight,
                      dst, PixelFormatIDs::RGBA8_Straight, 2);

        for (int i = 0; i < 8; ++i) {
            CHECK(dst[i] == src[i]);
        }
    }

    SUBCASE("RGBA8 to RGBA16 conversion") {
        // RGBA8: opaque red
        uint8_t src[4] = {255, 0, 0, 255};
        uint16_t dst[4] = {0};

        convertFormat(src, PixelFormatIDs::RGBA8_Straight,
                      dst, PixelFormatIDs::RGBA16_Premultiplied, 1);

        // A8=255, A_tmp=256, so:
        // R16 = 255 * 256 = 65280
        // G16 = 0 * 256 = 0
        // B16 = 0 * 256 = 0
        // A16 = 255 * 256 = 65280
        CHECK(dst[0] == 65280);  // R
        CHECK(dst[1] == 0);      // G
        CHECK(dst[2] == 0);      // B
        CHECK(dst[3] == 65280);  // A
    }

    SUBCASE("RGBA16 to RGBA8 conversion") {
        // RGBA16 Premul: opaque red (max values)
        uint16_t src[4] = {65280, 0, 0, 65280};
        uint8_t dst[4] = {0};

        convertFormat(src, PixelFormatIDs::RGBA16_Premultiplied,
                      dst, PixelFormatIDs::RGBA8_Straight, 1);

        // A8 = 65280 >> 8 = 255
        // A_tmp = 256
        // R8 = 65280 / 256 = 255
        CHECK(dst[0] == 255);  // R
        CHECK(dst[1] == 0);    // G
        CHECK(dst[2] == 0);    // B
        CHECK(dst[3] == 255);  // A
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
