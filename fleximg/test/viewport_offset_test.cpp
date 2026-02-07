// fleximg ViewPort Offset Tests
// ViewPortのx,yオフセット機能の検証（Plan B実装）

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/image/image_buffer.h"
#include "fleximg/image/pixel_format.h"

using namespace fleximg;

// =============================================================================
// Helper Functions - bit-packed formats
// =============================================================================

// bit-packedフォーマット用のピクセル設定
// MSB/LSB両対応
// ViewPortのx,yオフセットを考慮した絶対座標でアクセス
inline void setPixelBitPacked(ImageBuffer& img, int localX, int localY, uint8_t value) {
    const ViewPort& v = img.viewRef();
    auto fmt = v.formatID;
    int bitsPerPixel = fmt->bitsPerPixel;

    // ViewPortのオフセットを加算してグローバル座標を計算
    int globalX = v.x + localX;
    int globalY = v.y + localY;

    // グローバル座標からビット位置を計算
    int bitIndex = globalX * bitsPerPixel;
    int byteIndex = bitIndex / 8;
    int bitOffset = bitIndex % 8;

    uint8_t* row = static_cast<uint8_t*>(v.data) + globalY * v.stride;
    uint8_t mask = static_cast<uint8_t>((1 << bitsPerPixel) - 1);

    if (fmt == PixelFormatIDs::Index1_MSB ||
        fmt == PixelFormatIDs::Index2_MSB ||
        fmt == PixelFormatIDs::Index4_MSB) {
        // MSB: 上位ビットから
        int shift = 8 - bitOffset - bitsPerPixel;
        row[byteIndex] = static_cast<uint8_t>(
            (row[byteIndex] & ~(mask << shift)) | ((value & mask) << shift)
        );
    } else {
        // LSB: 下位ビットから
        row[byteIndex] = static_cast<uint8_t>(
            (row[byteIndex] & ~(mask << bitOffset)) | ((value & mask) << bitOffset)
        );
    }
}

// bit-packedフォーマット用のピクセル取得
// ViewPortのx,yオフセットを考慮した絶対座標でアクセス
inline uint8_t getPixelBitPacked(const ImageBuffer& img, int localX, int localY) {
    const ViewPort& v = img.viewRef();
    auto fmt = v.formatID;
    int bitsPerPixel = fmt->bitsPerPixel;

    // ViewPortのオフセットを加算してグローバル座標を計算
    int globalX = v.x + localX;
    int globalY = v.y + localY;

    // グローバル座標からビット位置を計算
    int bitIndex = globalX * bitsPerPixel;
    int byteIndex = bitIndex / 8;
    int bitOffset = bitIndex % 8;

    const uint8_t* row = static_cast<const uint8_t*>(v.data) + globalY * v.stride;
    uint8_t mask = static_cast<uint8_t>((1 << bitsPerPixel) - 1);

    if (fmt == PixelFormatIDs::Index1_MSB ||
        fmt == PixelFormatIDs::Index2_MSB ||
        fmt == PixelFormatIDs::Index4_MSB) {
        // MSB: 上位ビットから
        int shift = 8 - bitOffset - bitsPerPixel;
        return static_cast<uint8_t>((row[byteIndex] >> shift) & mask);
    } else {
        // LSB: 下位ビットから
        return static_cast<uint8_t>((row[byteIndex] >> bitOffset) & mask);
    }
}

// =============================================================================
// Helper Functions - byte-aligned formats
// =============================================================================

// Index8/Alpha8/Grayscale8用のピクセル設定
inline void setPixelU8(ImageBuffer& img, int x, int y, uint8_t value) {
    uint8_t* pixel = static_cast<uint8_t*>(img.pixelAt(x, y));
    *pixel = value;
}

// Index8/Alpha8/Grayscale8用のピクセル取得
inline uint8_t getPixelU8(const ImageBuffer& img, int x, int y) {
    const uint8_t* pixel = static_cast<const uint8_t*>(img.pixelAt(x, y));
    return *pixel;
}

