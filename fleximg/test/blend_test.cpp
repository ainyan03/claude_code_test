// fleximg blend operations Unit Tests
// ブレンド操作のテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/image/image_buffer.h"
#include "fleximg/operations/canvas_utils.h"

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


// =============================================================================
// canvas_utils::placeFirst Tests
// =============================================================================

TEST_CASE("placeFirst basic copy") {
    // 同一フォーマット間のコピー
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dst(4, 4, PixelFormatIDs::RGBA8_Straight);

    // srcに赤ピクセルを設定
    setPixelRGBA8(src, 1, 1, 255, 0, 0, 255);

    // 基準点を中央に設定
    int_fixed srcOrigin = to_fixed(2);
    int_fixed dstOrigin = to_fixed(2);

    ViewPort dstView = dst.viewRef();
    canvas_utils::placeFirst(dstView, dstOrigin, dstOrigin,
                             src.view(), srcOrigin, srcOrigin);

    // コピーされていることを確認
    uint8_t r, g, b, a;
    getPixelRGBA8(dst, 1, 1, r, g, b, a);
    CHECK(r == 255);
    CHECK(g == 0);
    CHECK(b == 0);
    CHECK(a == 255);
}

TEST_CASE("placeFirst with offset") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dst(8, 8, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);

    // srcの(0,0)に赤ピクセル
    setPixelRGBA8(src, 0, 0, 255, 0, 0, 255);

    // 新座標系: canvasOrigin=(0,0)、srcOrigin=(4,4)
    // → srcの左上がワールド(4,4)、canvasの左上がワールド(0,0)
    // → srcの(0,0)がcanvasの(4,4)に配置される
    ViewPort dstView = dst.viewRef();
    canvas_utils::placeFirst(dstView, to_fixed(0), to_fixed(0),
                             src.view(), to_fixed(4), to_fixed(4));

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


TEST_CASE("placeFirst clipping") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dst(4, 4, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);

    // 全ピクセルを赤に
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            setPixelRGBA8(src, x, y, 255, 0, 0, 255);
        }
    }

    // 新座標系: canvasOrigin=(2,2)、srcOrigin=(0,0)
    // → srcの左上がワールド(0,0)、canvasの左上がワールド(2,2)
    // → srcはcanvasより左上にある → srcの(2,2)-(3,3)がcanvasの(0,0)-(1,1)に配置
    ViewPort dstView = dst.viewRef();
    canvas_utils::placeFirst(dstView, to_fixed(2), to_fixed(2),
                             src.view(), to_fixed(0), to_fixed(0));

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
// Edge Cases
// =============================================================================

TEST_CASE("placeFirst with invalid viewports") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
    ViewPort invalidDst;  // invalid

    // should not crash
    canvas_utils::placeFirst(invalidDst, to_fixed(0), to_fixed(0),
                             src.view(), to_fixed(0), to_fixed(0));
    CHECK(true);  // reached here without crash
}

TEST_CASE("placeFirst with completely out of bounds") {
    ImageBuffer src(4, 4, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dst(4, 4, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);

    setPixelRGBA8(src, 0, 0, 255, 0, 0, 255);

    // srcを完全にdstの外に配置
    ViewPort dstView = dst.viewRef();
    canvas_utils::placeFirst(dstView, to_fixed(0), to_fixed(0),
                             src.view(), to_fixed(100), to_fixed(100));

    // dstは変更されない
    uint8_t r, g, b, a;
    getPixelRGBA8(dst, 0, 0, r, g, b, a);
    CHECK(r == 0);
    CHECK(a == 0);
}
