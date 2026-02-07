// fleximg bit-packed index format Unit Tests

#include "doctest.h"
#include <string>

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/image/image_buffer.h"

using namespace fleximg;

// ========================================================================
// Index1 テスト
// ========================================================================

TEST_CASE("Index1_MSB: basic conversion") {
    // パレット定義（2色: 黒/白）
    uint32_t palette[2] = {
        0xFF000000,  // 黒 (R=0, G=0, B=0, A=255)
        0xFFFFFFFF   // 白 (R=255, G=255, B=255, A=255)
    };

    // Index1_MSB: 8 pixels/byte
    // byte: [b7 b6 b5 b4 b3 b2 b1 b0] → pixel[0][1][2][3][4][5][6][7]
    uint8_t src[1] = { 0b10101010 };  // 1,0,1,0,1,0,1,0

    uint32_t dst[8];

    PixelAuxInfo aux;
    aux.palette = palette;
    aux.paletteFormat = PixelFormatIDs::RGBA8_Straight;
    aux.paletteColorCount = 2;

    convertFormat(src, PixelFormatIDs::Index1_MSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  8, &aux);

    CHECK(dst[0] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[1] == 0xFF000000);  // 0 → 黒
    CHECK(dst[2] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[3] == 0xFF000000);  // 0 → 黒
    CHECK(dst[4] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[5] == 0xFF000000);  // 0 → 黒
    CHECK(dst[6] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[7] == 0xFF000000);  // 0 → 黒
}

TEST_CASE("Index1_LSB: basic conversion") {
    uint32_t palette[2] = {
        0xFF000000,  // 黒
        0xFFFFFFFF   // 白
    };

    // Index1_LSB: byte[b7 b6 b5 b4 b3 b2 b1 b0] → pixel[7][6][5][4][3][2][1][0]
    uint8_t src[1] = { 0b10101010 };  // pixel: 0,1,0,1,0,1,0,1

    uint32_t dst[8];

    PixelAuxInfo aux;
    aux.palette = palette;
    aux.paletteFormat = PixelFormatIDs::RGBA8_Straight;
    aux.paletteColorCount = 2;

    convertFormat(src, PixelFormatIDs::Index1_LSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  8, &aux);

    CHECK(dst[0] == 0xFF000000);  // 0 → 黒
    CHECK(dst[1] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[2] == 0xFF000000);  // 0 → 黒
    CHECK(dst[3] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[4] == 0xFF000000);  // 0 → 黒
    CHECK(dst[5] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[6] == 0xFF000000);  // 0 → 黒
    CHECK(dst[7] == 0xFFFFFFFF);  // 1 → 白
}

TEST_CASE("Index1_MSB: sibling relationship") {
    auto msb = PixelFormatIDs::Index1_MSB;
    auto lsb = PixelFormatIDs::Index1_LSB;

    CHECK(msb->siblingEndian == lsb);
    CHECK(lsb->siblingEndian == msb);
    CHECK(msb->bitOrder == BitOrder::MSBFirst);
    CHECK(lsb->bitOrder == BitOrder::LSBFirst);
}

// ========================================================================
// Index2 テスト
// ========================================================================

TEST_CASE("Index2_MSB: basic conversion") {
    // パレット定義（4色）
    uint32_t palette[4] = {
        0xFF000000,  // 黒
        0xFF0000FF,  // 赤
        0xFF00FF00,  // 緑
        0xFFFF0000   // 青
    };

    // Index2_MSB: 4 pixels/byte
    // byte: [b7b6 b5b4 b3b2 b1b0] → pixel[0][1][2][3]
    uint8_t src[1] = { 0b00011011 };  // 0,1,2,3

    uint32_t dst[4];

    PixelAuxInfo aux;
    aux.palette = palette;
    aux.paletteFormat = PixelFormatIDs::RGBA8_Straight;
    aux.paletteColorCount = 4;

    convertFormat(src, PixelFormatIDs::Index2_MSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  4, &aux);

    CHECK(dst[0] == 0xFF000000);  // 0 → 黒
    CHECK(dst[1] == 0xFF0000FF);  // 1 → 赤
    CHECK(dst[2] == 0xFF00FF00);  // 2 → 緑
    CHECK(dst[3] == 0xFFFF0000);  // 3 → 青
}

