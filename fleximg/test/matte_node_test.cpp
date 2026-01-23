// fleximg MatteNode Unit Tests
// マット合成ノードのテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/core/common.h"
#include "fleximg/core/types.h"
#include "fleximg/image/render_types.h"
#include "fleximg/image/image_buffer.h"
#include "fleximg/nodes/matte_node.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/sink_node.h"
#include "fleximg/nodes/renderer_node.h"
#include <string>

using namespace fleximg;

// =============================================================================
// Helper Functions
// =============================================================================

// 単色RGBA画像を作成
static ImageBuffer createSolidImage(int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    ImageBuffer img(width, height, PixelFormatIDs::RGBA8_Straight);
    ViewPort view = img.view();

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t* p = static_cast<uint8_t*>(view.pixelAt(x, y));
            p[0] = r; p[1] = g; p[2] = b; p[3] = a;
        }
    }

    return img;
}

// 単色Alpha8画像を作成
static ImageBuffer createAlphaMask(int width, int height, uint8_t alpha) {
    ImageBuffer img(width, height, PixelFormatIDs::Alpha8);
    ViewPort view = img.view();

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t* p = static_cast<uint8_t*>(view.pixelAt(x, y));
            p[0] = alpha;
        }
    }

    return img;
}

// ピクセル色を取得
static void getPixelRGBA8(const ViewPort& view, int x, int y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    const uint8_t* p = static_cast<const uint8_t*>(view.pixelAt(x, y));
    r = p[0]; g = p[1]; b = p[2]; a = p[3];
}

// =============================================================================
// MatteNode Construction Tests
// =============================================================================

TEST_CASE("MatteNode basic construction") {
    MatteNode node;
    CHECK(node.name() != nullptr);
    CHECK(std::string(node.name()) == "MatteNode");
}

TEST_CASE("MatteNode port configuration") {
    MatteNode node;

    SUBCASE("has 3 input ports") {
        CHECK(node.inputPort(0) != nullptr);
        CHECK(node.inputPort(1) != nullptr);
        CHECK(node.inputPort(2) != nullptr);
    }

    SUBCASE("has 1 output port") {
        CHECK(node.outputPort(0) != nullptr);
    }
}

// =============================================================================
// MatteNode Matte Compositing Tests
// =============================================================================

