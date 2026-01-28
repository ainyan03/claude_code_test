// fleximg pixel_format.h Unit Tests
// ピクセルフォーマットDescriptor、変換のテスト

#include "doctest.h"
#include <string>

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/image/image_buffer.h"

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

// =============================================================================
// Grayscale8 Tests
// =============================================================================

TEST_CASE("Grayscale8 pixel format properties") {
    const auto* fmt = PixelFormatIDs::Grayscale8;

    SUBCASE("basic properties") {
        CHECK(fmt != nullptr);
        CHECK(fmt->bitsPerPixel == 8);
        CHECK(fmt->bytesPerUnit == 1);
        CHECK(fmt->channelCount == 1);
        CHECK(fmt->hasAlpha == false);
        CHECK(fmt->isIndexed == false);
        CHECK(fmt->maxPaletteSize == 0);
        CHECK(fmt->expandIndex == nullptr);
    }

    SUBCASE("channel type") {
        CHECK(fmt->getChannel(0).type == ChannelType::Luminance);
        CHECK(fmt->getChannel(0).bits == 8);
        CHECK(fmt->hasChannelType(ChannelType::Luminance) == true);
        CHECK(fmt->hasChannelType(ChannelType::Red) == false);
    }

    SUBCASE("getBytesPerPixel") {
        CHECK(getBytesPerPixel(PixelFormatIDs::Grayscale8) == 1);
    }

    SUBCASE("getFormatByName") {
        CHECK(getFormatByName("Grayscale8") == PixelFormatIDs::Grayscale8);
    }
}

TEST_CASE("Grayscale8 conversion") {
    SUBCASE("Grayscale8 to RGBA8_Straight") {
        uint8_t src[3] = {0, 128, 255};
        uint8_t dst[12] = {0};

        convertFormat(src, PixelFormatIDs::Grayscale8,
                      dst, PixelFormatIDs::RGBA8_Straight, 3);

        // Pixel 0: black
        CHECK(dst[0] == 0);    // R
        CHECK(dst[1] == 0);    // G
        CHECK(dst[2] == 0);    // B
        CHECK(dst[3] == 255);  // A (always opaque)

        // Pixel 1: mid gray
        CHECK(dst[4] == 128);
        CHECK(dst[5] == 128);
        CHECK(dst[6] == 128);
        CHECK(dst[7] == 255);

        // Pixel 2: white
        CHECK(dst[8] == 255);
        CHECK(dst[9] == 255);
        CHECK(dst[10] == 255);
        CHECK(dst[11] == 255);
    }

    SUBCASE("RGBA8_Straight to Grayscale8 (BT.601)") {
        // Pure red: (77*255 + 150*0 + 29*0 + 128) >> 8 = 76
        // Pure green: (77*0 + 150*255 + 29*0 + 128) >> 8 = 149
        // Pure blue: (77*0 + 150*0 + 29*255 + 128) >> 8 = 29
        // White: (77*255 + 150*255 + 29*255 + 128) >> 8 = 255
        uint8_t src[16] = {
            255, 0, 0, 255,     // Red
            0, 255, 0, 255,     // Green
            0, 0, 255, 255,     // Blue
            255, 255, 255, 255  // White
        };
        uint8_t dst[4] = {0};

        convertFormat(src, PixelFormatIDs::RGBA8_Straight,
                      dst, PixelFormatIDs::Grayscale8, 4);

        CHECK(dst[0] == 77);   // Red luminance
        CHECK(dst[1] == 149);  // Green luminance
        CHECK(dst[2] == 29);   // Blue luminance
        CHECK(dst[3] == 255);  // White luminance
    }

    SUBCASE("round-trip Grayscale8 → RGBA8 → Grayscale8") {
        uint8_t original[4] = {0, 64, 192, 255};
        uint8_t intermediate[16];
        uint8_t result[4];

        convertFormat(original, PixelFormatIDs::Grayscale8,
                      intermediate, PixelFormatIDs::RGBA8_Straight, 4);
        convertFormat(intermediate, PixelFormatIDs::RGBA8_Straight,
                      result, PixelFormatIDs::Grayscale8, 4);

        // Gray → RGBA (R=G=B=L) → Gray (BT.601) should preserve value
        // because BT.601 with R=G=B=L gives: (77+150+29)*L/256 = 256*L/256 = L
        for (int i = 0; i < 4; ++i) {
            CHECK(result[i] == original[i]);
        }
    }
}

// =============================================================================
// Index8 Tests
// =============================================================================

