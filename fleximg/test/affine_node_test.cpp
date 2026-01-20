// fleximg AffineNode Unit Tests
// アフィン変換ノードのテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/core/common.h"
#include "fleximg/core/types.h"
#include "fleximg/image/render_types.h"
#include "fleximg/image/image_buffer.h"
#include "fleximg/nodes/affine_node.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/sink_node.h"
#include "fleximg/nodes/renderer_node.h"

#include <cmath>

using namespace fleximg;

// =============================================================================
// Helper Functions
// =============================================================================

// テスト用画像を作成（中心に十字マーク）
static ImageBuffer createTestImage(int width, int height) {
    ImageBuffer img(width, height, PixelFormatIDs::RGBA8_Straight);
    ViewPort view = img.view();

    // 透明で初期化
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t* p = static_cast<uint8_t*>(view.pixelAt(x, y));
            p[0] = p[1] = p[2] = p[3] = 0;
        }
    }

    // 中心に赤い十字を描画
    int cx = width / 2;
    int cy = height / 2;

    // 水平線
    for (int x = 0; x < width; x++) {
        uint8_t* p = static_cast<uint8_t*>(view.pixelAt(x, cy));
        p[0] = 255; p[1] = 0; p[2] = 0; p[3] = 255;
    }
    // 垂直線
    for (int y = 0; y < height; y++) {
        uint8_t* p = static_cast<uint8_t*>(view.pixelAt(cx, y));
        p[0] = 255; p[1] = 0; p[2] = 0; p[3] = 255;
    }

    return img;
}

// 赤いピクセルの中心位置を検索
struct PixelPos {
    int x, y;
    bool found;
};

static PixelPos findRedCenter(const ViewPort& view) {
    int sumX = 0, sumY = 0, count = 0;

    for (int y = 0; y < view.height; y++) {
        for (int x = 0; x < view.width; x++) {
            const uint8_t* p = static_cast<const uint8_t*>(view.pixelAt(x, y));
            if (p[0] > 128 && p[3] > 128) {  // 赤くて不透明
                sumX += x;
                sumY += y;
                count++;
            }
        }
    }

    if (count == 0) {
        return {0, 0, false};
    }
    return {sumX / count, sumY / count, true};
}

// =============================================================================
// AffineNode Basic Tests
// =============================================================================

TEST_CASE("AffineNode basic construction") {
    AffineNode node;
    CHECK(node.name() != nullptr);

    // デフォルトは恒等変換
    const auto& m = node.matrix();
    CHECK(m.a == doctest::Approx(1.0f));
    CHECK(m.b == doctest::Approx(0.0f));
    CHECK(m.c == doctest::Approx(0.0f));
    CHECK(m.d == doctest::Approx(1.0f));
    CHECK(m.tx == doctest::Approx(0.0f));
    CHECK(m.ty == doctest::Approx(0.0f));
}

TEST_CASE("AffineNode setRotation") {
    AffineNode node;

    SUBCASE("0 degrees") {
        node.setRotation(0.0f);
        const auto& m = node.matrix();
        CHECK(m.a == doctest::Approx(1.0f));
        CHECK(m.d == doctest::Approx(1.0f));
    }

    SUBCASE("90 degrees") {
        node.setRotation(static_cast<float>(M_PI / 2.0));
        const auto& m = node.matrix();
        CHECK(m.a == doctest::Approx(0.0f).epsilon(0.001));
        CHECK(m.b == doctest::Approx(-1.0f));
        CHECK(m.c == doctest::Approx(1.0f));
        CHECK(m.d == doctest::Approx(0.0f).epsilon(0.001));
    }

    SUBCASE("180 degrees") {
        node.setRotation(static_cast<float>(M_PI));
        const auto& m = node.matrix();
        CHECK(m.a == doctest::Approx(-1.0f));
        CHECK(m.d == doctest::Approx(-1.0f));
    }
}

TEST_CASE("AffineNode setScale") {
    AffineNode node;

    SUBCASE("uniform scale") {
        node.setScale(2.0f, 2.0f);
        const auto& m = node.matrix();
        CHECK(m.a == doctest::Approx(2.0f));
        CHECK(m.d == doctest::Approx(2.0f));
        CHECK(m.b == doctest::Approx(0.0f));
        CHECK(m.c == doctest::Approx(0.0f));
    }

    SUBCASE("non-uniform scale") {
        node.setScale(3.0f, 0.5f);
        const auto& m = node.matrix();
        CHECK(m.a == doctest::Approx(3.0f));
        CHECK(m.d == doctest::Approx(0.5f));
    }
}

TEST_CASE("AffineNode setTranslation") {
    AffineNode node;
    node.setTranslation(10.5f, -5.3f);

    const auto& m = node.matrix();
    CHECK(m.a == doctest::Approx(1.0f));
    CHECK(m.d == doctest::Approx(1.0f));
    CHECK(m.tx == doctest::Approx(10.5f));
    CHECK(m.ty == doctest::Approx(-5.3f));
}

// =============================================================================
// AffineNode Pull Mode Tests
// =============================================================================