// RGBA8用のピクセル設定
inline void setPixelRGBA8(ImageBuffer& img, int x, int y, uint32_t rgba) {
    uint8_t* pixel = static_cast<uint8_t*>(img.pixelAt(x, y));
    pixel[0] = static_cast<uint8_t>(rgba >> 24);  // R
    pixel[1] = static_cast<uint8_t>(rgba >> 16);  // G
    pixel[2] = static_cast<uint8_t>(rgba >> 8);   // B
    pixel[3] = static_cast<uint8_t>(rgba);        // A
}

// RGBA8用のピクセル取得
inline uint32_t getPixelRGBA8(const ImageBuffer& img, int x, int y) {
    const uint8_t* pixel = static_cast<const uint8_t*>(img.pixelAt(x, y));
    return (static_cast<uint32_t>(pixel[0]) << 24) |
           (static_cast<uint32_t>(pixel[1]) << 16) |
           (static_cast<uint32_t>(pixel[2]) << 8) |
           static_cast<uint32_t>(pixel[3]);
}

// RGB565用のピクセル設定
inline void setPixelRGB565(ImageBuffer& img, int x, int y, uint16_t rgb) {
    uint16_t* pixel = static_cast<uint16_t*>(img.pixelAt(x, y));
    *pixel = rgb;
}

// RGB565用のピクセル取得
inline uint16_t getPixelRGB565(const ImageBuffer& img, int x, int y) {
    const uint16_t* pixel = static_cast<const uint16_t*>(img.pixelAt(x, y));
    return *pixel;
}

// =============================================================================
// Phase 1: bit-packed formats (最重要)
// =============================================================================

TEST_CASE("ViewPort offset - Index1_MSB") {
    constexpr uint8_t DOT = 1;
    constexpr uint8_t BG = 0;
    auto format = PixelFormatIDs::Index1_MSB;

    SUBCASE("baseline x=0,y=0") {
        ImageBuffer img(11, 11, format, InitPolicy::Zero);
        setPixelBitPacked(img, 5, 5, DOT);

        ImageBuffer sub = img.subBuffer(0, 0, 11, 11);
        CHECK(getPixelBitPacked(sub, 5, 5) == DOT);
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
        CHECK(getPixelBitPacked(sub, 10, 10) == BG);
    }

    SUBCASE("small offset x=3,y=3") {
        ImageBuffer img(11, 11, format, InitPolicy::Zero);
        setPixelBitPacked(img, 5, 5, DOT);

        ImageBuffer sub = img.subBuffer(3, 3, 5, 5);
        CHECK(getPixelBitPacked(sub, 2, 2) == DOT);  // (3+2, 3+2) = (5,5)
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
        CHECK(getPixelBitPacked(sub, 4, 4) == BG);
    }

    SUBCASE("byte boundary x=8,y=8") {
        ImageBuffer img(15, 15, format, InitPolicy::Zero);
        setPixelBitPacked(img, 10, 10, DOT);

        ImageBuffer sub = img.subBuffer(8, 8, 5, 5);
        CHECK(getPixelBitPacked(sub, 2, 2) == DOT);  // (8+2, 8+2) = (10,10)
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
        CHECK(getPixelBitPacked(sub, 4, 4) == BG);
    }

    SUBCASE("large offset x=9,y=9") {
        ImageBuffer img(17, 17, format, InitPolicy::Zero);
        setPixelBitPacked(img, 11, 11, DOT);

        ImageBuffer sub = img.subBuffer(9, 9, 5, 5);
        CHECK(getPixelBitPacked(sub, 2, 2) == DOT);  // (9+2, 9+2) = (11,11)
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
        CHECK(getPixelBitPacked(sub, 4, 4) == BG);
    }
}