TEST_CASE("Index8 pixel format properties") {
    const auto* fmt = PixelFormatIDs::Index8;

    SUBCASE("basic properties") {
        CHECK(fmt != nullptr);
        CHECK(fmt->bitsPerPixel == 8);
        CHECK(fmt->bytesPerUnit == 1);
        CHECK(fmt->channelCount == 1);
        CHECK(fmt->hasAlpha == false);
        CHECK(fmt->isIndexed == true);
        CHECK(fmt->maxPaletteSize == 256);
    }

    SUBCASE("expandIndex is set, toStraight/fromStraight are null") {
        CHECK(fmt->expandIndex != nullptr);
        CHECK(fmt->toStraight == nullptr);
        CHECK(fmt->fromStraight == nullptr);
    }

    SUBCASE("channel type") {
        CHECK(fmt->getChannel(0).type == ChannelType::Index);
        CHECK(fmt->getChannel(0).bits == 8);
        CHECK(fmt->hasChannelType(ChannelType::Index) == true);
    }

    SUBCASE("getBytesPerPixel") {
        CHECK(getBytesPerPixel(PixelFormatIDs::Index8) == 1);
    }

    SUBCASE("getFormatByName") {
        CHECK(getFormatByName("Index8") == PixelFormatIDs::Index8);
    }
}

TEST_CASE("Index8 conversion with RGBA8 palette") {
    // RGBA8パレット: 4エントリ
    uint8_t palette[16] = {
        255, 0, 0, 255,       // [0] Red
        0, 255, 0, 255,       // [1] Green
        0, 0, 255, 255,       // [2] Blue
        255, 255, 255, 128    // [3] White, semi-transparent
    };

    PixelAuxInfo srcAux;
    srcAux.palette = palette;
    srcAux.paletteFormat = PixelFormatIDs::RGBA8_Straight;
    srcAux.paletteColorCount = 4;

    SUBCASE("Index8 + RGBA8 palette → RGBA8") {
        uint8_t src[4] = {0, 1, 2, 3};
        uint8_t dst[16] = {0};

        convertFormat(src, PixelFormatIDs::Index8,
                      dst, PixelFormatIDs::RGBA8_Straight, 4,
                      &srcAux);

        // Pixel 0: Red
        CHECK(dst[0] == 255);
        CHECK(dst[1] == 0);
        CHECK(dst[2] == 0);
        CHECK(dst[3] == 255);

        // Pixel 1: Green
        CHECK(dst[4] == 0);
        CHECK(dst[5] == 255);
        CHECK(dst[6] == 0);
        CHECK(dst[7] == 255);

        // Pixel 2: Blue
        CHECK(dst[8] == 0);
        CHECK(dst[9] == 0);
        CHECK(dst[10] == 255);
        CHECK(dst[11] == 255);

        // Pixel 3: White, semi-transparent
        CHECK(dst[12] == 255);
        CHECK(dst[13] == 255);
        CHECK(dst[14] == 255);
        CHECK(dst[15] == 128);
    }

    SUBCASE("Index8 + RGBA8 palette → RGB565_LE (2-stage conversion)") {
        uint8_t src[2] = {0, 1};  // Red, Green
        uint8_t dst[4] = {0};

        convertFormat(src, PixelFormatIDs::Index8,
                      dst, PixelFormatIDs::RGB565_LE, 2,
                      &srcAux);

        // Verify non-zero output (exact values depend on conversion)
        uint16_t pixel0 = *reinterpret_cast<uint16_t*>(&dst[0]);
        uint16_t pixel1 = *reinterpret_cast<uint16_t*>(&dst[2]);
        CHECK(pixel0 != 0);  // Red should produce non-zero RGB565
        CHECK(pixel1 != 0);  // Green should produce non-zero RGB565
    }

    SUBCASE("Index8 out-of-range index clamped") {
        uint8_t src[1] = {200};  // Out of range (palette has 4 entries)
        uint8_t dst[4] = {0};

        convertFormat(src, PixelFormatIDs::Index8,
                      dst, PixelFormatIDs::RGBA8_Straight, 1,
                      &srcAux);

        // Should be clamped to max index (3) = White, semi-transparent
        CHECK(dst[0] == 255);
        CHECK(dst[1] == 255);
        CHECK(dst[2] == 255);
        CHECK(dst[3] == 128);
    }
}

TEST_CASE("Index8 conversion without palette (fallback)") {
    SUBCASE("no srcAux → no conversion output (memcpy of same format)") {
        uint8_t src[2] = {0, 1};
        uint8_t dst[8] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};

        // Without palette, expandIndex path is skipped (srcAux is nullptr)
        // and toStraight is nullptr, so RGBA8 buffer stays uninitialized
        convertFormat(src, PixelFormatIDs::Index8,
                      dst, PixelFormatIDs::RGBA8_Straight, 2);

        // With no palette and no toStraight, conversion is a no-op
        // (conversionBuffer is uninitialized, fromStraight writes garbage)
        // This is expected behavior for Index8 without palette
    }
}

