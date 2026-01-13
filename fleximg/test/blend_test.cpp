// fleximg blend operations Unit Tests
// ブレンド操作のテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/image/image_buffer.h"
#include "fleximg/operations/blend.h"

using namespace fleximg;

// =============================================================================
// Helper Functions
// =============================================================================

// RGBA8ピクセルを設定
static void setPixelRGBA8(ImageBuffer& buf, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t* p = static_cast<uint8_t*>(buf.pixelAt(x, y));
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

// RGBA8ピクセルを取得
static void getPixelRGBA8(const ImageBuffer& buf, int x, int y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    const uint8_t* p = static_cast<const uint8_t*>(buf.pixelAt(x, y));
    r = p[0]; g = p[1]; b = p[2]; a = p[3];
}

// RGBA16ピクセルを設定
static void setPixelRGBA16(ImageBuffer& buf, int x, int y, uint16_t r, uint16_t g, uint16_t b, uint16_t a) {
    uint16_t* p = static_cast<uint16_t*>(buf.pixelAt(x, y));
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

// RGBA16ピクセルを取得
static void getPixelRGBA16(const ImageBuffer& buf, int x, int y, uint16_t& r, uint16_t& g, uint16_t& b, uint16_t& a) {
    const uint16_t* p = static_cast<const uint16_t*>(buf.pixelAt(x, y));
    r = p[0]; g = p[1]; b = p[2]; a = p[3];
}

// =============================================================================
// blend::first Tests
// =============================================================================

TEST_CASE("blend::first basic copy") {
    // 同一フォーマット間のコピー
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dst(4, 4, PixelFormatIDs::RGBA8_Straight);

    // srcに赤ピクセルを設定
    setPixelRGBA8(src, 1, 1, 255, 0, 0, 255);

    // 基準点を中央に設定
    int_fixed8 srcOrigin = to_fixed8(2);
    int_fixed8 dstOrigin = to_fixed8(2);

    blend::first(dst.viewRef(), dstOrigin, dstOrigin,
                 src.view(), srcOrigin, srcOrigin);

    // コピーされていることを確認
    uint8_t r, g, b, a;
    getPixelRGBA8(dst, 1, 1, r, g, b, a);
    CHECK(r == 255);
    CHECK(g == 0);
    CHECK(b == 0);
    CHECK(a == 255);
}

TEST_CASE("blend::first with offset") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dst(8, 8, PixelFormatIDs::RGBA8_Straight);

    // srcの(0,0)に赤ピクセル
    setPixelRGBA8(src, 0, 0, 255, 0, 0, 255);

    // srcの基準点(0,0)をdstの基準点(4,4)に合わせる
    blend::first(dst.viewRef(), to_fixed8(4), to_fixed8(4),
                 src.view(), to_fixed8(0), to_fixed8(0));

    // dstの(4,4)に赤ピクセルがあるはず
    uint8_t r, g, b, a;
    getPixelRGBA8(dst, 4, 4, r, g, b, a);
    CHECK(r == 255);
    CHECK(g == 0);
    CHECK(b == 0);
    CHECK(a == 255);

    // dstの(0,0)は変更されない（ゼロのまま）
    getPixelRGBA8(dst, 0, 0, r, g, b, a);
    CHECK(r == 0);
    CHECK(a == 0);
}

TEST_CASE("blend::first format conversion RGBA8 to RGBA16") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dst(4, 4, PixelFormatIDs::RGBA16_Premultiplied);

    // srcに不透明赤ピクセル
    setPixelRGBA8(src, 1, 1, 255, 0, 0, 255);

    blend::first(dst.viewRef(), to_fixed8(2), to_fixed8(2),
                 src.view(), to_fixed8(2), to_fixed8(2));

    // RGBA16形式で確認
    uint16_t r, g, b, a;
    getPixelRGBA16(dst, 1, 1, r, g, b, a);

    // 不透明（alpha >= ALPHA_OPAQUE_MIN）であること
    CHECK(a >= PixelFormatIDs::RGBA16Premul::ALPHA_OPAQUE_MIN);
    // 赤成分が支配的であること
    CHECK(r > 0);
    CHECK(g == 0);
    CHECK(b == 0);
}

TEST_CASE("blend::first clipping") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dst(4, 4, PixelFormatIDs::RGBA8_Straight);

    // 全ピクセルを赤に
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            setPixelRGBA8(src, x, y, 255, 0, 0, 255);
        }
    }

    // srcを右下にオフセット（一部のみ見える）
    blend::first(dst.viewRef(), to_fixed8(0), to_fixed8(0),
                 src.view(), to_fixed8(2), to_fixed8(2));

    // dstの(0,0)-(1,1)にsrcの(2,2)-(3,3)がコピーされる
    uint8_t r, g, b, a;
    getPixelRGBA8(dst, 0, 0, r, g, b, a);
    CHECK(r == 255);
    CHECK(a == 255);

    getPixelRGBA8(dst, 1, 1, r, g, b, a);
    CHECK(r == 255);
    CHECK(a == 255);

    // (2,2)以降はコピーされない（ゼロのまま）
    getPixelRGBA8(dst, 2, 2, r, g, b, a);
    CHECK(r == 0);
    CHECK(a == 0);
}