TEST_CASE("ViewPort offset - Index1_LSB") {
    constexpr uint8_t DOT = 1;
    constexpr uint8_t BG = 0;
    auto format = PixelFormatIDs::Index1_LSB;

    SUBCASE("baseline x=0,y=0") {
        ImageBuffer img(11, 11, format, InitPolicy::Zero);
        setPixelBitPacked(img, 5, 5, DOT);

        ImageBuffer sub = img.subBuffer(0, 0, 11, 11);
        CHECK(getPixelBitPacked(sub, 5, 5) == DOT);
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
    }

    SUBCASE("small offset x=3,y=3") {
        ImageBuffer img(11, 11, format, InitPolicy::Zero);
        setPixelBitPacked(img, 5, 5, DOT);

        ImageBuffer sub = img.subBuffer(3, 3, 5, 5);
        CHECK(getPixelBitPacked(sub, 2, 2) == DOT);
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
    }

    SUBCASE("byte boundary x=8,y=8") {
        ImageBuffer img(15, 15, format, InitPolicy::Zero);
        setPixelBitPacked(img, 10, 10, DOT);

        ImageBuffer sub = img.subBuffer(8, 8, 5, 5);
        CHECK(getPixelBitPacked(sub, 2, 2) == DOT);
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
    }

    SUBCASE("large offset x=9,y=9") {
        ImageBuffer img(17, 17, format, InitPolicy::Zero);
        setPixelBitPacked(img, 11, 11, DOT);

        ImageBuffer sub = img.subBuffer(9, 9, 5, 5);
        CHECK(getPixelBitPacked(sub, 2, 2) == DOT);
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
    }
}

TEST_CASE("ViewPort offset - Index2_MSB") {
    constexpr uint8_t DOT = 3;  // 2ビット最大値
    constexpr uint8_t BG = 0;
    auto format = PixelFormatIDs::Index2_MSB;

    SUBCASE("small offset x=3,y=3") {
        ImageBuffer img(11, 11, format, InitPolicy::Zero);
        setPixelBitPacked(img, 5, 5, DOT);

        ImageBuffer sub = img.subBuffer(3, 3, 5, 5);
        CHECK(getPixelBitPacked(sub, 2, 2) == DOT);
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
    }

    SUBCASE("large offset x=9,y=9") {
        ImageBuffer img(17, 17, format, InitPolicy::Zero);
        setPixelBitPacked(img, 11, 11, DOT);

        ImageBuffer sub = img.subBuffer(9, 9, 5, 5);
        CHECK(getPixelBitPacked(sub, 2, 2) == DOT);
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
    }
}

TEST_CASE("ViewPort offset - Index2_LSB") {
    constexpr uint8_t DOT = 3;
    constexpr uint8_t BG = 0;
    auto format = PixelFormatIDs::Index2_LSB;

    SUBCASE("small offset x=3,y=3") {
        ImageBuffer img(11, 11, format, InitPolicy::Zero);
        setPixelBitPacked(img, 5, 5, DOT);

        ImageBuffer sub = img.subBuffer(3, 3, 5, 5);
        CHECK(getPixelBitPacked(sub, 2, 2) == DOT);
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
    }

    SUBCASE("large offset x=9,y=9") {
        ImageBuffer img(17, 17, format, InitPolicy::Zero);
        setPixelBitPacked(img, 11, 11, DOT);

        ImageBuffer sub = img.subBuffer(9, 9, 5, 5);
        CHECK(getPixelBitPacked(sub, 2, 2) == DOT);
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
    }
}

TEST_CASE("ViewPort offset - Index4_MSB") {
    constexpr uint8_t DOT = 15;  // 4ビット最大値
    constexpr uint8_t BG = 0;
    auto format = PixelFormatIDs::Index4_MSB;

    SUBCASE("small offset x=3,y=3") {
        ImageBuffer img(11, 11, format, InitPolicy::Zero);
        setPixelBitPacked(img, 5, 5, DOT);

        ImageBuffer sub = img.subBuffer(3, 3, 5, 5);
        CHECK(getPixelBitPacked(sub, 2, 2) == DOT);
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
    }

    SUBCASE("large offset x=9,y=9") {
        ImageBuffer img(17, 17, format, InitPolicy::Zero);
        setPixelBitPacked(img, 11, 11, DOT);

        ImageBuffer sub = img.subBuffer(9, 9, 5, 5);
        CHECK(getPixelBitPacked(sub, 2, 2) == DOT);
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
    }
}

