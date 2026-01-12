// fleximg CompositeNode Unit Tests
// 合成ノードのテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/common.h"
#include "fleximg/types.h"
#include "fleximg/render_types.h"
#include "fleximg/image_buffer.h"
#include "fleximg/nodes/composite_node.h"
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

// ピクセル色を取得
static void getPixelRGBA8(const ViewPort& view, int x, int y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    const uint8_t* p = static_cast<const uint8_t*>(view.pixelAt(x, y));
    r = p[0]; g = p[1]; b = p[2]; a = p[3];
}

// =============================================================================
// CompositeNode Construction Tests
// =============================================================================

TEST_CASE("CompositeNode basic construction") {
    SUBCASE("default 2 inputs") {
        CompositeNode node;
        CHECK(node.inputCount() == 2);
        CHECK(node.name() != nullptr);
    }

    SUBCASE("custom input count") {
        CompositeNode node3(3);
        CHECK(node3.inputCount() == 3);

        CompositeNode node5(5);
        CHECK(node5.inputCount() == 5);
    }
}

TEST_CASE("CompositeNode setInputCount") {
    CompositeNode node;
    CHECK(node.inputCount() == 2);

    node.setInputCount(4);
    CHECK(node.inputCount() == 4);

    node.setInputCount(1);
    CHECK(node.inputCount() == 1);

    // 0以下は1にクランプ
    node.setInputCount(0);
    CHECK(node.inputCount() == 1);

    node.setInputCount(-1);
    CHECK(node.inputCount() == 1);
}

// =============================================================================
// CompositeNode Compositing Tests
// =============================================================================

TEST_CASE("CompositeNode single opaque input") {
    const int imgSize = 32;
    const int canvasSize = 64;

    // 赤い不透明画像
    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
    ViewPort srcView = srcImg.view();

    // 出力バッファ
    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    // ノード構築（1入力のComposite）
    SourceNode src(srcView, imgSize / 2.0f, imgSize / 2.0f);
    CompositeNode composite(1);
    RendererNode renderer;
    SinkNode sink(dstView, canvasSize / 2.0f, canvasSize / 2.0f);

    src >> composite >> renderer >> sink;

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // 出力に赤いピクセルがあることを確認
    bool foundRed = false;
    for (int y = 0; y < canvasSize && !foundRed; y++) {
        for (int x = 0; x < canvasSize && !foundRed; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, y, r, g, b, a);
            if (r > 128 && a > 128) foundRed = true;
        }
    }
    CHECK(foundRed);
}

TEST_CASE("CompositeNode two inputs compositing") {
    const int imgSize = 32;
    const int canvasSize = 64;

    // 背景：不透明赤
    ImageBuffer bgImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
    ViewPort bgView = bgImg.view();

    // 前景：半透明緑
    ImageBuffer fgImg = createSolidImage(imgSize, imgSize, 0, 255, 0, 128);
    ViewPort fgView = fgImg.view();

    // 出力バッファ
    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    // ノード構築
    SourceNode bgSrc(bgView, imgSize / 2.0f, imgSize / 2.0f);
    SourceNode fgSrc(fgView, imgSize / 2.0f, imgSize / 2.0f);
    CompositeNode composite(2);
    RendererNode renderer;
    SinkNode sink(dstView, canvasSize / 2.0f, canvasSize / 2.0f);

    bgSrc >> composite;
    fgSrc.connectTo(composite, 1);
    composite >> renderer >> sink;

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // 合成されたピクセルがあることを確認（赤と緑が混ざる）
    bool foundComposite = false;
    for (int y = 0; y < canvasSize && !foundComposite; y++) {
        for (int x = 0; x < canvasSize && !foundComposite; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, y, r, g, b, a);
            // 赤と緑の両方が存在するピクセル = 合成された
            if (r > 50 && g > 50 && a > 128) foundComposite = true;
        }
    }
    CHECK(foundComposite);
}

TEST_CASE("CompositeNode empty inputs") {
    const int canvasSize = 64;

    // 出力バッファ
    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    // ノード構築（入力なし）
    CompositeNode composite(2);  // 2入力だが接続なし
    RendererNode renderer;
    SinkNode sink(dstView, canvasSize / 2.0f, canvasSize / 2.0f);

    composite >> renderer >> sink;

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // エラーなく完了すればOK
    CHECK(true);
}

// =============================================================================
// CompositeNode Port Management Tests
// =============================================================================

TEST_CASE("CompositeNode port access") {
    CompositeNode node(3);

    SUBCASE("input ports exist") {
        CHECK(node.inputPort(0) != nullptr);
        CHECK(node.inputPort(1) != nullptr);
        CHECK(node.inputPort(2) != nullptr);
    }

    SUBCASE("output port exists") {
        CHECK(node.outputPort(0) != nullptr);
    }
}