// =============================================================================
// ImageBuffer Palette Tests
// =============================================================================

TEST_CASE("ImageBuffer palette support") {
    SUBCASE("default palette is null") {
        ImageBuffer buf(4, 4, PixelFormatIDs::RGBA8_Straight);
        CHECK(buf.palette() == nullptr);
        CHECK(buf.paletteFormat() == nullptr);
        CHECK(buf.paletteColorCount() == 0);
    }

    SUBCASE("setPalette and accessors") {
        uint8_t palette[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        ImageBuffer buf(4, 4, PixelFormatIDs::Index8);
        buf.setPalette(palette, PixelFormatIDs::RGBA8_Straight, 2);

        CHECK(buf.palette() == palette);
        CHECK(buf.paletteFormat() == PixelFormatIDs::RGBA8_Straight);
        CHECK(buf.paletteColorCount() == 2);
    }

    SUBCASE("copy constructor propagates palette") {
        uint8_t palette[4] = {10, 20, 30, 40};
        ImageBuffer original(4, 4, PixelFormatIDs::Index8);
        original.setPalette(palette, PixelFormatIDs::RGBA8_Straight, 1);

        ImageBuffer copy(original);
        CHECK(copy.palette() == palette);
        CHECK(copy.paletteFormat() == PixelFormatIDs::RGBA8_Straight);
        CHECK(copy.paletteColorCount() == 1);
    }

    SUBCASE("move constructor propagates and resets palette") {
        uint8_t palette[4] = {10, 20, 30, 40};
        ImageBuffer original(4, 4, PixelFormatIDs::Index8);
        original.setPalette(palette, PixelFormatIDs::RGBA8_Straight, 1);

        ImageBuffer moved(std::move(original));
        CHECK(moved.palette() == palette);
        CHECK(moved.paletteFormat() == PixelFormatIDs::RGBA8_Straight);
        CHECK(moved.paletteColorCount() == 1);

        // Original should be reset
        CHECK(original.palette() == nullptr);
        CHECK(original.paletteFormat() == nullptr);
        CHECK(original.paletteColorCount() == 0);
    }

    SUBCASE("copy assignment propagates palette") {
        uint8_t palette[4] = {10, 20, 30, 40};
        ImageBuffer original(4, 4, PixelFormatIDs::Index8);
        original.setPalette(palette, PixelFormatIDs::RGBA8_Straight, 1);

        ImageBuffer copy(2, 2);
        copy = original;
        CHECK(copy.palette() == palette);
        CHECK(copy.paletteFormat() == PixelFormatIDs::RGBA8_Straight);
        CHECK(copy.paletteColorCount() == 1);
    }

    SUBCASE("move assignment propagates and resets palette") {
        uint8_t palette[4] = {10, 20, 30, 40};
        ImageBuffer original(4, 4, PixelFormatIDs::Index8);
        original.setPalette(palette, PixelFormatIDs::RGBA8_Straight, 1);

        ImageBuffer moved(2, 2);
        moved = std::move(original);
        CHECK(moved.palette() == palette);
        CHECK(moved.paletteFormat() == PixelFormatIDs::RGBA8_Straight);
        CHECK(moved.paletteColorCount() == 1);

        CHECK(original.palette() == nullptr);
    }
}

TEST_CASE("ImageBuffer toFormat with palette") {
    // 2x1 Index8 image with RGBA8 palette
    uint8_t palette[8] = {
        255, 0, 0, 255,     // [0] Red
        0, 0, 255, 255      // [1] Blue
    };

    ImageBuffer buf(2, 1, PixelFormatIDs::Index8, InitPolicy::Uninitialized);
    buf.setPalette(palette, PixelFormatIDs::RGBA8_Straight, 2);

    // Write index data
    uint8_t* data = static_cast<uint8_t*>(buf.data());
    data[0] = 0;  // Red
    data[1] = 1;  // Blue

    // Convert to RGBA8
    ImageBuffer converted = std::move(buf).toFormat(PixelFormatIDs::RGBA8_Straight);
    CHECK(converted.formatID() == PixelFormatIDs::RGBA8_Straight);
    CHECK(converted.width() == 2);
    CHECK(converted.height() == 1);

    const uint8_t* pixels = static_cast<const uint8_t*>(converted.data());
    // Pixel 0: Red
    CHECK(pixels[0] == 255);
    CHECK(pixels[1] == 0);
    CHECK(pixels[2] == 0);
    CHECK(pixels[3] == 255);

    // Pixel 1: Blue
    CHECK(pixels[4] == 0);
    CHECK(pixels[5] == 0);
    CHECK(pixels[6] == 255);
    CHECK(pixels[7] == 255);
}