TEST_CASE("ViewPort offset - Index4_LSB") {
    constexpr uint8_t DOT = 15;
    constexpr uint8_t BG = 0;
    auto format = PixelFormatIDs::Index4_LSB;

    SUBCASE("small offset x=3,y=3") {
        ImageBuffer img(11, 11, format, InitPolicy::Zero);
        setPixelBitPacked(img, 5, 5, DOT);

        ImageBuffer sub = img.subBuffer(3, 3, 5, 5);
        CHECK(getPixelBitPacked(sub, 2, 2) == DOT);
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
    }

    SUBCASE("large offset x=9,y=9") {
        ImageBuffer img(17, 17, format, InitPolicy::Zero);
        setPixelBitPacked(img, 11, 11, DOT);

        ImageBuffer sub = img.subBuffer(9, 9, 5, 5);
        CHECK(getPixelBitPacked(sub, 2, 2) == DOT);
        CHECK(getPixelBitPacked(sub, 0, 0) == BG);
    }
}

// =============================================================================
// Phase 2: byte-aligned formats
// =============================================================================

TEST_CASE("ViewPort offset - RGBA8_Straight") {
    constexpr uint32_t DOT = 0xFF00FFFF;  // Magenta
    constexpr uint32_t BG = 0x00000000;   // Transparent black
    auto format = PixelFormatIDs::RGBA8_Straight;

    SUBCASE("small offset x=3,y=3") {
        ImageBuffer img(11, 11, format, InitPolicy::Zero);
        setPixelRGBA8(img, 5, 5, DOT);

        ImageBuffer sub = img.subBuffer(3, 3, 5, 5);
        CHECK(getPixelRGBA8(sub, 2, 2) == DOT);
        CHECK(getPixelRGBA8(sub, 0, 0) == BG);
    }

    SUBCASE("large offset x=9,y=9") {
        ImageBuffer img(17, 17, format, InitPolicy::Zero);
        setPixelRGBA8(img, 11, 11, DOT);

        ImageBuffer sub = img.subBuffer(9, 9, 5, 5);
        CHECK(getPixelRGBA8(sub, 2, 2) == DOT);
        CHECK(getPixelRGBA8(sub, 0, 0) == BG);
    }
}

TEST_CASE("ViewPort offset - RGB565_LE") {
    constexpr uint16_t DOT = 0xF81F;  // Magenta
    constexpr uint16_t BG = 0x0000;   // Black
    auto format = PixelFormatIDs::RGB565_LE;

    SUBCASE("small offset x=3,y=3") {
        ImageBuffer img(11, 11, format, InitPolicy::Zero);
        setPixelRGB565(img, 5, 5, DOT);

        ImageBuffer sub = img.subBuffer(3, 3, 5, 5);
        CHECK(getPixelRGB565(sub, 2, 2) == DOT);
        CHECK(getPixelRGB565(sub, 0, 0) == BG);
    }

    SUBCASE("large offset x=9,y=9") {
        ImageBuffer img(17, 17, format, InitPolicy::Zero);
        setPixelRGB565(img, 11, 11, DOT);

        ImageBuffer sub = img.subBuffer(9, 9, 5, 5);
        CHECK(getPixelRGB565(sub, 2, 2) == DOT);
        CHECK(getPixelRGB565(sub, 0, 0) == BG);
    }
}

TEST_CASE("ViewPort offset - Index8") {
    constexpr uint8_t DOT = 42;
    constexpr uint8_t BG = 0;
    auto format = PixelFormatIDs::Index8;

    SUBCASE("small offset x=3,y=3") {
        ImageBuffer img(11, 11, format, InitPolicy::Zero);
        setPixelU8(img, 5, 5, DOT);

        ImageBuffer sub = img.subBuffer(3, 3, 5, 5);
        CHECK(getPixelU8(sub, 2, 2) == DOT);
        CHECK(getPixelU8(sub, 0, 0) == BG);
    }

    SUBCASE("large offset x=9,y=9") {
        ImageBuffer img(17, 17, format, InitPolicy::Zero);
        setPixelU8(img, 11, 11, DOT);

        ImageBuffer sub = img.subBuffer(9, 9, 5, 5);
        CHECK(getPixelU8(sub, 2, 2) == DOT);
        CHECK(getPixelU8(sub, 0, 0) == BG);
    }
}