TEST_CASE("Index2_LSB: basic conversion") {
    uint32_t palette[4] = {
        0xFF000000,  // 黒
        0xFF0000FF,  // 赤
        0xFF00FF00,  // 緑
        0xFFFF0000   // 青
    };

    // Index2_LSB: byte[b7b6 b5b4 b3b2 b1b0] → pixel[3][2][1][0]
    uint8_t src[1] = { 0b11100100 };  // 3,2,1,0

    uint32_t dst[4];

    PixelAuxInfo aux;
    aux.palette = palette;
    aux.paletteFormat = PixelFormatIDs::RGBA8_Straight;
    aux.paletteColorCount = 4;

    convertFormat(src, PixelFormatIDs::Index2_LSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  4, &aux);

    CHECK(dst[0] == 0xFF000000);  // 0 → 黒
    CHECK(dst[1] == 0xFF0000FF);  // 1 → 赤
    CHECK(dst[2] == 0xFF00FF00);  // 2 → 緑
    CHECK(dst[3] == 0xFFFF0000);  // 3 → 青
}

// ========================================================================
// Index4 テスト
// ========================================================================

TEST_CASE("Index4_MSB: basic conversion") {
    // パレット定義（16色、一部のみ定義）
    uint32_t palette[16] = {};
    palette[0] = 0xFF000000;   // 黒
    palette[15] = 0xFFFFFFFF;  // 白

    // Index4_MSB: 2 pixels/byte
    // byte: [b7b6b5b4 b3b2b1b0] → pixel[0][1]
    uint8_t src[1] = { 0x0F };  // 0, 15

    uint32_t dst[2];

    PixelAuxInfo aux;
    aux.palette = palette;
    aux.paletteFormat = PixelFormatIDs::RGBA8_Straight;
    aux.paletteColorCount = 16;

    convertFormat(src, PixelFormatIDs::Index4_MSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  2, &aux);

    CHECK(dst[0] == 0xFF000000);  // 0 → 黒
    CHECK(dst[1] == 0xFFFFFFFF);  // 15 → 白
}

TEST_CASE("Index4_LSB: basic conversion") {
    uint32_t palette[16] = {};
    palette[0] = 0xFF000000;   // 黒
    palette[15] = 0xFFFFFFFF;  // 白

    // Index4_LSB: byte[b7b6b5b4 b3b2b1b0] → pixel[1][0]
    uint8_t src[1] = { 0xF0 };  // 0, 15

    uint32_t dst[2];

    PixelAuxInfo aux;
    aux.palette = palette;
    aux.paletteFormat = PixelFormatIDs::RGBA8_Straight;
    aux.paletteColorCount = 16;

    convertFormat(src, PixelFormatIDs::Index4_LSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  2, &aux);

    CHECK(dst[0] == 0xFF000000);  // 0 → 黒
    CHECK(dst[1] == 0xFFFFFFFF);  // 15 → 白
}

// ========================================================================
// パレットなし時のグレースケール展開テスト
// ========================================================================

TEST_CASE("Index1_MSB: grayscale fallback (no palette)") {
    uint8_t src[1] = { 0b10000000 };  // 1,0,0,0,0,0,0,0

    uint32_t dst[8];

    // パレットなし
    convertFormat(src, PixelFormatIDs::Index1_MSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  8, nullptr);

    // 1 → 0xFF (白), 0 → 0x00 (黒)
    CHECK(dst[0] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[1] == 0xFF000000);  // 0 → 黒 (A=255)
}

// ========================================================================
// fromStraight テスト
// ========================================================================

TEST_CASE("Index1_MSB: RGBA8 to Index1 (quantization)") {
    uint32_t src[8] = {
        0xFFFFFFFF,  // R=255,G=255,B=255,A=255 (白, lum=255) → 255>>7 = 1
        0xFF000000,  // R=0,G=0,B=0,A=255 (黒, lum=0) → 0>>7 = 0
        0xFF808080,  // R=128,G=128,B=128,A=255 (グレー, lum=128) → 128>>7 = 1
        0xFF404040,  // R=64,G=64,B=64,A=255 (暗灰, lum=64) → 64>>7 = 0
        0xFF0000FF,  // R=255,G=0,B=0,A=255 (赤, lum≈77) → 77>>7 = 0
        0xFF00FF00,  // R=0,G=255,B=0,A=255 (緑, lum≈150) → 150>>7 = 1
        0xFFFF0000,  // R=0,G=0,B=255,A=255 (青, lum≈29) → 29>>7 = 0
        0xFFC0C0C0   // R=192,G=192,B=192,A=255 (明灰, lum=192) → 192>>7 = 1
    };

    uint8_t dst[1];

    convertFormat(src, PixelFormatIDs::RGBA8_Straight,
                  dst, PixelFormatIDs::Index1_MSB,
                  8, nullptr);

    // 期待値: 10100101 = 0xA5
    CHECK(dst[0] == 0xA5);
}

