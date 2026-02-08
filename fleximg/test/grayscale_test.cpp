// fleximg Grayscale format Unit Tests

#include "doctest.h"
#include <string>

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/image/image_buffer.h"

using namespace fleximg;

// ========================================================================
// Grayscale1 テスト
// ========================================================================

TEST_CASE("Grayscale1_MSB: basic toStraight conversion") {
    // 1bit: 0→黒(0), 1→白(255)
    uint8_t src[1] = { 0b10101010 };  // 1,0,1,0,1,0,1,0

    uint32_t dst[8];

    // パレットなし（Grayscaleとして変換）
    convertFormat(src, PixelFormatIDs::Grayscale1_MSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  8, nullptr);

    // 1 → 255(白), 0 → 0(黒)
    CHECK(dst[0] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[1] == 0xFF000000);  // 0 → 黒 (A=255)
    CHECK(dst[2] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[3] == 0xFF000000);  // 0 → 黒
    CHECK(dst[4] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[5] == 0xFF000000);  // 0 → 黒
    CHECK(dst[6] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[7] == 0xFF000000);  // 0 → 黒
}

TEST_CASE("Grayscale1_LSB: basic toStraight conversion") {
    uint8_t src[1] = { 0b10101010 };  // LSB: pixel 0,1,0,1,0,1,0,1

    uint32_t dst[8];

    convertFormat(src, PixelFormatIDs::Grayscale1_LSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  8, nullptr);

    CHECK(dst[0] == 0xFF000000);  // 0 → 黒
    CHECK(dst[1] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[2] == 0xFF000000);  // 0 → 黒
    CHECK(dst[3] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[4] == 0xFF000000);  // 0 → 黒
    CHECK(dst[5] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[6] == 0xFF000000);  // 0 → 黒
    CHECK(dst[7] == 0xFFFFFFFF);  // 1 → 白
}

TEST_CASE("Grayscale1_MSB: sibling relationship") {
    auto msb = PixelFormatIDs::Grayscale1_MSB;
    auto lsb = PixelFormatIDs::Grayscale1_LSB;

    CHECK(msb->siblingEndian == lsb);
    CHECK(lsb->siblingEndian == msb);
    CHECK(msb->bitOrder == BitOrder::MSBFirst);
    CHECK(lsb->bitOrder == BitOrder::LSBFirst);
}

// ========================================================================
// Grayscale2 テスト
// ========================================================================

TEST_CASE("Grayscale2_MSB: basic toStraight conversion") {
    // 2bit: 0→0, 1→85, 2→170, 3→255
    // byte: [b7b6 b5b4 b3b2 b1b0] → pixel[0][1][2][3]
    uint8_t src[1] = { 0b00011011 };  // 0,1,2,3

    uint32_t dst[4];

    convertFormat(src, PixelFormatIDs::Grayscale2_MSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  4, nullptr);

    // 各値のRGB: 0*85=0, 1*85=85, 2*85=170, 3*85=255
    uint8_t* p0 = reinterpret_cast<uint8_t*>(&dst[0]);
    CHECK(p0[0] == 0);    // R
    CHECK(p0[1] == 0);    // G
    CHECK(p0[2] == 0);    // B
    CHECK(p0[3] == 255);  // A

    uint8_t* p1 = reinterpret_cast<uint8_t*>(&dst[1]);
    CHECK(p1[0] == 85);
    CHECK(p1[1] == 85);
    CHECK(p1[2] == 85);
    CHECK(p1[3] == 255);

    uint8_t* p2 = reinterpret_cast<uint8_t*>(&dst[2]);
    CHECK(p2[0] == 170);
    CHECK(p2[1] == 170);
    CHECK(p2[2] == 170);
    CHECK(p2[3] == 255);

    uint8_t* p3 = reinterpret_cast<uint8_t*>(&dst[3]);
    CHECK(p3[0] == 255);
    CHECK(p3[1] == 255);
    CHECK(p3[2] == 255);
    CHECK(p3[3] == 255);
}

