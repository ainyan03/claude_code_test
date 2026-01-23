// fleximg ViewPort Unit Tests
// ViewPort構造体のテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/image/viewport.h"
#include "fleximg/image/image_buffer.h"

using namespace fleximg;

// =============================================================================
// ViewPort Construction Tests
// =============================================================================

TEST_CASE("ViewPort default construction") {
    ViewPort v;
    CHECK(v.data == nullptr);
    CHECK(v.width == 0);
    CHECK(v.height == 0);
    CHECK(v.stride == 0);
    CHECK_FALSE(v.isValid());
}

TEST_CASE("ViewPort direct construction") {
    uint8_t buffer[400];  // 10x10 RGBA8
    ViewPort v(buffer, PixelFormatIDs::RGBA8_Straight, 40, 10, 10);

    CHECK(v.data == buffer);
    CHECK(v.formatID == PixelFormatIDs::RGBA8_Straight);
    CHECK(v.stride == 40);
    CHECK(v.width == 10);
    CHECK(v.height == 10);
    CHECK(v.isValid());
}

TEST_CASE("ViewPort simple construction with auto stride") {
    uint8_t buffer[400];
    ViewPort v(buffer, 10, 10, PixelFormatIDs::RGBA8_Straight);

    CHECK(v.data == buffer);
    CHECK(v.width == 10);
    CHECK(v.height == 10);
    CHECK(v.stride == 40);  // 10 * 4 bytes
    CHECK(v.isValid());
}

#ifdef FLEXIMG_ENABLE_PREMUL
TEST_CASE("ViewPort with RGBA16 format") {
    uint8_t buffer[800];  // 10x10 RGBA16
    ViewPort v(buffer, 10, 10, PixelFormatIDs::RGBA16_Premultiplied);

    CHECK(v.stride == 80);  // 10 * 8 bytes
    CHECK(v.bytesPerPixel() == 8);
}
#endif

// =============================================================================
// ViewPort Validity Tests
// =============================================================================

TEST_CASE("ViewPort validity") {
    uint8_t buffer[100];

    SUBCASE("null data is invalid") {
        ViewPort v(nullptr, 10, 10, PixelFormatIDs::RGBA8_Straight);
        CHECK_FALSE(v.isValid());
    }

    SUBCASE("zero width is invalid") {
        ViewPort v(buffer, 0, 10, PixelFormatIDs::RGBA8_Straight);
        CHECK_FALSE(v.isValid());
    }

    SUBCASE("zero height is invalid") {
        ViewPort v(buffer, 10, 0, PixelFormatIDs::RGBA8_Straight);
        CHECK_FALSE(v.isValid());
    }

    SUBCASE("valid viewport") {
        ViewPort v(buffer, 5, 5, PixelFormatIDs::RGBA8_Straight);
        CHECK(v.isValid());
    }
}

// =============================================================================
// Pixel Access Tests
// =============================================================================

TEST_CASE("ViewPort pixelAt") {
    uint8_t buffer[16] = {0};  // 2x2 RGBA8
    ViewPort v(buffer, 2, 2, PixelFormatIDs::RGBA8_Straight);

    SUBCASE("pixelAt returns correct address") {
        CHECK(v.pixelAt(0, 0) == buffer);
        CHECK(v.pixelAt(1, 0) == buffer + 4);
        CHECK(v.pixelAt(0, 1) == buffer + 8);
        CHECK(v.pixelAt(1, 1) == buffer + 12);
    }

    SUBCASE("write and read pixel") {
        uint8_t* pixel = static_cast<uint8_t*>(v.pixelAt(1, 1));
        pixel[0] = 255;  // R
        pixel[1] = 128;  // G
        pixel[2] = 64;   // B
        pixel[3] = 255;  // A

        const uint8_t* readPixel = static_cast<const uint8_t*>(v.pixelAt(1, 1));
        CHECK(readPixel[0] == 255);
        CHECK(readPixel[1] == 128);
        CHECK(readPixel[2] == 64);
        CHECK(readPixel[3] == 255);
    }
}

TEST_CASE("ViewPort pixelAt with custom stride") {
    // stride > width * bpp のケース（パディングあり）
    uint8_t buffer[64] = {0};  // 2x2 with 32-byte stride
    ViewPort v(buffer, PixelFormatIDs::RGBA8_Straight, 32, 2, 2);

    CHECK(v.pixelAt(0, 0) == buffer);
    CHECK(v.pixelAt(1, 0) == buffer + 4);
    CHECK(v.pixelAt(0, 1) == buffer + 32);  // next row at stride offset
    CHECK(v.pixelAt(1, 1) == buffer + 36);
}

// =============================================================================
// Byte Info Tests
// =============================================================================

TEST_CASE("ViewPort byte info") {
    uint8_t buffer[100];

    SUBCASE("bytesPerPixel for RGBA8") {
        ViewPort v(buffer, 10, 10, PixelFormatIDs::RGBA8_Straight);
        CHECK(v.bytesPerPixel() == 4);
    }

#ifdef FLEXIMG_ENABLE_PREMUL
    SUBCASE("bytesPerPixel for RGBA16") {
        ViewPort v(buffer, 5, 5, PixelFormatIDs::RGBA16_Premultiplied);
        CHECK(v.bytesPerPixel() == 8);
    }
#endif

    SUBCASE("rowBytes with positive stride") {
        ViewPort v(buffer, PixelFormatIDs::RGBA8_Straight, 48, 10, 10);
        CHECK(v.rowBytes() == 48);
    }

    SUBCASE("rowBytes with negative stride (Y-flip)") {
        ViewPort v(buffer, PixelFormatIDs::RGBA8_Straight, -48, 10, 10);
        CHECK(v.rowBytes() == 40);  // width * bpp
    }
}

// =============================================================================
// subView Tests
// =============================================================================

TEST_CASE("view_ops::subView") {
    uint8_t buffer[400] = {0};  // 10x10 RGBA8
    ViewPort v(buffer, 10, 10, PixelFormatIDs::RGBA8_Straight);

    SUBCASE("subView at origin") {
        auto sub = view_ops::subView(v, 0, 0, 5, 5);
        CHECK(sub.data == buffer);
        CHECK(sub.width == 5);
        CHECK(sub.height == 5);
        CHECK(sub.stride == v.stride);
        CHECK(sub.formatID == v.formatID);
    }

    SUBCASE("subView with offset") {
        auto sub = view_ops::subView(v, 2, 3, 4, 4);
        CHECK(sub.data == v.pixelAt(2, 3));
        CHECK(sub.width == 4);
        CHECK(sub.height == 4);
        CHECK(sub.stride == v.stride);
    }

#ifdef FLEXIMG_ENABLE_PREMUL
    SUBCASE("subView preserves format") {
        ViewPort v16(buffer, 5, 5, PixelFormatIDs::RGBA16_Premultiplied);
        auto sub = view_ops::subView(v16, 1, 1, 3, 3);
        CHECK(sub.formatID == PixelFormatIDs::RGBA16_Premultiplied);
    }
#endif
}
