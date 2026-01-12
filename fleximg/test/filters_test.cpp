// fleximg filters operations Unit Tests
// フィルタ操作のテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/image_buffer.h"
#include "fleximg/operations/filters.h"

using namespace fleximg;

// =============================================================================
// Helper Functions
// =============================================================================

static void setPixelRGBA8(ImageBuffer& buf, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t* p = static_cast<uint8_t*>(buf.pixelAt(x, y));
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

static void getPixelRGBA8(const ImageBuffer& buf, int x, int y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    const uint8_t* p = static_cast<const uint8_t*>(buf.pixelAt(x, y));
    r = p[0]; g = p[1]; b = p[2]; a = p[3];
}

// 全ピクセルを同じ色で埋める
static void fillBuffer(ImageBuffer& buf, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (int y = 0; y < buf.height(); ++y) {
        for (int x = 0; x < buf.width(); ++x) {
            setPixelRGBA8(buf, x, y, r, g, b, a);
        }
    }
}

// =============================================================================
// brightness Tests
// =============================================================================

TEST_CASE("filters::brightness") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dst(4, 4, PixelFormatIDs::RGBA8_Straight);

    SUBCASE("positive brightness") {
        fillBuffer(src, 100, 100, 100, 255);

        filters::brightness(dst.viewRef(), src.view(), 0.2f);  // +51

        uint8_t r, g, b, a;
        getPixelRGBA8(dst, 0, 0, r, g, b, a);
        CHECK(r == 151);  // 100 + 51
        CHECK(g == 151);
        CHECK(b == 151);
        CHECK(a == 255);  // alpha unchanged
    }

    SUBCASE("negative brightness") {
        fillBuffer(src, 100, 100, 100, 255);

        filters::brightness(dst.viewRef(), src.view(), -0.2f);  // -51

        uint8_t r, g, b, a;
        getPixelRGBA8(dst, 0, 0, r, g, b, a);
        CHECK(r == 49);  // 100 - 51
        CHECK(g == 49);
        CHECK(b == 49);
        CHECK(a == 255);
    }

    SUBCASE("brightness clamps to 0") {
        fillBuffer(src, 50, 50, 50, 255);

        filters::brightness(dst.viewRef(), src.view(), -0.5f);  // -127

        uint8_t r, g, b, a;
        getPixelRGBA8(dst, 0, 0, r, g, b, a);
        CHECK(r == 0);  // clamped to 0
        CHECK(g == 0);
        CHECK(b == 0);
    }

    SUBCASE("brightness clamps to 255") {
        fillBuffer(src, 200, 200, 200, 255);

        filters::brightness(dst.viewRef(), src.view(), 0.5f);  // +127

        uint8_t r, g, b, a;
        getPixelRGBA8(dst, 0, 0, r, g, b, a);
        CHECK(r == 255);  // clamped to 255
        CHECK(g == 255);
        CHECK(b == 255);
    }

    SUBCASE("zero brightness is passthrough") {
        fillBuffer(src, 123, 45, 67, 200);

        filters::brightness(dst.viewRef(), src.view(), 0.0f);

        uint8_t r, g, b, a;
        getPixelRGBA8(dst, 0, 0, r, g, b, a);
        CHECK(r == 123);
        CHECK(g == 45);
        CHECK(b == 67);
        CHECK(a == 200);
    }
}

// =============================================================================
// grayscale Tests
// =============================================================================

TEST_CASE("filters::grayscale") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dst(4, 4, PixelFormatIDs::RGBA8_Straight);

    SUBCASE("pure red to grayscale") {
        fillBuffer(src, 255, 0, 0, 255);

        filters::grayscale(dst.viewRef(), src.view());

        uint8_t r, g, b, a;
        getPixelRGBA8(dst, 0, 0, r, g, b, a);
        // 平均: (255 + 0 + 0) / 3 = 85
        CHECK(r == 85);
        CHECK(g == 85);
        CHECK(b == 85);
        CHECK(a == 255);  // alpha unchanged
    }

    SUBCASE("white stays white") {
        fillBuffer(src, 255, 255, 255, 255);

        filters::grayscale(dst.viewRef(), src.view());

        uint8_t r, g, b, a;
        getPixelRGBA8(dst, 0, 0, r, g, b, a);
        CHECK(r == 255);
        CHECK(g == 255);
        CHECK(b == 255);
    }

    SUBCASE("black stays black") {
        fillBuffer(src, 0, 0, 0, 255);

        filters::grayscale(dst.viewRef(), src.view());

        uint8_t r, g, b, a;
        getPixelRGBA8(dst, 0, 0, r, g, b, a);
        CHECK(r == 0);
        CHECK(g == 0);
        CHECK(b == 0);
    }

    SUBCASE("mixed color to grayscale") {
        fillBuffer(src, 100, 150, 200, 128);

        filters::grayscale(dst.viewRef(), src.view());

        uint8_t r, g, b, a;
        getPixelRGBA8(dst, 0, 0, r, g, b, a);
        // 平均: (100 + 150 + 200) / 3 = 150
        CHECK(r == 150);
        CHECK(g == 150);
        CHECK(b == 150);
        CHECK(a == 128);  // alpha unchanged
    }
}

// =============================================================================
// alpha Tests
// =============================================================================