TEST_CASE("Grayscale2_LSB: basic toStraight conversion") {
    // LSB: byte[b7b6 b5b4 b3b2 b1b0] → pixel[3][2][1][0]
    uint8_t src[1] = { 0b11100100 };  // pixel: 0,1,2,3

    uint32_t dst[4];

    convertFormat(src, PixelFormatIDs::Grayscale2_LSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  4, nullptr);

    uint8_t* p0 = reinterpret_cast<uint8_t*>(&dst[0]);
    CHECK(p0[0] == 0);    // pixel 0 → 0

    uint8_t* p1 = reinterpret_cast<uint8_t*>(&dst[1]);
    CHECK(p1[0] == 85);   // pixel 1 → 85

    uint8_t* p2 = reinterpret_cast<uint8_t*>(&dst[2]);
    CHECK(p2[0] == 170);  // pixel 2 → 170

    uint8_t* p3 = reinterpret_cast<uint8_t*>(&dst[3]);
    CHECK(p3[0] == 255);  // pixel 3 → 255
}

TEST_CASE("Grayscale2_MSB: sibling relationship") {
    auto msb = PixelFormatIDs::Grayscale2_MSB;
    auto lsb = PixelFormatIDs::Grayscale2_LSB;

    CHECK(msb->siblingEndian == lsb);
    CHECK(lsb->siblingEndian == msb);
}

// ========================================================================
// Grayscale4 テスト
// ========================================================================

TEST_CASE("Grayscale4_MSB: basic toStraight conversion") {
    // 4bit: 0→0, 15→255
    // byte: [b7b6b5b4 b3b2b1b0] → pixel[0][1]
    uint8_t src[1] = { 0x0F };  // 0, 15

    uint32_t dst[2];

    convertFormat(src, PixelFormatIDs::Grayscale4_MSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  2, nullptr);

    uint8_t* p0 = reinterpret_cast<uint8_t*>(&dst[0]);
    CHECK(p0[0] == 0);    // 0 → 0
    CHECK(p0[3] == 255);  // A=255

    uint8_t* p1 = reinterpret_cast<uint8_t*>(&dst[1]);
    CHECK(p1[0] == 255);  // 15 → 255
    CHECK(p1[3] == 255);
}

TEST_CASE("Grayscale4_LSB: basic toStraight conversion") {
    // LSB: byte[b7b6b5b4 b3b2b1b0] → pixel[1][0]
    uint8_t src[1] = { 0xF0 };  // pixel 0=0, pixel 1=15

    uint32_t dst[2];

    convertFormat(src, PixelFormatIDs::Grayscale4_LSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  2, nullptr);

    uint8_t* p0 = reinterpret_cast<uint8_t*>(&dst[0]);
    CHECK(p0[0] == 0);    // 0 → 0

    uint8_t* p1 = reinterpret_cast<uint8_t*>(&dst[1]);
    CHECK(p1[0] == 255);  // 15 → 255
}

TEST_CASE("Grayscale4_MSB: sibling relationship") {
    auto msb = PixelFormatIDs::Grayscale4_MSB;
    auto lsb = PixelFormatIDs::Grayscale4_LSB;

    CHECK(msb->siblingEndian == lsb);
    CHECK(lsb->siblingEndian == msb);
}

// ========================================================================
// fromStraight テスト
// ========================================================================