TEST_CASE("MatteNode alpha=255 shows foreground only") {
    const int imgSize = 32;
    const int canvasSize = 64;

    // 前景：不透明赤
    ImageBuffer fgImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
    ViewPort fgView = fgImg.view();

    // 背景：不透明青
    ImageBuffer bgImg = createSolidImage(imgSize, imgSize, 0, 0, 255, 255);
    ViewPort bgView = bgImg.view();

    // マスク：全面白（alpha=255）
    ImageBuffer maskImg = createAlphaMask(imgSize, imgSize, 255);
    ViewPort maskView = maskImg.view();

    // 出力バッファ
    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    // ノード構築
    SourceNode fgSrc(fgView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    SourceNode bgSrc(bgView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    SourceNode maskSrc(maskView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    MatteNode matte;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

    // 接続: fg >> matte(0), bg >> matte(1), mask >> matte(2)
    fgSrc >> matte;
    bgSrc.connectTo(matte, 1);
    maskSrc.connectTo(matte, 2);
    matte >> renderer >> sink;

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // alpha=255なので前景（赤）のみが見えるはず
    bool foundRed = false;
    bool foundBlue = false;
    for (int y = 0; y < canvasSize; y++) {
        for (int x = 0; x < canvasSize; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, y, r, g, b, a);
            if (a > 128) {
                if (r > 200 && b < 50) foundRed = true;
                if (b > 200 && r < 50) foundBlue = true;
            }
        }
    }
    CHECK(foundRed);
    CHECK_FALSE(foundBlue);
}

TEST_CASE("MatteNode alpha=0 shows background only") {
    const int imgSize = 32;
    const int canvasSize = 64;

    // 前景：不透明赤
    ImageBuffer fgImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
    ViewPort fgView = fgImg.view();

    // 背景：不透明青
    ImageBuffer bgImg = createSolidImage(imgSize, imgSize, 0, 0, 255, 255);
    ViewPort bgView = bgImg.view();

    // マスク：全面黒（alpha=0）
    ImageBuffer maskImg = createAlphaMask(imgSize, imgSize, 0);
    ViewPort maskView = maskImg.view();

    // 出力バッファ
    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    // ノード構築
    SourceNode fgSrc(fgView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    SourceNode bgSrc(bgView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    SourceNode maskSrc(maskView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    MatteNode matte;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

    fgSrc >> matte;
    bgSrc.connectTo(matte, 1);
    maskSrc.connectTo(matte, 2);
    matte >> renderer >> sink;

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // alpha=0なので背景（青）のみが見えるはず
    bool foundRed = false;
    bool foundBlue = false;
    for (int y = 0; y < canvasSize; y++) {
        for (int x = 0; x < canvasSize; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, y, r, g, b, a);
            if (a > 128) {
                if (r > 200 && b < 50) foundRed = true;
                if (b > 200 && r < 50) foundBlue = true;
            }
        }
    }
    CHECK_FALSE(foundRed);
    CHECK(foundBlue);
}

TEST_CASE("MatteNode alpha=128 blends both images") {
    const int imgSize = 32;
    const int canvasSize = 64;

    // 前景：不透明赤
    ImageBuffer fgImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
    ViewPort fgView = fgImg.view();

    // 背景：不透明青
    ImageBuffer bgImg = createSolidImage(imgSize, imgSize, 0, 0, 255, 255);
    ViewPort bgView = bgImg.view();

    // マスク：半透明（alpha=128）
    ImageBuffer maskImg = createAlphaMask(imgSize, imgSize, 128);
    ViewPort maskView = maskImg.view();

    // 出力バッファ
    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    // ノード構築
    SourceNode fgSrc(fgView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    SourceNode bgSrc(bgView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    SourceNode maskSrc(maskView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    MatteNode matte;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

    fgSrc >> matte;
    bgSrc.connectTo(matte, 1);
    maskSrc.connectTo(matte, 2);
    matte >> renderer >> sink;

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // alpha=128なので赤と青が混ざって紫っぽくなるはず
    bool foundBlended = false;
    for (int y = 0; y < canvasSize; y++) {
        for (int x = 0; x < canvasSize; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, y, r, g, b, a);
            // 赤と青の両方が存在するピクセル = ブレンドされた
            if (r > 50 && b > 50 && a > 128) {
                foundBlended = true;
                break;
            }
        }
        if (foundBlended) break;
    }
    CHECK(foundBlended);
}

// =============================================================================
// MatteNode Input Omission Tests
// =============================================================================

TEST_CASE("MatteNode without background (foreground + mask only)") {
    const int imgSize = 32;
    const int canvasSize = 64;

    // 前景：不透明赤
    ImageBuffer fgImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
    ViewPort fgView = fgImg.view();

    // マスク：alpha=128（前景50%、背景50%=透明）
    ImageBuffer maskImg = createAlphaMask(imgSize, imgSize, 128);
    ViewPort maskView = maskImg.view();

    // 出力バッファ
    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    // ノード構築（背景なし）
    SourceNode fgSrc(fgView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    SourceNode maskSrc(maskView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    MatteNode matte;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

    fgSrc >> matte;
    // bgは未接続
    maskSrc.connectTo(matte, 2);
    matte >> renderer >> sink;

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // エラーなく完了すればOK（背景=透明黒として処理）
    CHECK(true);
}

TEST_CASE("MatteNode without mask (default alpha=0)") {
    const int imgSize = 32;
    const int canvasSize = 64;

    // 前景：不透明赤
    ImageBuffer fgImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
    ViewPort fgView = fgImg.view();

    // 背景：不透明青
    ImageBuffer bgImg = createSolidImage(imgSize, imgSize, 0, 0, 255, 255);
    ViewPort bgView = bgImg.view();

    // 出力バッファ
    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    // ノード構築（マスクなし）
    SourceNode fgSrc(fgView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    SourceNode bgSrc(bgView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    MatteNode matte;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

    fgSrc >> matte;
    bgSrc.connectTo(matte, 1);
    // maskは未接続（alpha=0として扱う=背景のみ）
    matte >> renderer >> sink;

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // マスクなし=alpha=0なので背景（青）のみが見えるはず
    bool foundBlue = false;
    for (int y = 0; y < canvasSize; y++) {
        for (int x = 0; x < canvasSize; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, y, r, g, b, a);
            if (b > 200 && r < 50 && a > 128) {
                foundBlue = true;
                break;
            }
        }
        if (foundBlue) break;
    }
    CHECK(foundBlue);
}

TEST_CASE("MatteNode all inputs empty") {
    const int canvasSize = 64;

    // 出力バッファ
    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    // ノード構築（入力なし）
    MatteNode matte;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

    matte >> renderer >> sink;

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // エラーなく完了すればOK
    CHECK(true);
}