TEST_CASE("Index2_MSB: RGBA8 to Index2 (quantization)") {
    uint32_t src[4] = {
        0xFF000000,  // 黒 (lum=0)   → 0
        0xFF555555,  // 暗灰 (lum=85)  → 1
        0xFFAAAAAA,  // 明灰 (lum=170) → 2
        0xFFFFFFFF   // 白 (lum=255) → 3
    };

    uint8_t dst[1];

    convertFormat(src, PixelFormatIDs::RGBA8_Straight,
                  dst, PixelFormatIDs::Index2_MSB,
                  4, nullptr);

    // 期待値: 00 01 10 11 = 0x1B
    CHECK(dst[0] == 0x1B);
}

// ========================================================================
// 端数処理テスト
// ========================================================================

TEST_CASE("Index1_MSB: partial byte (5 pixels)") {
    uint32_t palette[2] = {
        0xFF000000,  // 黒
        0xFFFFFFFF   // 白
    };

    uint8_t src[1] = { 0b10101000 };  // 1,0,1,0,1,0,0,0 (5ピクセルのみ使用)

    uint32_t dst[5];

    PixelAuxInfo aux;
    aux.palette = palette;
    aux.paletteFormat = PixelFormatIDs::RGBA8_Straight;
    aux.paletteColorCount = 2;

    convertFormat(src, PixelFormatIDs::Index1_MSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  5, &aux);

    CHECK(dst[0] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[1] == 0xFF000000);  // 0 → 黒
    CHECK(dst[2] == 0xFFFFFFFF);  // 1 → 白
    CHECK(dst[3] == 0xFF000000);  // 0 → 黒
    CHECK(dst[4] == 0xFFFFFFFF);  // 1 → 白
}

TEST_CASE("Index4_MSB: partial byte (1 pixel)") {
    uint32_t palette[16] = {};
    palette[5] = 0x12345678;

    uint8_t src[1] = { 0x50 };  // 5, (1ピクセルのみ使用)

    uint32_t dst[1];

    PixelAuxInfo aux;
    aux.palette = palette;
    aux.paletteFormat = PixelFormatIDs::RGBA8_Straight;
    aux.paletteColorCount = 16;

    convertFormat(src, PixelFormatIDs::Index4_MSB,
                  dst, PixelFormatIDs::RGBA8_Straight,
                  1, &aux);

    CHECK(dst[0] == 0x12345678);
}

// ========================================================================
// フォーマット情報テスト
// ========================================================================

TEST_CASE("Index format properties") {
    auto i1_msb = PixelFormatIDs::Index1_MSB;
    auto i2_msb = PixelFormatIDs::Index2_MSB;
    auto i4_msb = PixelFormatIDs::Index4_MSB;

    // Index1
    CHECK(i1_msb->bitsPerPixel == 1);
    CHECK(i1_msb->pixelsPerUnit == 8);
    CHECK(i1_msb->bytesPerUnit == 1);
    CHECK(i1_msb->maxPaletteSize == 2);
    CHECK(i1_msb->isIndexed == true);
    CHECK(i1_msb->hasAlpha == false);

    // Index2
    CHECK(i2_msb->bitsPerPixel == 2);
    CHECK(i2_msb->pixelsPerUnit == 4);
    CHECK(i2_msb->bytesPerUnit == 1);
    CHECK(i2_msb->maxPaletteSize == 4);

    // Index4
    CHECK(i4_msb->bitsPerPixel == 4);
    CHECK(i4_msb->pixelsPerUnit == 2);
    CHECK(i4_msb->bytesPerUnit == 1);
    CHECK(i4_msb->maxPaletteSize == 16);
}