TEST_CASE("Grayscale1_MSB: RGBA8 to Grayscale1 (quantization)") {
    uint32_t src[8] = {
        0xFFFFFFFF,  // 白 (lum=255) → 255>>7 = 1
        0xFF000000,  // 黒 (lum=0)   → 0>>7 = 0
        0xFF808080,  // グレー (lum=128) → 128>>7 = 1
        0xFF404040,  // 暗灰 (lum=64) → 64>>7 = 0
        0xFF0000FF,  // 赤 (lum≈77) → 77>>7 = 0
        0xFF00FF00,  // 緑 (lum≈150) → 150>>7 = 1
        0xFFFF0000,  // 青 (lum≈29) → 29>>7 = 0
        0xFFC0C0C0   // 明灰 (lum=192) → 192>>7 = 1
    };

    uint8_t dst[1];

    convertFormat(src, PixelFormatIDs::RGBA8_Straight,
                  dst, PixelFormatIDs::Grayscale1_MSB,
                  8, nullptr);

    // 期待値: 10100101 = 0xA5
    CHECK(dst[0] == 0xA5);
}

TEST_CASE("Grayscale2_MSB: RGBA8 to Grayscale2 (quantization)") {
    uint32_t src[4] = {
        0xFF000000,  // 黒 (lum=0)   → 0
        0xFF555555,  // 暗灰 (lum=85)  → 1
        0xFFAAAAAA,  // 明灰 (lum=170) → 2
        0xFFFFFFFF   // 白 (lum=255) → 3
    };

    uint8_t dst[1];

    convertFormat(src, PixelFormatIDs::RGBA8_Straight,
                  dst, PixelFormatIDs::Grayscale2_MSB,
                  4, nullptr);

    // 期待値: 00 01 10 11 = 0x1B
    CHECK(dst[0] == 0x1B);
}

TEST_CASE("Grayscale4_MSB: RGBA8 to Grayscale4 (quantization)") {
    uint32_t src[2] = {
        0xFF000000,  // 黒 (lum=0)   → 0
        0xFFFFFFFF   // 白 (lum=255) → 15
    };

    uint8_t dst[1];

    convertFormat(src, PixelFormatIDs::RGBA8_Straight,
                  dst, PixelFormatIDs::Grayscale4_MSB,
                  2, nullptr);

    // 期待値: 0000 1111 = 0x0F
    CHECK(dst[0] == 0x0F);
}

// ========================================================================
// Descriptor プロパティテスト
// ========================================================================

TEST_CASE("Grayscale format properties") {
    auto g1_msb = PixelFormatIDs::Grayscale1_MSB;
    auto g2_msb = PixelFormatIDs::Grayscale2_MSB;
    auto g4_msb = PixelFormatIDs::Grayscale4_MSB;

    // Grayscale1
    CHECK(g1_msb->bitsPerPixel == 1);
    CHECK(g1_msb->pixelsPerUnit == 8);
    CHECK(g1_msb->bytesPerUnit == 1);
    CHECK(g1_msb->maxPaletteSize == 0);
    CHECK(g1_msb->isIndexed == false);
    CHECK(g1_msb->hasAlpha == false);
    CHECK(g1_msb->expandIndex == nullptr);

    // Grayscale2
    CHECK(g2_msb->bitsPerPixel == 2);
    CHECK(g2_msb->pixelsPerUnit == 4);
    CHECK(g2_msb->bytesPerUnit == 1);
    CHECK(g2_msb->maxPaletteSize == 0);
    CHECK(g2_msb->isIndexed == false);

    // Grayscale4
    CHECK(g4_msb->bitsPerPixel == 4);
    CHECK(g4_msb->pixelsPerUnit == 2);
    CHECK(g4_msb->bytesPerUnit == 1);
    CHECK(g4_msb->maxPaletteSize == 0);
    CHECK(g4_msb->isIndexed == false);
}

// ========================================================================
// 端数処理テスト
// ========================================================================

TEST_CASE("Grayscale1_MSB: partial byte (5 pixels)") {
    uint8_t src[1] = { 0b10101000 };  // 1,0,1,0,1,0,0,0 (5ピクセルのみ使用)

    uint32_t dst[5];

    convertFormat(src, PixelFormatIDs::Grayscale1_MSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  5, nullptr);

    CHECK(dst[0] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[1] == 0xFF000000);  // 0 → 黒
    CHECK(dst[2] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[3] == 0xFF000000);  // 0 → 黒
    CHECK(dst[4] == 0xFFFFFFFF);  // 1 → 白
}