// =============================================================================
// Phase 3: single-channel formats
// =============================================================================

TEST_CASE("ViewPort offset - Alpha8") {
    constexpr uint8_t DOT = 255;
    constexpr uint8_t BG = 0;
    auto format = PixelFormatIDs::Alpha8;

    SUBCASE("small offset x=3,y=3") {
        ImageBuffer img(11, 11, format, InitPolicy::Zero);
        setPixelU8(img, 5, 5, DOT);

        ImageBuffer sub = img.subBuffer(3, 3, 5, 5);
        CHECK(getPixelU8(sub, 2, 2) == DOT);
        CHECK(getPixelU8(sub, 0, 0) == BG);
    }

    SUBCASE("large offset x=9,y=9") {
        ImageBuffer img(17, 17, format, InitPolicy::Zero);
        setPixelU8(img, 11, 11, DOT);

        ImageBuffer sub = img.subBuffer(9, 9, 5, 5);
        CHECK(getPixelU8(sub, 2, 2) == DOT);
        CHECK(getPixelU8(sub, 0, 0) == BG);
    }
}

TEST_CASE("ViewPort offset - Grayscale8") {
    constexpr uint8_t DOT = 255;
    constexpr uint8_t BG = 0;
    auto format = PixelFormatIDs::Grayscale8;

    SUBCASE("small offset x=3,y=3") {
        ImageBuffer img(11, 11, format, InitPolicy::Zero);
        setPixelU8(img, 5, 5, DOT);

        ImageBuffer sub = img.subBuffer(3, 3, 5, 5);
        CHECK(getPixelU8(sub, 2, 2) == DOT);
        CHECK(getPixelU8(sub, 0, 0) == BG);
    }

    SUBCASE("large offset x=9,y=9") {
        ImageBuffer img(17, 17, format, InitPolicy::Zero);
        setPixelU8(img, 11, 11, DOT);

        ImageBuffer sub = img.subBuffer(9, 9, 5, 5);
        CHECK(getPixelU8(sub, 2, 2) == DOT);
        CHECK(getPixelU8(sub, 0, 0) == BG);
    }
}

// =============================================================================
// Advanced: subView chaining (累積オフセット)
// =============================================================================

TEST_CASE("ViewPort offset - subView chaining") {
    SUBCASE("Index1_MSB double subView") {
        constexpr uint8_t DOT = 1;
        ImageBuffer img(17, 17, PixelFormatIDs::Index1_MSB, InitPolicy::Zero);
        setPixelBitPacked(img, 8, 8, DOT);

        // 最初のsubView (2, 2, 13, 13) → ドットは(6, 6)
        ImageBuffer sub1 = img.subBuffer(2, 2, 13, 13);
        CHECK(getPixelBitPacked(sub1, 6, 6) == DOT);

        // 2回目のsubView (3, 3, 7, 7) → ドットは(3, 3) = 元の(2+3+3, 2+3+3) = (8,8)
        ImageBuffer sub2 = sub1.subBuffer(3, 3, 7, 7);
        CHECK(getPixelBitPacked(sub2, 3, 3) == DOT);
    }

    SUBCASE("RGBA8 triple subView") {
        constexpr uint32_t DOT = 0xFF00FFFF;
        ImageBuffer img(20, 20, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);
        setPixelRGBA8(img, 10, 10, DOT);

        ImageBuffer sub1 = img.subBuffer(2, 2, 16, 16);
        ImageBuffer sub2 = sub1.subBuffer(3, 3, 10, 10);
        ImageBuffer sub3 = sub2.subBuffer(2, 2, 6, 6);

        // (2+3+2+3, 2+3+2+3) = (10, 10) → sub3の(3, 3)
        CHECK(getPixelRGBA8(sub3, 3, 3) == DOT);
    }
}