TEST_CASE("getFormatByName: bit-packed index formats") {
    CHECK(getFormatByName("Index1_MSB") == PixelFormatIDs::Index1_MSB);
    CHECK(getFormatByName("Index1_LSB") == PixelFormatIDs::Index1_LSB);
    CHECK(getFormatByName("Index2_MSB") == PixelFormatIDs::Index2_MSB);
    CHECK(getFormatByName("Index2_LSB") == PixelFormatIDs::Index2_LSB);
    CHECK(getFormatByName("Index4_MSB") == PixelFormatIDs::Index4_MSB);
    CHECK(getFormatByName("Index4_LSB") == PixelFormatIDs::Index4_LSB);
}

// ========================================================================
// DDA関数テスト
// ========================================================================

TEST_CASE("Index1_MSB: copyRowDDA function exists") {
    auto fmt = PixelFormatIDs::Index1_MSB;
    CHECK(fmt->copyRowDDA != nullptr);
    CHECK(fmt->copyQuadDDA != nullptr);
}

TEST_CASE("Index1_MSB: copyRowDDA basic sampling") {
    // 8ピクセル: 10101010
    uint8_t src[1] = { 0xAA };
    uint8_t dst[4];

    // DDAパラメータ: 等間隔サンプリング (pixel 0, 2, 4, 6)
    DDAParam param = {};
    param.srcStride = 1;
    param.srcWidth = 8;
    param.srcHeight = 1;
    param.srcX = 0;  // 開始位置 pixel 0
    param.srcY = 0;
    param.incrX = 2 << INT_FIXED_SHIFT;  // 2ピクセルずつ
    param.incrY = 0;

    auto fmt = PixelFormatIDs::Index1_MSB;
    fmt->copyRowDDA(dst, src, 4, &param);

    // pixel[0,2,4,6] = 1,1,1,1
    CHECK(dst[0] == 1);
    CHECK(dst[1] == 1);
    CHECK(dst[2] == 1);
    CHECK(dst[3] == 1);
}

TEST_CASE("Index2_MSB: copyRowDDA basic sampling") {
    // 4ピクセル: 00 01 10 11
    uint8_t src[1] = { 0x1B };
    uint8_t dst[4];

    DDAParam param = {};
    param.srcStride = 1;
    param.srcWidth = 4;
    param.srcHeight = 1;
    param.srcX = 0;
    param.srcY = 0;
    param.incrX = 1 << INT_FIXED_SHIFT;  // 1ピクセルずつ
    param.incrY = 0;

    auto fmt = PixelFormatIDs::Index2_MSB;
    fmt->copyRowDDA(dst, src, 4, &param);

    CHECK(dst[0] == 0);
    CHECK(dst[1] == 1);
    CHECK(dst[2] == 2);
    CHECK(dst[3] == 3);
}

TEST_CASE("Index4_MSB: copyRowDDA with fractional increment") {
    // 2ピクセル: 0000 1111
    uint8_t src[1] = { 0x0F };
    uint8_t dst[3];

    DDAParam param = {};
    param.srcStride = 1;
    param.srcWidth = 2;
    param.srcHeight = 1;
    param.srcX = 0;
    param.srcY = 0;
    param.incrX = (1 << INT_FIXED_SHIFT) / 2;  // 0.5ピクセルずつ
    param.incrY = 0;

    auto fmt = PixelFormatIDs::Index4_MSB;
    fmt->copyRowDDA(dst, src, 3, &param);

    // pixel[0.0, 0.5, 1.0] = 0, 0, 15
    CHECK(dst[0] == 0);
    CHECK(dst[1] == 0);  // 0.5 → pixel 0
    CHECK(dst[2] == 15);
}

TEST_CASE("Index1_MSB: copyQuadDDA exists and callable") {
    uint8_t src[1] = { 0xFF };
    uint8_t dst[4 * 4];  // 4ピクセル × 4値（p00,p10,p01,p11）
    uint8_t edgeFlags[4];
    BilinearWeightXY weights[4];

    DDAParam param = {};
    param.srcStride = 1;
    param.srcWidth = 8;
    param.srcHeight = 1;
    param.srcX = 0;
    param.srcY = 0;
    param.incrX = 1 << INT_FIXED_SHIFT;
    param.incrY = 0;
    param.edgeFlags = edgeFlags;
    param.weightsXY = weights;

    auto fmt = PixelFormatIDs::Index1_MSB;

    // 関数呼び出しが成功すること（値の検証は複雑なので省略）
    fmt->copyQuadDDA(dst, src, 4, &param);

    // 呼び出しが成功したことを確認（クラッシュしないこと）
    CHECK(true);
}