TEST_CASE("Grayscale4_MSB: partial byte (1 pixel)") {
    uint8_t src[1] = { 0x80 };  // pixel 0 = 8 (upper nibble), pixel 1 = 0

    uint32_t dst[1];

    convertFormat(src, PixelFormatIDs::Grayscale4_MSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  1, nullptr);

    // 8 * 17 = 136
    uint8_t* p0 = reinterpret_cast<uint8_t*>(&dst[0]);
    CHECK(p0[0] == 136);
    CHECK(p0[1] == 136);
    CHECK(p0[2] == 136);
    CHECK(p0[3] == 255);
}

// ========================================================================
// getFormatByName テスト
// ========================================================================

TEST_CASE("getFormatByName: grayscale formats") {
    CHECK(getFormatByName("Grayscale8") == PixelFormatIDs::Grayscale8);
    CHECK(getFormatByName("Grayscale1_MSB") == PixelFormatIDs::Grayscale1_MSB);
    CHECK(getFormatByName("Grayscale1_LSB") == PixelFormatIDs::Grayscale1_LSB);
    CHECK(getFormatByName("Grayscale2_MSB") == PixelFormatIDs::Grayscale2_MSB);
    CHECK(getFormatByName("Grayscale2_LSB") == PixelFormatIDs::Grayscale2_LSB);
    CHECK(getFormatByName("Grayscale4_MSB") == PixelFormatIDs::Grayscale4_MSB);
    CHECK(getFormatByName("Grayscale4_LSB") == PixelFormatIDs::Grayscale4_LSB);
}

// ========================================================================
// DDA関数テスト
// ========================================================================

TEST_CASE("Grayscale1_MSB: copyRowDDA function exists") {
    auto fmt = PixelFormatIDs::Grayscale1_MSB;
    CHECK(fmt->copyRowDDA != nullptr);
    CHECK(fmt->copyQuadDDA != nullptr);
}

TEST_CASE("Grayscale1_MSB: copyRowDDA basic sampling") {
    // 8ピクセル: 10101010
    uint8_t src[1] = { 0xAA };
    uint8_t dst[4];

    // DDAパラメータ: 等間隔サンプリング (pixel 0, 2, 4, 6)
    DDAParam param = {};
    param.srcStride = 1;
    param.srcWidth = 8;
    param.srcHeight = 1;
    param.srcX = 0;
    param.srcY = 0;
    param.incrX = 2 << INT_FIXED_SHIFT;
    param.incrY = 0;

    auto fmt = PixelFormatIDs::Grayscale1_MSB;
    fmt->copyRowDDA(dst, src, 4, &param);

    // pixel[0,2,4,6] = 1,1,1,1
    CHECK(dst[0] == 1);
    CHECK(dst[1] == 1);
    CHECK(dst[2] == 1);
    CHECK(dst[3] == 1);
}

// ========================================================================
// Grayscale8 テスト（既存フォーマットの確認）
// ========================================================================

TEST_CASE("Grayscale8: roundtrip conversion") {
    // Grayscale8 → RGBA8 → Grayscale8 のラウンドトリップ
    uint8_t src[4] = { 0, 85, 170, 255 };
    uint32_t rgba[4];
    uint8_t dst[4];

    convertFormat(src, PixelFormatIDs::Grayscale8,
                  rgba, PixelFormatIDs::RGBA8_Straight,
                  4, nullptr);

    convertFormat(rgba, PixelFormatIDs::RGBA8_Straight,
                  dst, PixelFormatIDs::Grayscale8,
                  4, nullptr);

    CHECK(dst[0] == 0);
    CHECK(dst[1] == 85);
    CHECK(dst[2] == 170);
    CHECK(dst[3] == 255);
}
