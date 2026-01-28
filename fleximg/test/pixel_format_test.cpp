// fleximg pixel_format.h Unit Tests
// ピクセルフォーマットDescriptor、変換のテスト

#include "doctest.h"
#include <string>
#include <vector>

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

    SUBCASE("expandIndex, toStraight, and fromStraight are set") {
        CHECK(fmt->expandIndex != nullptr);
        CHECK(fmt->toStraight != nullptr);   // grayscale fallback
        CHECK(fmt->fromStraight != nullptr);
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

TEST_CASE("PaletteData") {
    SUBCASE("default construction") {
        PaletteData pd;
        CHECK(pd.data == nullptr);
        CHECK(pd.format == nullptr);
        CHECK(pd.colorCount == 0);
        CHECK(static_cast<bool>(pd) == false);
    }

    SUBCASE("parameterized construction") {
        uint8_t data[4] = {1, 2, 3, 4};
        PaletteData pd(data, PixelFormatIDs::RGBA8_Straight, 1);
        CHECK(pd.data == data);
        CHECK(pd.format == PixelFormatIDs::RGBA8_Straight);
        CHECK(pd.colorCount == 1);
        CHECK(static_cast<bool>(pd) == true);
    }
}

TEST_CASE("ImageBuffer palette support") {
    SUBCASE("default auxInfo has null palette") {
        ImageBuffer buf(4, 4, PixelFormatIDs::RGBA8_Straight);
        CHECK(buf.auxInfo().palette == nullptr);
        CHECK(buf.auxInfo().paletteFormat == nullptr);
        CHECK(buf.auxInfo().paletteColorCount == 0);
    }

    SUBCASE("setPalette with individual args") {
        uint8_t palette[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        ImageBuffer buf(4, 4, PixelFormatIDs::Index8);
        buf.setPalette(palette, PixelFormatIDs::RGBA8_Straight, 2);

        CHECK(buf.auxInfo().palette == palette);
        CHECK(buf.auxInfo().paletteFormat == PixelFormatIDs::RGBA8_Straight);
        CHECK(buf.auxInfo().paletteColorCount == 2);
    }

    SUBCASE("setPalette with PaletteData") {
        uint8_t palette[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        PaletteData pd(palette, PixelFormatIDs::RGBA8_Straight, 2);
        ImageBuffer buf(4, 4, PixelFormatIDs::Index8);
        buf.setPalette(pd);

        CHECK(buf.auxInfo().palette == palette);
        CHECK(buf.auxInfo().paletteFormat == PixelFormatIDs::RGBA8_Straight);
        CHECK(buf.auxInfo().paletteColorCount == 2);
    }

    SUBCASE("copy constructor propagates auxInfo") {
        uint8_t palette[4] = {10, 20, 30, 40};
        ImageBuffer original(4, 4, PixelFormatIDs::Index8);
        original.setPalette(palette, PixelFormatIDs::RGBA8_Straight, 1);

        ImageBuffer copy(original);
        CHECK(copy.auxInfo().palette == palette);
        CHECK(copy.auxInfo().paletteFormat == PixelFormatIDs::RGBA8_Straight);
        CHECK(copy.auxInfo().paletteColorCount == 1);
    }

    SUBCASE("move constructor propagates and resets auxInfo") {
        uint8_t palette[4] = {10, 20, 30, 40};
        ImageBuffer original(4, 4, PixelFormatIDs::Index8);
        original.setPalette(palette, PixelFormatIDs::RGBA8_Straight, 1);

        ImageBuffer moved(std::move(original));
        CHECK(moved.auxInfo().palette == palette);
        CHECK(moved.auxInfo().paletteFormat == PixelFormatIDs::RGBA8_Straight);
        CHECK(moved.auxInfo().paletteColorCount == 1);

        // Original should be reset
        CHECK(original.auxInfo().palette == nullptr);
        CHECK(original.auxInfo().paletteFormat == nullptr);
        CHECK(original.auxInfo().paletteColorCount == 0);
    }

    SUBCASE("copy assignment propagates auxInfo") {
        uint8_t palette[4] = {10, 20, 30, 40};
        ImageBuffer original(4, 4, PixelFormatIDs::Index8);
        original.setPalette(palette, PixelFormatIDs::RGBA8_Straight, 1);

        ImageBuffer copy(2, 2);
        copy = original;
        CHECK(copy.auxInfo().palette == palette);
        CHECK(copy.auxInfo().paletteFormat == PixelFormatIDs::RGBA8_Straight);
        CHECK(copy.auxInfo().paletteColorCount == 1);
    }

    SUBCASE("move assignment propagates and resets auxInfo") {
        uint8_t palette[4] = {10, 20, 30, 40};
        ImageBuffer original(4, 4, PixelFormatIDs::Index8);
        original.setPalette(palette, PixelFormatIDs::RGBA8_Straight, 1);

        ImageBuffer moved(2, 2);
        moved = std::move(original);
        CHECK(moved.auxInfo().palette == palette);
        CHECK(moved.auxInfo().paletteFormat == PixelFormatIDs::RGBA8_Straight);
        CHECK(moved.auxInfo().paletteColorCount == 1);

        CHECK(original.auxInfo().palette == nullptr);
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

// =============================================================================
// Index8 fromStraight Tests (BT.601 luminance)
// =============================================================================

TEST_CASE("Index8 fromStraight (BT.601 luminance)") {
    SUBCASE("basic luminance values") {
        // Same as Grayscale8 fromStraight
        // White: (77*255 + 150*255 + 29*255 + 128) >> 8 = 255
        // Black: (77*0 + 150*0 + 29*0 + 128) >> 8 = 0
        // Red:   (77*255 + 150*0 + 29*0 + 128) >> 8 = 76 (note: (19635+128)/256 = 77.2 → 77)
        uint8_t src[16] = {
            255, 255, 255, 255,  // White
            0, 0, 0, 255,        // Black
            255, 0, 0, 255,      // Red
            0, 255, 0, 255       // Green
        };
        uint8_t dst[4] = {0};

        convertFormat(src, PixelFormatIDs::RGBA8_Straight,
                      dst, PixelFormatIDs::Index8, 4);

        CHECK(dst[0] == 255);   // White
        CHECK(dst[1] == 0);     // Black
        CHECK(dst[2] == 77);    // Red luminance
        CHECK(dst[3] == 149);   // Green luminance
    }

    SUBCASE("matches Grayscale8 fromStraight") {
        // Index8 and Grayscale8 should produce identical results
        uint8_t src[8] = {
            100, 150, 200, 255,
            50, 75, 25, 128
        };
        uint8_t dstIndex[2] = {0};
        uint8_t dstGray[2] = {0};

        convertFormat(src, PixelFormatIDs::RGBA8_Straight,
                      dstIndex, PixelFormatIDs::Index8, 2);
        convertFormat(src, PixelFormatIDs::RGBA8_Straight,
                      dstGray, PixelFormatIDs::Grayscale8, 2);

        CHECK(dstIndex[0] == dstGray[0]);
        CHECK(dstIndex[1] == dstGray[1]);
    }
}

// =============================================================================
// Index8 toStraight Tests (grayscale fallback without palette)
// =============================================================================

TEST_CASE("Index8 toStraight (grayscale fallback)") {
    SUBCASE("index values expand to grayscale RGBA") {
        // Index8 → RGBA8 without palette should treat indices as grayscale
        uint8_t src[4] = {0, 128, 255, 64};
        uint8_t dst[16] = {0};

        convertFormat(src, PixelFormatIDs::Index8,
                      dst, PixelFormatIDs::RGBA8_Straight, 4);

        // Pixel 0: index 0 → (0, 0, 0, 255)
        CHECK(dst[0] == 0);
        CHECK(dst[1] == 0);
        CHECK(dst[2] == 0);
        CHECK(dst[3] == 255);

        // Pixel 1: index 128 → (128, 128, 128, 255)
        CHECK(dst[4] == 128);
        CHECK(dst[5] == 128);
        CHECK(dst[6] == 128);
        CHECK(dst[7] == 255);

        // Pixel 2: index 255 → (255, 255, 255, 255)
        CHECK(dst[8] == 255);
        CHECK(dst[9] == 255);
        CHECK(dst[10] == 255);
        CHECK(dst[11] == 255);

        // Pixel 3: index 64 → (64, 64, 64, 255)
        CHECK(dst[12] == 64);
        CHECK(dst[13] == 64);
        CHECK(dst[14] == 64);
        CHECK(dst[15] == 255);
    }

    SUBCASE("roundtrip: RGBA8 → Index8 → RGBA8 without palette produces grayscale") {
        // Pure gray input: should roundtrip exactly
        uint8_t rgba[8] = {
            100, 100, 100, 255,    // gray 100
            200, 200, 200, 255     // gray 200
        };
        uint8_t index[2] = {0};
        uint8_t result[8] = {0};

        // RGBA8 → Index8 (BT.601: for pure gray, luminance == gray value)
        convertFormat(rgba, PixelFormatIDs::RGBA8_Straight,
                      index, PixelFormatIDs::Index8, 2);

        // Index8 → RGBA8 (without palette: grayscale fallback)
        convertFormat(index, PixelFormatIDs::Index8,
                      result, PixelFormatIDs::RGBA8_Straight, 2);

        // Pure gray should roundtrip exactly
        CHECK(result[0] == 100);
        CHECK(result[1] == 100);
        CHECK(result[2] == 100);
        CHECK(result[3] == 255);
        CHECK(result[4] == 200);
        CHECK(result[5] == 200);
        CHECK(result[6] == 200);
        CHECK(result[7] == 255);
    }
}

// =============================================================================
// Index8 convertFormat with palette Tests
// =============================================================================

TEST_CASE("Index8 convertFormat with palette") {
    SUBCASE("RGBA8 palette direct expand") {
        // Set up a 4-color RGBA8 palette
        uint8_t palette[16] = {
            255,   0,   0, 255,   // [0] Red
              0, 255,   0, 255,   // [1] Green
              0,   0, 255, 255,   // [2] Blue
            255, 255,   0, 255    // [3] Yellow
        };

        // Index8 source data
        uint8_t src[4] = {0, 1, 2, 3};  // Red, Green, Blue, Yellow

        // Set up auxInfo with palette
        PixelAuxInfo srcAux;
        srcAux.palette = palette;
        srcAux.paletteFormat = PixelFormatIDs::RGBA8_Straight;
        srcAux.paletteColorCount = 4;

        // Convert Index8 → RGBA8 with palette
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

        // Pixel 3: Yellow
        CHECK(dst[12] == 255);
        CHECK(dst[13] == 255);
        CHECK(dst[14] == 0);
        CHECK(dst[15] == 255);
    }

    SUBCASE("palette with index clamping") {
        // 2-color palette, but index values go beyond
        uint8_t palette[8] = {
            10, 20, 30, 255,   // [0]
            40, 50, 60, 255    // [1]
        };

        uint8_t src[3] = {0, 1, 5};  // index 5 exceeds palette size

        PixelAuxInfo srcAux;
        srcAux.palette = palette;
        srcAux.paletteFormat = PixelFormatIDs::RGBA8_Straight;
        srcAux.paletteColorCount = 2;

        uint8_t dst[12] = {0};
        convertFormat(src, PixelFormatIDs::Index8,
                      dst, PixelFormatIDs::RGBA8_Straight, 3,
                      &srcAux);

        // Index 0 → palette[0]
        CHECK(dst[0] == 10);
        CHECK(dst[1] == 20);
        CHECK(dst[2] == 30);
        CHECK(dst[3] == 255);

        // Index 1 → palette[1]
        CHECK(dst[4] == 40);
        CHECK(dst[5] == 50);
        CHECK(dst[6] == 60);
        CHECK(dst[7] == 255);

        // Index 5 → clamped to palette[1] (maxIdx = 1)
        CHECK(dst[8] == 40);
        CHECK(dst[9] == 50);
        CHECK(dst[10] == 60);
        CHECK(dst[11] == 255);
    }

    SUBCASE("pipeline simulation: RGBA8 → Index8 → RGBA8 with grayscale palette") {
        // Simulate the exact WebUI pipeline:
        // 1. Convert RGBA8 image → Index8 (BT.601 luminance)
        // 2. Create Grayscale256 palette
        // 3. Convert Index8 → RGBA8 with palette

        // Step 1: RGBA8 source (4 pixels)
        uint8_t rgba_src[16] = {
            255, 255, 255, 255,  // White → luminance 255
              0,   0,   0, 255,  // Black → luminance 0
            255,   0,   0, 255,  // Red → luminance ~77
              0, 255,   0, 255   // Green → luminance ~150
        };

        // Convert to Index8
        uint8_t index_data[4] = {0};
        convertFormat(rgba_src, PixelFormatIDs::RGBA8_Straight,
                      index_data, PixelFormatIDs::Index8, 4);

        // Verify luminance values
        CHECK(index_data[0] == 255);  // White
        CHECK(index_data[1] == 0);    // Black

        // Step 2: Create Grayscale256 palette (same as WebUI preset)
        uint8_t palette[256 * 4];
        for (int i = 0; i < 256; i++) {
            palette[i * 4 + 0] = static_cast<uint8_t>(i);  // R
            palette[i * 4 + 1] = static_cast<uint8_t>(i);  // G
            palette[i * 4 + 2] = static_cast<uint8_t>(i);  // B
            palette[i * 4 + 3] = 255;                       // A
        }

        // Step 3: Convert Index8 → RGBA8 with palette
        PixelAuxInfo srcAux;
        srcAux.palette = palette;
        srcAux.paletteFormat = PixelFormatIDs::RGBA8_Straight;
        srcAux.paletteColorCount = 256;

        uint8_t result[16] = {0};
        convertFormat(index_data, PixelFormatIDs::Index8,
                      result, PixelFormatIDs::RGBA8_Straight, 4,
                      &srcAux);

        // White → index 255 → palette[255] = (255, 255, 255, 255)
        CHECK(result[0] == 255);
        CHECK(result[1] == 255);
        CHECK(result[2] == 255);
        CHECK(result[3] == 255);

        // Black → index 0 → palette[0] = (0, 0, 0, 255)
        CHECK(result[4] == 0);
        CHECK(result[5] == 0);
        CHECK(result[6] == 0);
        CHECK(result[7] == 255);

        // Red → index ~77 → palette[77] = (77, 77, 77, 255)
        CHECK(result[8] == index_data[2]);
        CHECK(result[9] == index_data[2]);
        CHECK(result[10] == index_data[2]);
        CHECK(result[11] == 255);

        // Green → index ~150 → palette[150] = (150, 150, 150, 255)
        CHECK(result[12] == index_data[3]);
        CHECK(result[13] == index_data[3]);
        CHECK(result[14] == index_data[3]);
        CHECK(result[15] == 255);
    }

    SUBCASE("without palette falls back to grayscale (not stale buffer)") {
        // This test verifies that convertFormat doesn't use stale buffer data
        // when converting Index8 → RGBA8 without palette.
        // Previously, toStraight was nullptr causing conversionBuffer to retain
        // old data from a prior convertFormat call.

        // First call: fill conversionBuffer with distinctive data
        uint8_t dummy_src[4] = {0xDE, 0xAD, 0xBE, 0xEF};
        uint8_t dummy_dst[1] = {0};
        convertFormat(dummy_src, PixelFormatIDs::RGBA8_Straight,
                      dummy_dst, PixelFormatIDs::Index8, 1);

        // Second call: Index8 → RGBA8 without palette
        uint8_t src[2] = {42, 200};
        uint8_t dst[8] = {0};
        convertFormat(src, PixelFormatIDs::Index8,
                      dst, PixelFormatIDs::RGBA8_Straight, 2);

        // Should NOT contain stale data (0xDE, 0xAD, 0xBE, 0xEF)
        // Should be grayscale: index → (index, index, index, 255)
        CHECK(dst[0] == 42);
        CHECK(dst[1] == 42);
        CHECK(dst[2] == 42);
        CHECK(dst[3] == 255);
        CHECK(dst[4] == 200);
        CHECK(dst[5] == 200);
        CHECK(dst[6] == 200);
        CHECK(dst[7] == 255);
    }
}

// =============================================================================
// FormatConverter / resolveConverter Tests
// =============================================================================

TEST_CASE("resolveConverter: null format handling") {
    auto conv = resolveConverter(nullptr, PixelFormatIDs::RGBA8_Straight);
    CHECK_FALSE(conv);

    conv = resolveConverter(PixelFormatIDs::RGBA8_Straight, nullptr);
    CHECK_FALSE(conv);

    conv = resolveConverter(nullptr, nullptr);
    CHECK_FALSE(conv);
}

TEST_CASE("resolveConverter: same format (memcpy path)") {
    const PixelFormatID formats[] = {
        PixelFormatIDs::RGBA8_Straight,
        PixelFormatIDs::RGB565_LE,
        PixelFormatIDs::RGB565_BE,
        PixelFormatIDs::RGB332,
        PixelFormatIDs::RGB888,
        PixelFormatIDs::Alpha8,
        PixelFormatIDs::Grayscale8,
        PixelFormatIDs::Index8,
    };

    for (auto fmt : formats) {
        CAPTURE(fmt->name);
        auto conv = resolveConverter(fmt, fmt);
        REQUIRE(conv);

        // 変換結果が memcpy と一致すること
        uint8_t src[16] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
                           0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        uint8_t dst[16] = {0};
        int bpp = (fmt->bitsPerPixel + 7) / 8;
        int count = 16 / bpp;
        if (count > 0) {
            conv(dst, src, count);
            CHECK(std::memcmp(dst, src,
                static_cast<size_t>(count) * static_cast<size_t>(bpp)) == 0);
        }
    }
}

TEST_CASE("resolveConverter: endian sibling (swapEndian path)") {
    auto conv = resolveConverter(PixelFormatIDs::RGB565_LE,
                                 PixelFormatIDs::RGB565_BE);
    REQUIRE(conv);

    // RGB565_LE → RGB565_BE: バイトスワップ
    uint8_t src[4] = {0x1F, 0xF8, 0xE0, 0x07};  // 2 pixels
    uint8_t dst[4] = {0};
    conv(dst, src, 2);

    CHECK(dst[0] == 0xF8);
    CHECK(dst[1] == 0x1F);
    CHECK(dst[2] == 0x07);
    CHECK(dst[3] == 0xE0);

    // 逆方向も確認
    auto conv2 = resolveConverter(PixelFormatIDs::RGB565_BE,
                                  PixelFormatIDs::RGB565_LE);
    REQUIRE(conv2);

    uint8_t dst2[4] = {0};
    conv2(dst2, dst, 2);
    CHECK(std::memcmp(dst2, src, 4) == 0);
}

TEST_CASE("resolveConverter: src==RGBA8 (direct fromStraight)") {
    SUBCASE("RGBA8 → RGB565_LE") {
        auto conv = resolveConverter(PixelFormatIDs::RGBA8_Straight,
                                     PixelFormatIDs::RGB565_LE);
        REQUIRE(conv);

        // 赤 (255,0,0,255) → RGB565_LE
        uint8_t src[4] = {255, 0, 0, 255};
        uint8_t dst[2] = {0};
        conv(dst, src, 1);

        // convertFormat と一致確認
        uint8_t ref[2] = {0};
        convertFormat(src, PixelFormatIDs::RGBA8_Straight,
                      ref, PixelFormatIDs::RGB565_LE, 1);
        CHECK(dst[0] == ref[0]);
        CHECK(dst[1] == ref[1]);
    }

    SUBCASE("RGBA8 → Grayscale8") {
        auto conv = resolveConverter(PixelFormatIDs::RGBA8_Straight,
                                     PixelFormatIDs::Grayscale8);
        REQUIRE(conv);

        uint8_t src[4] = {100, 150, 200, 255};
        uint8_t dst[1] = {0};
        conv(dst, src, 1);

        uint8_t ref[1] = {0};
        convertFormat(src, PixelFormatIDs::RGBA8_Straight,
                      ref, PixelFormatIDs::Grayscale8, 1);
        CHECK(dst[0] == ref[0]);
    }
}

TEST_CASE("resolveConverter: dst==RGBA8 (direct toStraight)") {
    SUBCASE("RGB565_LE → RGBA8") {
        auto conv = resolveConverter(PixelFormatIDs::RGB565_LE,
                                     PixelFormatIDs::RGBA8_Straight);
        REQUIRE(conv);

        // 赤 RGB565_LE
        uint8_t src[2] = {0x00, 0xF8};  // R=31, G=0, B=0
        uint8_t dst[4] = {0};
        conv(dst, src, 1);

        uint8_t ref[4] = {0};
        convertFormat(src, PixelFormatIDs::RGB565_LE,
                      ref, PixelFormatIDs::RGBA8_Straight, 1);
        CHECK(std::memcmp(dst, ref, 4) == 0);
    }

    SUBCASE("Grayscale8 → RGBA8") {
        auto conv = resolveConverter(PixelFormatIDs::Grayscale8,
                                     PixelFormatIDs::RGBA8_Straight);
        REQUIRE(conv);

        uint8_t src[1] = {128};
        uint8_t dst[4] = {0};
        conv(dst, src, 1);

        CHECK(dst[0] == 128);
        CHECK(dst[1] == 128);
        CHECK(dst[2] == 128);
        CHECK(dst[3] == 255);
    }
}

TEST_CASE("resolveConverter: general 2-stage (toStraight + fromStraight)") {
    SUBCASE("RGB565_LE → RGB332") {
        auto conv = resolveConverter(PixelFormatIDs::RGB565_LE,
                                     PixelFormatIDs::RGB332);
        REQUIRE(conv);

        // 複数ピクセルで convertFormat と比較
        uint8_t src[8];  // 4 pixels RGB565_LE
        for (int i = 0; i < 8; i++) src[i] = static_cast<uint8_t>(i * 31);

        uint8_t dst[4] = {0};
        uint8_t ref[4] = {0};
        conv(dst, src, 4);
        convertFormat(src, PixelFormatIDs::RGB565_LE,
                      ref, PixelFormatIDs::RGB332, 4);
        CHECK(std::memcmp(dst, ref, 4) == 0);
    }

    SUBCASE("Grayscale8 → RGB565_LE") {
        auto conv = resolveConverter(PixelFormatIDs::Grayscale8,
                                     PixelFormatIDs::RGB565_LE);
        REQUIRE(conv);

        uint8_t src[3] = {0, 128, 255};
        uint8_t dst[6] = {0};
        uint8_t ref[6] = {0};
        conv(dst, src, 3);
        convertFormat(src, PixelFormatIDs::Grayscale8,
                      ref, PixelFormatIDs::RGB565_LE, 3);
        CHECK(std::memcmp(dst, ref, 6) == 0);
    }

    SUBCASE("RGB888 → Alpha8") {
        auto conv = resolveConverter(PixelFormatIDs::RGB888,
                                     PixelFormatIDs::Alpha8);
        REQUIRE(conv);

        uint8_t src[6] = {100, 150, 200, 50, 75, 100};
        uint8_t dst[2] = {0};
        uint8_t ref[2] = {0};
        conv(dst, src, 2);
        convertFormat(src, PixelFormatIDs::RGB888,
                      ref, PixelFormatIDs::Alpha8, 2);
        CHECK(std::memcmp(dst, ref, 2) == 0);
    }
}

TEST_CASE("resolveConverter: Index8 with palette") {
    // RGBA8 パレット: 赤, 緑, 青, 白
    uint8_t palette[16] = {
        255, 0,   0,   255,  // [0] 赤
        0,   255, 0,   255,  // [1] 緑
        0,   0,   255, 255,  // [2] 青
        255, 255, 255, 255,  // [3] 白
    };
    PixelAuxInfo aux;
    aux.palette = palette;
    aux.paletteFormat = PixelFormatIDs::RGBA8_Straight;
    aux.paletteColorCount = 4;

    SUBCASE("palFmt == dstFmt (direct expand)") {
        auto conv = resolveConverter(PixelFormatIDs::Index8,
                                     PixelFormatIDs::RGBA8_Straight,
                                     &aux);
        REQUIRE(conv);

        uint8_t src[3] = {0, 2, 3};
        uint8_t dst[12] = {0};
        conv(dst, src, 3);

        // [0] = 赤
        CHECK(dst[0] == 255); CHECK(dst[1] == 0);
        CHECK(dst[2] == 0);   CHECK(dst[3] == 255);
        // [2] = 青
        CHECK(dst[4] == 0);   CHECK(dst[5] == 0);
        CHECK(dst[6] == 255); CHECK(dst[7] == 255);
        // [3] = 白
        CHECK(dst[8] == 255);  CHECK(dst[9] == 255);
        CHECK(dst[10] == 255); CHECK(dst[11] == 255);
    }

    SUBCASE("palFmt == RGBA8, dst != RGBA8 (expandIndex + fromStraight)") {
        auto conv = resolveConverter(PixelFormatIDs::Index8,
                                     PixelFormatIDs::RGB565_LE,
                                     &aux);
        REQUIRE(conv);

        uint8_t src[4] = {0, 1, 2, 3};
        uint8_t dst[8] = {0};
        uint8_t ref[8] = {0};
        conv(dst, src, 4);
        convertFormat(src, PixelFormatIDs::Index8,
                      ref, PixelFormatIDs::RGB565_LE, 4, &aux);
        CHECK(std::memcmp(dst, ref, 8) == 0);
    }

    SUBCASE("palFmt != RGBA8 (expandIndex + toStraight + fromStraight)") {
        // RGB565 パレット
        uint8_t rgb565_palette[8] = {
            0x00, 0xF8,  // [0] 赤 (RGB565_LE)
            0xE0, 0x07,  // [1] 緑
            0x1F, 0x00,  // [2] 青
            0xFF, 0xFF,  // [3] 白
        };
        PixelAuxInfo rgb565_aux;
        rgb565_aux.palette = rgb565_palette;
        rgb565_aux.paletteFormat = PixelFormatIDs::RGB565_LE;
        rgb565_aux.paletteColorCount = 4;

        auto conv = resolveConverter(PixelFormatIDs::Index8,
                                     PixelFormatIDs::RGB332,
                                     &rgb565_aux);
        REQUIRE(conv);

        uint8_t src[4] = {0, 1, 2, 3};
        uint8_t dst[4] = {0};
        uint8_t ref[4] = {0};
        conv(dst, src, 4);
        convertFormat(src, PixelFormatIDs::Index8,
                      ref, PixelFormatIDs::RGB332, 4, &rgb565_aux);
        CHECK(std::memcmp(dst, ref, 4) == 0);
    }

    SUBCASE("Index8 without palette (toStraight fallback)") {
        auto conv = resolveConverter(PixelFormatIDs::Index8,
                                     PixelFormatIDs::RGBA8_Straight);
        REQUIRE(conv);

        uint8_t src[2] = {42, 200};
        uint8_t dst[8] = {0};
        conv(dst, src, 2);

        // グレースケールフォールバック
        CHECK(dst[0] == 42);  CHECK(dst[1] == 42);
        CHECK(dst[2] == 42);  CHECK(dst[3] == 255);
        CHECK(dst[4] == 200); CHECK(dst[5] == 200);
        CHECK(dst[6] == 200); CHECK(dst[7] == 255);
    }
}

TEST_CASE("resolveConverter: all format pairs match convertFormat") {
    // 全フォーマット組み合わせで resolveConverter と convertFormat の出力を比較
    const struct {
        PixelFormatID id;
        const char* name;
    } formats[] = {
        {PixelFormatIDs::RGBA8_Straight, "RGBA8"},
        {PixelFormatIDs::RGB565_LE, "RGB565_LE"},
        {PixelFormatIDs::RGB565_BE, "RGB565_BE"},
        {PixelFormatIDs::RGB332, "RGB332"},
        {PixelFormatIDs::RGB888, "RGB888"},
        {PixelFormatIDs::BGR888, "BGR888"},
        {PixelFormatIDs::Alpha8, "Alpha8"},
        {PixelFormatIDs::Grayscale8, "Grayscale8"},
    };

    // テスト用 RGBA8 ソースデータ（4ピクセル）
    const uint8_t rgba_src[16] = {
        255, 0,   0,   255,  // 赤
        0,   255, 0,   200,  // 緑（半透明）
        0,   0,   255, 128,  // 青（半透明）
        128, 128, 128, 255,  // グレー
    };

    for (auto& srcFmt : formats) {
        // srcデータを作成（RGBA8からsrcFormatに変換）
        uint8_t src_buf[16] = {0};
        convertFormat(rgba_src, PixelFormatIDs::RGBA8_Straight,
                      src_buf, srcFmt.id, 4);

        for (auto& dstFmt : formats) {
            CAPTURE(srcFmt.name);
            CAPTURE(dstFmt.name);

            auto conv = resolveConverter(srcFmt.id, dstFmt.id);
            REQUIRE(conv);

            int dstBpp = (dstFmt.id->bitsPerPixel + 7) / 8;
            uint8_t dst_conv[16] = {0};
            uint8_t dst_ref[16] = {0};

            conv(dst_conv, src_buf, 4);
            convertFormat(src_buf, srcFmt.id,
                          dst_ref, dstFmt.id, 4);

            CHECK(std::memcmp(dst_conv, dst_ref,
                static_cast<size_t>(4) * static_cast<size_t>(dstBpp)) == 0);
        }
    }
}

TEST_CASE("resolveConverter: operator bool") {
    SUBCASE("valid converter is truthy") {
        auto conv = resolveConverter(PixelFormatIDs::RGBA8_Straight,
                                     PixelFormatIDs::RGB565_LE);
        CHECK(static_cast<bool>(conv));
        CHECK(conv.func != nullptr);
    }

    SUBCASE("invalid converter is falsy") {
        FormatConverter empty;
        CHECK_FALSE(static_cast<bool>(empty));
        CHECK(empty.func == nullptr);
    }
}

TEST_CASE("resolveConverter: large pixel count") {
    auto conv = resolveConverter(PixelFormatIDs::RGB565_LE,
                                 PixelFormatIDs::RGBA8_Straight);
    REQUIRE(conv);

    const int count = 320;
    std::vector<uint8_t> src(static_cast<size_t>(count) * 2);
    for (size_t i = 0; i < src.size(); i++) {
        src[i] = static_cast<uint8_t>(i & 0xFF);
    }

    std::vector<uint8_t> dst_conv(static_cast<size_t>(count) * 4, 0);
    std::vector<uint8_t> dst_ref(static_cast<size_t>(count) * 4, 0);

    conv(dst_conv.data(), src.data(), count);
    convertFormat(src.data(), PixelFormatIDs::RGB565_LE,
                  dst_ref.data(), PixelFormatIDs::RGBA8_Straight, count);

    CHECK(dst_conv == dst_ref);
}

TEST_CASE("resolveConverter: custom allocator") {
    // カスタムアロケータを渡しても正しく動作すること
    core::memory::DefaultAllocator alloc;

    auto conv = resolveConverter(PixelFormatIDs::RGB565_LE,
                                 PixelFormatIDs::RGB332,
                                 nullptr, &alloc);
    REQUIRE(conv);
    CHECK(conv.ctx.allocator == &alloc);

    uint8_t src[4] = {0x00, 0xF8, 0xFF, 0xFF};
    uint8_t dst[2] = {0};
    uint8_t ref[2] = {0};
    conv(dst, src, 2);
    convertFormat(src, PixelFormatIDs::RGB565_LE,
                  ref, PixelFormatIDs::RGB332, 2);
    CHECK(std::memcmp(dst, ref, 2) == 0);
}