TEST_CASE("filters::alpha") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dst(4, 4, PixelFormatIDs::RGBA8_Straight);

    SUBCASE("scale 0.5 halves alpha") {
        fillBuffer(src, 100, 100, 100, 200);

        filters::alpha(dst.viewRef(), src.view(), 0.5f);

        uint8_t r, g, b, a;
        getPixelRGBA8(dst, 0, 0, r, g, b, a);
        CHECK(r == 100);  // RGB unchanged
        CHECK(g == 100);
        CHECK(b == 100);
        CHECK(a == 100);  // 200 * 0.5 = 100
    }

    SUBCASE("scale 0 makes transparent") {
        fillBuffer(src, 100, 100, 100, 255);

        filters::alpha(dst.viewRef(), src.view(), 0.0f);

        uint8_t r, g, b, a;
        getPixelRGBA8(dst, 0, 0, r, g, b, a);
        CHECK(a == 0);
    }

    SUBCASE("scale 1 is passthrough") {
        fillBuffer(src, 100, 100, 100, 200);

        filters::alpha(dst.viewRef(), src.view(), 1.0f);

        uint8_t r, g, b, a;
        getPixelRGBA8(dst, 0, 0, r, g, b, a);
        CHECK(a == 200);
    }

    SUBCASE("scale overflow wraps (no clamping)") {
        fillBuffer(src, 100, 100, 100, 200);

        filters::alpha(dst.viewRef(), src.view(), 2.0f);

        uint8_t r, g, b, a;
        getPixelRGBA8(dst, 0, 0, r, g, b, a);
        // 注: 現在の実装はクランプせず、オーバーフロー時にトランケートする
        // (200 * 512) >> 8 = 400, uint8_t(400) = 144
        CHECK(a == 144);
    }
}

// =============================================================================
// boxBlur Tests
// =============================================================================

TEST_CASE("filters::boxBlur") {
    SUBCASE("uniform image stays uniform") {
        ImageBuffer src(8, 8, PixelFormatIDs::RGBA8_Straight);
        ImageBuffer dst(8, 8, PixelFormatIDs::RGBA8_Straight);

        fillBuffer(src, 100, 100, 100, 255);

        filters::boxBlur(dst.viewRef(), src.view(), 2);

        uint8_t r, g, b, a;
        getPixelRGBA8(dst, 4, 4, r, g, b, a);
        // 均一な画像はブラー後も均一
        CHECK(r == 100);
        CHECK(g == 100);
        CHECK(b == 100);
    }

    SUBCASE("blur spreads color") {
        ImageBuffer src(8, 8, PixelFormatIDs::RGBA8_Straight);
        ImageBuffer dst(8, 8, PixelFormatIDs::RGBA8_Straight);

        // 中央に白いピクセル、周囲は黒
        fillBuffer(src, 0, 0, 0, 255);
        setPixelRGBA8(src, 4, 4, 255, 255, 255, 255);

        filters::boxBlur(dst.viewRef(), src.view(), 1);

        uint8_t r, g, b, a;
        // 中央は周囲とブレンドされて暗くなる
        getPixelRGBA8(dst, 4, 4, r, g, b, a);
        CHECK(r < 255);
        CHECK(r > 0);
    }

    SUBCASE("radius 0 is passthrough") {
        ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
        ImageBuffer dst(4, 4, PixelFormatIDs::RGBA8_Straight);

        fillBuffer(src, 123, 45, 67, 200);

        filters::boxBlur(dst.viewRef(), src.view(), 0);

        uint8_t r, g, b, a;
        getPixelRGBA8(dst, 0, 0, r, g, b, a);
        CHECK(r == 123);
        CHECK(g == 45);
        CHECK(b == 67);
        CHECK(a == 200);
    }
}

// =============================================================================
// boxBlurWithPadding Tests
// =============================================================================

TEST_CASE("filters::boxBlurWithPadding") {
    SUBCASE("center pixel with transparent padding") {
        ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
        ImageBuffer dst(8, 8, PixelFormatIDs::RGBA8_Straight);

        // srcに不透明赤
        fillBuffer(src, 255, 0, 0, 255);

        // srcをdstの中央に配置（offset 2,2）
        filters::boxBlurWithPadding(dst.viewRef(), src.view(), 2, 2, 1);

        uint8_t r, g, b, a;

        // dstの中央（srcの範囲内）は色がある
        getPixelRGBA8(dst, 4, 4, r, g, b, a);
        CHECK(r > 0);
        CHECK(a > 0);

        // dstの角（srcの範囲外）は透明に近づく
        getPixelRGBA8(dst, 0, 0, r, g, b, a);
        CHECK(a < 128);  // 透明に近い
    }

    SUBCASE("larger radius spreads more") {
        ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
        ImageBuffer dst1(8, 8, PixelFormatIDs::RGBA8_Straight);
        ImageBuffer dst2(8, 8, PixelFormatIDs::RGBA8_Straight);

        fillBuffer(src, 255, 0, 0, 255);

        filters::boxBlurWithPadding(dst1.viewRef(), src.view(), 2, 2, 1);
        filters::boxBlurWithPadding(dst2.viewRef(), src.view(), 2, 2, 3);

        uint8_t r1, g1, b1, a1;
        uint8_t r2, g2, b2, a2;

        getPixelRGBA8(dst1, 0, 0, r1, g1, b1, a1);
        getPixelRGBA8(dst2, 0, 0, r2, g2, b2, a2);

        // 半径が大きいほどエッジに色が広がる
        CHECK(a2 >= a1);
    }
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("filters edge cases") {
    SUBCASE("in-place operation (src == dst)") {
        ImageBuffer buf(4, 4, PixelFormatIDs::RGBA8_Straight);
        fillBuffer(buf, 100, 100, 100, 255);

        // 同じバッファを入出力に使用
        filters::brightness(buf.viewRef(), buf.view(), 0.1f);

        uint8_t r, g, b, a;
        getPixelRGBA8(buf, 0, 0, r, g, b, a);
        CHECK(r > 100);  // 明るくなった
    }
}
