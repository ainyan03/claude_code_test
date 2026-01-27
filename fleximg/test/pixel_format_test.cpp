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
    SUBCASE("RGBA8_Straight") {
        const auto* desc = PixelFormatIDs::RGBA8_Straight;
        CHECK(desc->bitsPerPixel == 32);
        CHECK(desc->bytesPerUnit == 4);
        CHECK(desc->hasAlpha == true);
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
// getBytesPerPixel Tests
// =============================================================================

TEST_CASE("getBytesPerPixel") {
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
// Direct Conversion Tests (disabled - getDirectConversion API removed)
// =============================================================================
// NOTE: getDirectConversion function was removed from the API
// These tests are commented out until the API is restored or tests are updated

// =============================================================================
// getFormatByName Tests
// =============================================================================

TEST_CASE("getFormatByName") {
    SUBCASE("finds builtin formats by name") {
        CHECK(getFormatByName("RGBA8_Straight") == PixelFormatIDs::RGBA8_Straight);
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

    SUBCASE("ChannelType construction") {
        ChannelDescriptor ch(ChannelType::Alpha, 8, 0);
        CHECK(ch.type == ChannelType::Alpha);
        CHECK(ch.bits == 8);
        CHECK(ch.shift == 0);
        CHECK(ch.mask == 0x00FF);
    }

    SUBCASE("default ChannelType is Unused") {
        ChannelDescriptor ch(8, 0);  // 旧コンストラクタ
        CHECK(ch.type == ChannelType::Unused);
    }
}

// =============================================================================
// PixelFormatDescriptor Channel Methods Tests
// =============================================================================

TEST_CASE("PixelFormatDescriptor channel methods") {
    SUBCASE("Alpha8 - single channel") {
        const auto* fmt = PixelFormatIDs::Alpha8;

        CHECK(fmt->channelCount == 1);
        CHECK(fmt->getChannel(0).type == ChannelType::Alpha);
        CHECK(fmt->getChannel(0).bits == 8);
        CHECK(fmt->getChannel(1).type == ChannelType::Unused);

        CHECK(fmt->hasChannelType(ChannelType::Alpha) == true);
        CHECK(fmt->hasChannelType(ChannelType::Red) == false);

        CHECK(fmt->getChannelIndex(ChannelType::Alpha) == 0);
        CHECK(fmt->getChannelIndex(ChannelType::Red) == -1);

        auto alphaCh = fmt->getChannelByType(ChannelType::Alpha);
        CHECK(alphaCh.type == ChannelType::Alpha);
        CHECK(alphaCh.bits == 8);
    }

    SUBCASE("RGBA8 - four channels") {
        const auto* fmt = PixelFormatIDs::RGBA8_Straight;

        CHECK(fmt->channelCount == 4);
        CHECK(fmt->getChannelIndex(ChannelType::Red) == 0);
        CHECK(fmt->getChannelIndex(ChannelType::Green) == 1);
        CHECK(fmt->getChannelIndex(ChannelType::Blue) == 2);
        CHECK(fmt->getChannelIndex(ChannelType::Alpha) == 3);

        auto alphaCh = fmt->getChannelByType(ChannelType::Alpha);
        CHECK(alphaCh.type == ChannelType::Alpha);
        CHECK(alphaCh.bits == 8);
    }

    SUBCASE("RGB565 - packed format") {
        const auto* fmt = PixelFormatIDs::RGB565_LE;

        CHECK(fmt->channelCount == 3);
        CHECK(fmt->hasChannelType(ChannelType::Red) == true);
        CHECK(fmt->hasChannelType(ChannelType::Alpha) == false);

        auto redCh = fmt->getChannelByType(ChannelType::Red);
        CHECK(redCh.type == ChannelType::Red);
        CHECK(redCh.bits == 5);
        CHECK(redCh.shift == 11);
    }
}

// =============================================================================
// Alpha8 Conversion Tests
// =============================================================================

TEST_CASE("Alpha8 pixel format conversion") {
    SUBCASE("Alpha8 to RGBA8_Straight") {
        uint8_t src[3] = {0, 128, 255};
        uint8_t dst[12] = {0};

        convertFormat(src, PixelFormatIDs::Alpha8,
                      dst, PixelFormatIDs::RGBA8_Straight, 3);

        // Pixel 0: alpha=0
        CHECK(dst[0] == 0);   // R
        CHECK(dst[1] == 0);   // G
        CHECK(dst[2] == 0);   // B
        CHECK(dst[3] == 0);   // A

        // Pixel 1: alpha=128
        CHECK(dst[4] == 128);
        CHECK(dst[5] == 128);
        CHECK(dst[6] == 128);
        CHECK(dst[7] == 128);

        // Pixel 2: alpha=255
        CHECK(dst[8] == 255);
        CHECK(dst[9] == 255);
        CHECK(dst[10] == 255);
        CHECK(dst[11] == 255);
    }

    SUBCASE("RGBA8_Straight to Alpha8") {
        uint8_t src[12] = {
            100, 100, 100, 50,   // R,G,B,A (alpha=50)
            200, 200, 200, 150,  // alpha=150
            255, 255, 255, 255   // alpha=255
        };
        uint8_t dst[3] = {0};

        convertFormat(src, PixelFormatIDs::RGBA8_Straight,
                      dst, PixelFormatIDs::Alpha8, 3);

        CHECK(dst[0] == 50);
        CHECK(dst[1] == 150);
        CHECK(dst[2] == 255);
    }

    SUBCASE("round-trip conversion") {
        uint8_t original[4] = {0, 64, 192, 255};
        uint8_t intermediate[16];
        uint8_t result[4];

        // Alpha8 → RGBA8 → Alpha8
        convertFormat(original, PixelFormatIDs::Alpha8,
                      intermediate, PixelFormatIDs::RGBA8_Straight, 4);
        convertFormat(intermediate, PixelFormatIDs::RGBA8_Straight,
                      result, PixelFormatIDs::Alpha8, 4);

        for (int i = 0; i < 4; ++i) {
            CHECK(result[i] == original[i]);
        }
    }
}