TEST_CASE("AffineNode pull mode translation only") {
    const int imgW = 32, imgH = 32;
    const int canvasW = 100, canvasH = 100;

    ImageBuffer srcImg = createTestImage(imgW, imgH);
    ViewPort srcView = srcImg.view();

    ImageBuffer dstImg(canvasW, canvasH, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);
    ViewPort dstView = dstImg.view();

    SourceNode src(srcView, float_to_fixed(imgW / 2.0f), float_to_fixed(imgH / 2.0f));
    AffineNode affine;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasW / 2.0f), float_to_fixed(canvasH / 2.0f));

    src >> affine >> renderer >> sink;

    float tx = 10.3f, ty = 5.7f;
    affine.setTranslation(tx, ty);

    renderer.setVirtualScreen(canvasW, canvasH);
    renderer.exec();

    PixelPos pos = findRedCenter(dstView);
    CHECK(pos.found);
    CHECK(pos.x >= 0);
    CHECK(pos.x < canvasW);
    CHECK(pos.y >= 0);
    CHECK(pos.y < canvasH);
}

TEST_CASE("AffineNode pull mode translation with rotation") {
    const int imgW = 32, imgH = 32;
    const int canvasW = 100, canvasH = 100;

    ImageBuffer srcImg = createTestImage(imgW, imgH);
    ViewPort srcView = srcImg.view();

    ImageBuffer dstImg(canvasW, canvasH, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);
    ViewPort dstView = dstImg.view();

    SourceNode src(srcView, float_to_fixed(imgW / 2.0f), float_to_fixed(imgH / 2.0f));
    AffineNode affine;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasW / 2.0f), float_to_fixed(canvasH / 2.0f));

    src >> affine >> renderer >> sink;

    float angle = static_cast<float>(M_PI / 4.0);  // 45度
    float tx = 10.5f, ty = 5.5f;
    float c = std::cos(angle), s = std::sin(angle);

    AffineMatrix m;
    m.a = c;  m.b = -s;
    m.c = s;  m.d = c;
    m.tx = tx; m.ty = ty;
    affine.setMatrix(m);

    renderer.setVirtualScreen(canvasW, canvasH);
    renderer.exec();

    PixelPos pos = findRedCenter(dstView);
    CHECK(pos.found);
    CHECK(pos.x >= 0);
    CHECK(pos.x < canvasW);
    CHECK(pos.y >= 0);
    CHECK(pos.y < canvasH);
}

TEST_CASE("AffineNode pull mode with tile splitting") {
    const int imgW = 32, imgH = 32;
    const int canvasW = 100, canvasH = 100;

    ImageBuffer srcImg = createTestImage(imgW, imgH);
    ViewPort srcView = srcImg.view();

    ImageBuffer dstImg(canvasW, canvasH, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);
    ViewPort dstView = dstImg.view();

    SourceNode src(srcView, float_to_fixed(imgW / 2.0f), float_to_fixed(imgH / 2.0f));
    AffineNode affine;
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasW / 2.0f), float_to_fixed(canvasH / 2.0f));

    src >> affine >> renderer >> sink;

    float tx = 7.7f, ty = 3.3f;
    affine.setTranslation(tx, ty);

    renderer.setVirtualScreen(canvasW, canvasH);
    renderer.setTileConfig(16, 16);
    renderer.exec();

    PixelPos pos = findRedCenter(dstView);
    CHECK(pos.found);
    CHECK(pos.x >= 0);
    CHECK(pos.x < canvasW);
    CHECK(pos.y >= 0);
    CHECK(pos.y < canvasH);
}

// =============================================================================
// Translation Smoothness Test
// =============================================================================

TEST_CASE("AffineNode translation smoothness") {
    const int imgW = 32, imgH = 32;
    const int canvasW = 100, canvasH = 100;

    ImageBuffer srcImg = createTestImage(imgW, imgH);
    ViewPort srcView = srcImg.view();

    SourceNode src(srcView, float_to_fixed(imgW / 2.0f), float_to_fixed(imgH / 2.0f));
    AffineNode affine;
    RendererNode renderer;

    src >> affine >> renderer;
    renderer.setVirtualScreen(canvasW, canvasH);

    int lastX = -1;
    int backwardJumps = 0;

    // tx を 0.0 から 10.0 まで 0.5 刻みで変化させる
    for (int i = 0; i <= 20; i++) {
        float tx = i * 0.5f;

        ImageBuffer dstImg(canvasW, canvasH, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);
        ViewPort dstView = dstImg.view();
        SinkNode sink(dstView, float_to_fixed(canvasW / 2.0f), float_to_fixed(canvasH / 2.0f));

        renderer.outputPort(0)->disconnect();
        renderer >> sink;

        affine.setTranslation(tx, 0.0f);
        renderer.exec();

        PixelPos pos = findRedCenter(dstView);
        if (!pos.found) continue;

        if (lastX >= 0 && pos.x < lastX) {
            backwardJumps++;
        }
        lastX = pos.x;
    }

    CHECK(backwardJumps == 0);  // 逆方向へのジャンプがないことを確認
}
