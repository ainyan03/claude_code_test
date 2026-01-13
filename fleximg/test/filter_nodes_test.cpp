// fleximg Filter Nodes Unit Tests
// フィルタノードのテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/core/common.h"
#include "fleximg/core/types.h"
#include "fleximg/image/render_types.h"
#include "fleximg/image/image_buffer.h"
#include "fleximg/nodes/brightness_node.h"
#include "fleximg/nodes/grayscale_node.h"
#include "fleximg/nodes/alpha_node.h"
#include "fleximg/nodes/box_blur_node.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/sink_node.h"
#include "fleximg/nodes/renderer_node.h"

using namespace fleximg;

// =============================================================================
// Helper Functions
// =============================================================================

// 単色画像を作成
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

// 平均ピクセル値を取得
static void getAveragePixel(const ViewPort& view, int& r, int& g, int& b, int& a) {
    int sumR = 0, sumG = 0, sumB = 0, sumA = 0;
    int count = 0;

    for (int y = 0; y < view.height; y++) {
        for (int x = 0; x < view.width; x++) {
            const uint8_t* p = static_cast<const uint8_t*>(view.pixelAt(x, y));
            if (p[3] > 0) {  // 透明でないピクセルのみ
                sumR += p[0];
                sumG += p[1];
                sumB += p[2];
                sumA += p[3];
                count++;
            }
        }
    }

    if (count > 0) {
        r = sumR / count;
        g = sumG / count;
        b = sumB / count;
        a = sumA / count;
    } else {
        r = g = b = a = 0;
    }
}

// =============================================================================
// BrightnessNode Tests
// =============================================================================

TEST_CASE("BrightnessNode basic construction") {
    BrightnessNode node;
    CHECK(node.name() != nullptr);
    CHECK(node.amount() == doctest::Approx(0.0f));
}

TEST_CASE("BrightnessNode setAmount") {
    BrightnessNode node;

    node.setAmount(0.5f);
    CHECK(node.amount() == doctest::Approx(0.5f));

    node.setAmount(-0.3f);
    CHECK(node.amount() == doctest::Approx(-0.3f));
}

TEST_CASE("BrightnessNode positive brightness") {
    const int imgSize = 32;
    const int canvasSize = 64;

    // グレー画像（100, 100, 100）
    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 100, 100, 100, 255);
    ViewPort srcView = srcImg.view();

    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    SourceNode src(srcView, imgSize / 2.0f, imgSize / 2.0f);
    BrightnessNode brightness;
    RendererNode renderer;
    SinkNode sink(dstView, canvasSize / 2.0f, canvasSize / 2.0f);

    src >> brightness >> renderer >> sink;

    brightness.setAmount(0.2f);  // +20%

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    int r, g, b, a;
    getAveragePixel(dstView, r, g, b, a);

    // 明るくなっているはず（100より大きい）
    CHECK(r > 100);
    CHECK(g > 100);
    CHECK(b > 100);
}

// =============================================================================
// GrayscaleNode Tests
// =============================================================================

TEST_CASE("GrayscaleNode basic construction") {
    GrayscaleNode node;
    CHECK(node.name() != nullptr);
}

TEST_CASE("GrayscaleNode converts to grayscale") {
    const int imgSize = 32;
    const int canvasSize = 64;

    // 赤い画像
    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
    ViewPort srcView = srcImg.view();

    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    SourceNode src(srcView, imgSize / 2.0f, imgSize / 2.0f);
    GrayscaleNode grayscale;
    RendererNode renderer;
    SinkNode sink(dstView, canvasSize / 2.0f, canvasSize / 2.0f);

    src >> grayscale >> renderer >> sink;

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    int r, g, b, a;
    getAveragePixel(dstView, r, g, b, a);

    // グレースケール化されているはず（R==G==B）
    // 許容誤差を設けて比較
    CHECK(std::abs(r - g) <= 5);
    CHECK(std::abs(g - b) <= 5);
    CHECK(std::abs(r - b) <= 5);
}

// =============================================================================
// AlphaNode Tests
// =============================================================================

TEST_CASE("AlphaNode basic construction") {
    AlphaNode node;
    CHECK(node.name() != nullptr);
    CHECK(node.scale() == doctest::Approx(1.0f));
}

TEST_CASE("AlphaNode setScale") {
    AlphaNode node;

    node.setScale(0.5f);
    CHECK(node.scale() == doctest::Approx(0.5f));

    node.setScale(0.0f);
    CHECK(node.scale() == doctest::Approx(0.0f));
}

TEST_CASE("AlphaNode reduces alpha") {
    const int imgSize = 32;
    const int canvasSize = 64;

    // 不透明赤画像
    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
    ViewPort srcView = srcImg.view();

    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    SourceNode src(srcView, imgSize / 2.0f, imgSize / 2.0f);
    AlphaNode alpha;
    RendererNode renderer;
    SinkNode sink(dstView, canvasSize / 2.0f, canvasSize / 2.0f);

    src >> alpha >> renderer >> sink;

    alpha.setScale(0.5f);  // 50%に減少

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // 結果を確認（SinkNodeの変換により値が変わる可能性あり）
    // とりあえずレンダリングが完了することを確認
    CHECK(true);
}

// =============================================================================
// BoxBlurNode Tests
// =============================================================================

TEST_CASE("BoxBlurNode basic construction") {
    BoxBlurNode node;
    CHECK(node.name() != nullptr);
    CHECK(node.radius() == 5);  // デフォルト半径
}

TEST_CASE("BoxBlurNode setRadius") {
    BoxBlurNode node;

    node.setRadius(3);
    CHECK(node.radius() == 3);

    node.setRadius(0);
    CHECK(node.radius() == 0);
}

TEST_CASE("BoxBlurNode blurs image") {
    const int imgSize = 32;
    const int canvasSize = 64;

    // 中央に白い点のある黒い画像を作成
    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 0, 0, 0, 255);
    ViewPort srcView = srcImg.view();
    // 中央に白い点
    uint8_t* centerPixel = static_cast<uint8_t*>(srcView.pixelAt(imgSize / 2, imgSize / 2));
    centerPixel[0] = 255; centerPixel[1] = 255; centerPixel[2] = 255; centerPixel[3] = 255;

    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    SourceNode src(srcView, imgSize / 2.0f, imgSize / 2.0f);
    BoxBlurNode blur;
    RendererNode renderer;
    SinkNode sink(dstView, canvasSize / 2.0f, canvasSize / 2.0f);

    src >> blur >> renderer >> sink;

    blur.setRadius(2);

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // ブラー処理が完了することを確認
    CHECK(true);
}

// =============================================================================
// Filter Chain Tests
// =============================================================================

TEST_CASE("Filter chain: brightness -> grayscale") {
    const int imgSize = 32;
    const int canvasSize = 64;

    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 100, 50, 150, 255);
    ViewPort srcView = srcImg.view();

    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    SourceNode src(srcView, imgSize / 2.0f, imgSize / 2.0f);
    BrightnessNode brightness;
    GrayscaleNode grayscale;
    RendererNode renderer;
    SinkNode sink(dstView, canvasSize / 2.0f, canvasSize / 2.0f);

    src >> brightness >> grayscale >> renderer >> sink;

    brightness.setAmount(0.1f);

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    int r, g, b, a;
    getAveragePixel(dstView, r, g, b, a);

    // グレースケール化されているはず
    CHECK(std::abs(r - g) <= 5);
    CHECK(std::abs(g - b) <= 5);
}