// =============================================================================
// blend::onto Tests
// =============================================================================

TEST_CASE("blend::onto opaque over transparent") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA16_Premultiplied);
    ImageBuffer dst(4, 4, PixelFormatIDs::RGBA16_Premultiplied);

    // srcに不透明赤ピクセル（プリマルチプライド）
    uint16_t opaqueAlpha = 65535;
    setPixelRGBA16(src, 1, 1, 65535, 0, 0, opaqueAlpha);

    blend::onto(dst.viewRef(), to_fixed8(2), to_fixed8(2),
                src.view(), to_fixed8(2), to_fixed8(2));

    uint16_t r, g, b, a;
    getPixelRGBA16(dst, 1, 1, r, g, b, a);

    // 不透明なsrcがdstを完全に上書き
    CHECK(r == 65535);
    CHECK(g == 0);
    CHECK(b == 0);
    CHECK(a == 65535);
}

TEST_CASE("blend::onto transparent src skipped") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA16_Premultiplied);
    ImageBuffer dst(4, 4, PixelFormatIDs::RGBA16_Premultiplied);

    // dstに緑ピクセル
    setPixelRGBA16(dst, 1, 1, 0, 65535, 0, 65535);

    // srcは透明（デフォルト）

    blend::onto(dst.viewRef(), to_fixed8(2), to_fixed8(2),
                src.view(), to_fixed8(2), to_fixed8(2));

    uint16_t r, g, b, a;
    getPixelRGBA16(dst, 1, 1, r, g, b, a);

    // 透明なsrcはスキップされ、dstは変更されない
    CHECK(r == 0);
    CHECK(g == 65535);
    CHECK(b == 0);
    CHECK(a == 65535);
}

TEST_CASE("blend::onto semi-transparent blending") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA16_Premultiplied);
    ImageBuffer dst(4, 4, PixelFormatIDs::RGBA16_Premultiplied);

    // dstに不透明緑
    setPixelRGBA16(dst, 1, 1, 0, 65535, 0, 65535);

    // srcに半透明赤（alpha=32768, 約50%）
    // プリマルチプライド: R = 65535 * 0.5 = 32768
    uint16_t halfAlpha = 32768;
    setPixelRGBA16(src, 1, 1, halfAlpha, 0, 0, halfAlpha);

    blend::onto(dst.viewRef(), to_fixed8(2), to_fixed8(2),
                src.view(), to_fixed8(2), to_fixed8(2));

    uint16_t r, g, b, a;
    getPixelRGBA16(dst, 1, 1, r, g, b, a);

    // ブレンド結果: 赤と緑が混合
    CHECK(r > 0);
    CHECK(g > 0);
    CHECK(a > halfAlpha);  // アルファは合成で増加
}

TEST_CASE("blend::onto RGBA8 to RGBA16 conversion") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dst(4, 4, PixelFormatIDs::RGBA16_Premultiplied);

    // dstに不透明緑
    setPixelRGBA16(dst, 1, 1, 0, 65535, 0, 65535);

    // srcに不透明赤（RGBA8 Straight）
    setPixelRGBA8(src, 1, 1, 255, 0, 0, 255);

    blend::onto(dst.viewRef(), to_fixed8(2), to_fixed8(2),
                src.view(), to_fixed8(2), to_fixed8(2));

    uint16_t r, g, b, a;
    getPixelRGBA16(dst, 1, 1, r, g, b, a);

    // 不透明な赤がdstを上書き
    CHECK(r > 60000);  // 高い赤成分
    CHECK(g < 5000);   // 緑はほぼなし
    CHECK(a >= PixelFormatIDs::RGBA16Premul::ALPHA_OPAQUE_MIN);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("blend::first with invalid viewports") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
    ViewPort invalidDst;  // invalid

    // should not crash
    blend::first(invalidDst, to_fixed8(0), to_fixed8(0),
                 src.view(), to_fixed8(0), to_fixed8(0));
    CHECK(true);  // reached here without crash
}

TEST_CASE("blend::onto with invalid viewports") {
    ImageBuffer dst(4, 4, PixelFormatIDs::RGBA16_Premultiplied);
    ViewPort invalidSrc;  // invalid

    // should not crash
    blend::onto(dst.viewRef(), to_fixed8(0), to_fixed8(0),
                invalidSrc, to_fixed8(0), to_fixed8(0));
    CHECK(true);  // reached here without crash
}

TEST_CASE("blend with completely out of bounds") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dst(4, 4, PixelFormatIDs::RGBA8_Straight);

    setPixelRGBA8(src, 0, 0, 255, 0, 0, 255);

    // srcを完全にdstの外に配置
    blend::first(dst.viewRef(), to_fixed8(0), to_fixed8(0),
                 src.view(), to_fixed8(100), to_fixed8(100));

    // dstは変更されない
    uint8_t r, g, b, a;
    getPixelRGBA8(dst, 0, 0, r, g, b, a);
    CHECK(r == 0);
    CHECK(a == 0);
}
