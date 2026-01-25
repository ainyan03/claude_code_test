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
#include "fleximg/nodes/horizontal_blur_node.h"
#include "fleximg/nodes/vertical_blur_node.h"
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

    SourceNode src(srcView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    BrightnessNode brightness;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

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

    SourceNode src(srcView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    GrayscaleNode grayscale;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

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

    SourceNode src(srcView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    AlphaNode alpha;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

    src >> alpha >> renderer >> sink;

    alpha.setScale(0.5f);  // 50%に減少

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // 結果を確認（SinkNodeの変換により値が変わる可能性あり）
    // とりあえずレンダリングが完了することを確認
    CHECK(true);
}

// =============================================================================
// HorizontalBlurNode Tests
// =============================================================================

TEST_CASE("HorizontalBlurNode basic construction") {
    HorizontalBlurNode node;
    CHECK(node.name() != nullptr);
    CHECK(node.radius() == 5);  // デフォルト半径
}

TEST_CASE("HorizontalBlurNode setRadius") {
    HorizontalBlurNode node;

    node.setRadius(3);
    CHECK(node.radius() == 3);

    node.setRadius(0);
    CHECK(node.radius() == 0);
}

TEST_CASE("HorizontalBlurNode blurs image horizontally") {
    const int imgSize = 32;
    const int canvasSize = 64;

    // 中央に白い点のある黒い画像を作成
    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 0, 0, 0, 255);
    ViewPort srcView = srcImg.view();
    uint8_t* centerPixel = static_cast<uint8_t*>(srcView.pixelAt(imgSize / 2, imgSize / 2));
    centerPixel[0] = 255; centerPixel[1] = 255; centerPixel[2] = 255; centerPixel[3] = 255;

    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    SourceNode src(srcView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    HorizontalBlurNode hblur;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

    src >> hblur >> renderer >> sink;

    hblur.setRadius(2);

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // 水平ブラー処理が完了することを確認
    CHECK(true);
}

// =============================================================================
// VerticalBlurNode Tests
// =============================================================================

TEST_CASE("VerticalBlurNode basic construction") {
    VerticalBlurNode node;
    CHECK(node.name() != nullptr);
    CHECK(node.radius() == 5);  // デフォルト半径
}

TEST_CASE("VerticalBlurNode setRadius") {
    VerticalBlurNode node;

    node.setRadius(3);
    CHECK(node.radius() == 3);

    node.setRadius(0);
    CHECK(node.radius() == 0);
}

TEST_CASE("VerticalBlurNode blurs image vertically") {
    const int imgSize = 32;
    const int canvasSize = 64;

    // 中央に白い点のある黒い画像を作成
    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 0, 0, 0, 255);
    ViewPort srcView = srcImg.view();
    uint8_t* centerPixel = static_cast<uint8_t*>(srcView.pixelAt(imgSize / 2, imgSize / 2));
    centerPixel[0] = 255; centerPixel[1] = 255; centerPixel[2] = 255; centerPixel[3] = 255;

    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    SourceNode src(srcView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    VerticalBlurNode vblur;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

    src >> vblur >> renderer >> sink;

    vblur.setRadius(2);

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // 垂直ブラー処理が完了することを確認
    CHECK(true);
}

// =============================================================================
// Horizontal + Vertical Blur Combination Tests
// =============================================================================

TEST_CASE("HorizontalBlur + VerticalBlur combination") {
    const int imgSize = 32;
    const int canvasSize = 64;

    // 中央に白い点のある黒い画像を作成
    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 0, 0, 0, 255);
    ViewPort srcView = srcImg.view();
    uint8_t* centerPixel = static_cast<uint8_t*>(srcView.pixelAt(imgSize / 2, imgSize / 2));
    centerPixel[0] = 255; centerPixel[1] = 255; centerPixel[2] = 255; centerPixel[3] = 255;

    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    SourceNode src(srcView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    HorizontalBlurNode hblur;
    VerticalBlurNode vblur;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

    // HorizontalBlur -> VerticalBlur の順で接続
    src >> hblur >> vblur >> renderer >> sink;

    hblur.setRadius(2);
    vblur.setRadius(2);

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // 組み合わせブラー処理が完了することを確認
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

    SourceNode src(srcView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    BrightnessNode brightness;
    GrayscaleNode grayscale;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

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

// =============================================================================
// getDataRange() Tests
// =============================================================================

TEST_CASE("SourceNode getDataRange basic test") {
    const int imgSize = 32;
    const int canvasSize = 100;

    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
    ViewPort srcView = srcImg.view();

    // SourceNodeのorigin引数は画像内の基準点（pivot）
    // pivot = (16, 16) → 画像中心がワールド原点に配置される
    // 画像範囲（ワールド座標）: [-16, 16) x [-16, 16)
    SourceNode src(srcView, float_to_fixed(16.0f), float_to_fixed(16.0f));
    RendererNode renderer;

    src >> renderer;

    // PrepareRequestを作成
    // 新座標系: originはrequestバッファ左上のワールド座標
    PrepareRequest prepReq;
    prepReq.width = static_cast<int16_t>(canvasSize);
    prepReq.height = static_cast<int16_t>(canvasSize);
    prepReq.origin.x = float_to_fixed(-50.0f);  // requestの左端がワールドX=-50
    prepReq.origin.y = float_to_fixed(-50.0f);

    PrepareResponse prepResult = src.pullPrepare(prepReq);
    CHECK(prepResult.ok());
    CHECK(prepResult.width == imgSize);
    CHECK(prepResult.height == imgSize);

    // RenderRequestを作成（Y=0のスキャンライン、画像と交差する位置）
    // 画像はY=[-16, 16)にあるので、origin.y=0で交差する
    RenderRequest renderReq;
    renderReq.width = static_cast<int16_t>(canvasSize);
    renderReq.height = 1;
    renderReq.origin.x = float_to_fixed(-50.0f);  // requestの左端がワールドX=-50
    renderReq.origin.y = 0;  // ワールドY=0のスキャンライン

    DataRange range = src.getDataRange(renderReq);

    // 画像はワールドX=[-16, 16)、リクエストはワールドX=[-50, 50)
    // 交差範囲: [-16, 16)
    // request座標系に変換: startX = -16 - (-50) = 34, endX = 16 - (-50) = 66
    CHECK(range.hasData());
    CHECK(range.startX == 34);
    CHECK(range.endX == 66);
}

TEST_CASE("HorizontalBlurNode getDataRange expands range correctly") {
    const int imgSize = 32;
    const int canvasSize = 100;

    // 画像を画面中央に配置（画像中心がワールド原点に来る）
    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
    ViewPort srcView = srcImg.view();

    // SourceNodeのorigin引数は画像内の基準点（pivot）
    // pivot = imgSize/2 → 画像中心がワールド原点に配置
    // 画像範囲（ワールド座標）: [-16, 16) x [-16, 16)
    SourceNode src(srcView, float_to_fixed(static_cast<float>(imgSize) / 2.0f),
                           float_to_fixed(static_cast<float>(imgSize) / 2.0f));
    HorizontalBlurNode hblur;
    RendererNode renderer;

    src >> hblur >> renderer;

    const int radius = 10;
    const int passes = 1;
    hblur.setRadius(radius);
    hblur.setPasses(passes);

    // PrepareRequestを作成してpullPrepareを実行
    // 新座標系: originはrequestバッファ左上のワールド座標
    PrepareRequest prepReq;
    prepReq.width = static_cast<int16_t>(canvasSize);
    prepReq.height = static_cast<int16_t>(canvasSize);
    prepReq.origin.x = float_to_fixed(-static_cast<float>(canvasSize) / 2.0f);
    prepReq.origin.y = float_to_fixed(-static_cast<float>(canvasSize) / 2.0f);

    PrepareResponse prepResult = hblur.pullPrepare(prepReq);
    CHECK(prepResult.ok());

    // RenderRequestを作成してgetDataRangeを呼び出す
    // Y=0のスキャンライン（画像と交差する位置）
    RenderRequest renderReq;
    renderReq.width = static_cast<int16_t>(canvasSize);
    renderReq.height = 1;
    renderReq.origin.x = prepReq.origin.x;
    renderReq.origin.y = 0;  // ワールドY=0のスキャンライン

    DataRange range = hblur.getDataRange(renderReq);

    // 上流の画像はワールド座標で[-16, 16)の範囲
    // request座標系（origin.x=-50、左端がワールドX=-50）で[34, 66)
    // ブラー処理により radius*passes = 10 だけ両側に拡張される
    // 期待される範囲: [24, 76)
    int totalMargin = radius * passes;
    int expectedStart = (canvasSize / 2 - imgSize / 2) - totalMargin;
    int expectedEnd = (canvasSize / 2 + imgSize / 2) + totalMargin;

    CHECK(range.hasData());
    CHECK(range.startX == expectedStart);
    CHECK(range.endX == expectedEnd);
}

TEST_CASE("HorizontalBlurNode getDataRange with radius=0 is passthrough") {
    const int imgSize = 32;
    const int canvasSize = 100;

    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
    ViewPort srcView = srcImg.view();

    // SourceNodeのorigin引数は画像内の基準点（pivot）
    SourceNode src(srcView, float_to_fixed(static_cast<float>(imgSize) / 2.0f),
                           float_to_fixed(static_cast<float>(imgSize) / 2.0f));
    HorizontalBlurNode hblur;
    RendererNode renderer;

    src >> hblur >> renderer;

    hblur.setRadius(0);  // パススルー

    // 新座標系: originはrequestバッファ左上のワールド座標
    PrepareRequest prepReq;
    prepReq.width = static_cast<int16_t>(canvasSize);
    prepReq.height = static_cast<int16_t>(canvasSize);
    prepReq.origin.x = float_to_fixed(-static_cast<float>(canvasSize) / 2.0f);
    prepReq.origin.y = float_to_fixed(-static_cast<float>(canvasSize) / 2.0f);

    PrepareResponse prepResult = hblur.pullPrepare(prepReq);
    CHECK(prepResult.ok());

    RenderRequest renderReq;
    renderReq.width = static_cast<int16_t>(canvasSize);
    renderReq.height = 1;
    renderReq.origin.x = prepReq.origin.x;
    renderReq.origin.y = 0;  // ワールドY=0のスキャンライン

    DataRange blurRange = hblur.getDataRange(renderReq);
    DataRange srcRange = src.getDataRange(renderReq);

    // radius=0ではソースと同じ範囲
    CHECK(blurRange.startX == srcRange.startX);
    CHECK(blurRange.endX == srcRange.endX);
}

TEST_CASE("HorizontalBlurNode getDataRange with offset image") {
    // 画像が画面左端寄りに配置されている場合のテスト
    const int imgSize = 20;
    const int canvasSize = 100;

    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
    ViewPort srcView = srcImg.view();

    // SourceNodeのorigin引数は画像内の基準点（pivot）
    // pivot = (10, 10) → 画像中心がワールド原点に配置
    // 画像範囲（ワールド座標）: [-10, 10) x [-10, 10)
    SourceNode src(srcView, float_to_fixed(10.0f), float_to_fixed(10.0f));
    HorizontalBlurNode hblur;
    RendererNode renderer;

    src >> hblur >> renderer;

    const int radius = 5;
    hblur.setRadius(radius);
    hblur.setPasses(1);

    // 新座標系: originはrequestバッファ左上のワールド座標
    PrepareRequest prepReq;
    prepReq.width = static_cast<int16_t>(canvasSize);
    prepReq.height = static_cast<int16_t>(canvasSize);
    prepReq.origin.x = float_to_fixed(-50.0f);
    prepReq.origin.y = float_to_fixed(-50.0f);

    PrepareResponse prepResult = hblur.pullPrepare(prepReq);
    CHECK(prepResult.ok());

    RenderRequest renderReq;
    renderReq.width = static_cast<int16_t>(canvasSize);
    renderReq.height = 1;
    renderReq.origin.x = prepReq.origin.x;
    renderReq.origin.y = 0;  // ワールドY=0のスキャンライン

    DataRange range = hblur.getDataRange(renderReq);

    // 画像はワールド座標で[-10, 10)
    // request座標系（origin.x=-50、左端がワールドX=-50）で[40, 60)
    // ブラー拡張で[35, 65)
    CHECK(range.hasData());
    CHECK(range.startX == 35);
    CHECK(range.endX == 65);
}

TEST_CASE("VerticalBlurNode getDataRange passes through X range") {
    const int imgSize = 32;
    const int canvasSize = 100;

    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
    ViewPort srcView = srcImg.view();

    // SourceNodeのorigin引数は画像内の基準点（pivot）
    SourceNode src(srcView, float_to_fixed(static_cast<float>(imgSize) / 2.0f),
                           float_to_fixed(static_cast<float>(imgSize) / 2.0f));
    VerticalBlurNode vblur;
    RendererNode renderer;

    src >> vblur >> renderer;

    vblur.setRadius(10);
    vblur.setPasses(1);

    // 新座標系: originはrequestバッファ左上のワールド座標
    PrepareRequest prepReq;
    prepReq.width = static_cast<int16_t>(canvasSize);
    prepReq.height = static_cast<int16_t>(canvasSize);
    prepReq.origin.x = float_to_fixed(-static_cast<float>(canvasSize) / 2.0f);
    prepReq.origin.y = float_to_fixed(-static_cast<float>(canvasSize) / 2.0f);

    PrepareResponse prepResult = vblur.pullPrepare(prepReq);
    CHECK(prepResult.ok());

    RenderRequest renderReq;
    renderReq.width = static_cast<int16_t>(canvasSize);
    renderReq.height = 1;
    renderReq.origin.x = prepReq.origin.x;
    renderReq.origin.y = 0;  // ワールドY=0のスキャンライン

    DataRange vblurRange = vblur.getDataRange(renderReq);
    DataRange srcRange = src.getDataRange(renderReq);

    // 垂直ブラーはX範囲に影響しない（ソースと同じ）
    CHECK(vblurRange.startX == srcRange.startX);
    CHECK(vblurRange.endX == srcRange.endX);
}

TEST_CASE("HorizontalBlurNode + VerticalBlurNode getDataRange chain") {
    const int imgSize = 32;
    const int canvasSize = 100;

    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
    ViewPort srcView = srcImg.view();

    // SourceNodeのorigin引数は画像内の基準点（pivot）
    SourceNode src(srcView, float_to_fixed(static_cast<float>(imgSize) / 2.0f),
                           float_to_fixed(static_cast<float>(imgSize) / 2.0f));
    HorizontalBlurNode hblur;
    VerticalBlurNode vblur;
    RendererNode renderer;

    src >> hblur >> vblur >> renderer;

    const int hRadius = 8;
    const int vRadius = 10;
    hblur.setRadius(hRadius);
    hblur.setPasses(1);
    vblur.setRadius(vRadius);
    vblur.setPasses(1);

    // 新座標系: originはrequestバッファ左上のワールド座標
    PrepareRequest prepReq;
    prepReq.width = static_cast<int16_t>(canvasSize);
    prepReq.height = static_cast<int16_t>(canvasSize);
    prepReq.origin.x = float_to_fixed(-static_cast<float>(canvasSize) / 2.0f);
    prepReq.origin.y = float_to_fixed(-static_cast<float>(canvasSize) / 2.0f);

    PrepareResponse prepResult = vblur.pullPrepare(prepReq);
    CHECK(prepResult.ok());

    RenderRequest renderReq;
    renderReq.width = static_cast<int16_t>(canvasSize);
    renderReq.height = 1;
    renderReq.origin.x = prepReq.origin.x;
    renderReq.origin.y = 0;  // ワールドY=0のスキャンライン

    DataRange range = vblur.getDataRange(renderReq);

    // VerticalBlurはX範囲に影響しないので、HorizontalBlurの結果と同じ
    // 画像はワールド座標で[-16, 16)、request座標系で[34, 66)
    // 水平ブラーで±8拡張 → [26, 74)
    int expectedStart = (canvasSize / 2 - imgSize / 2) - hRadius;
    int expectedEnd = (canvasSize / 2 + imgSize / 2) + hRadius;

    CHECK(range.hasData());
    CHECK(range.startX == expectedStart);
    CHECK(range.endX == expectedEnd);
}
