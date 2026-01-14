// fleximg Scanline Rendering Tests
// スキャンラインレンダリングテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/core/common.h"
#include "fleximg/core/types.h"
#include "fleximg/image/render_types.h"
#include "fleximg/image/image_buffer.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/sink_node.h"
#include "fleximg/nodes/affine_node.h"
#include "fleximg/nodes/renderer_node.h"

#include <cmath>
#include <vector>

using namespace fleximg;

// =============================================================================
// Helper Functions
// =============================================================================

// 単色不透明画像を作成
static ImageBuffer createSolidImage(int width, int height, uint8_t r, uint8_t g, uint8_t b) {
    ImageBuffer img(width, height, PixelFormatIDs::RGBA8_Straight);
    for (int y = 0; y < height; y++) {
        uint8_t* row = static_cast<uint8_t*>(img.pixelAt(0, y));
        for (int x = 0; x < width; x++) {
            row[x * 4 + 0] = r;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = b;
            row[x * 4 + 3] = 255;
        }
    }
    return img;
}

// 有効なピクセルがあるかチェック
static bool hasNonZeroPixels(const ViewPort& view) {
    for (int y = 0; y < view.height; y++) {
        const uint8_t* row = static_cast<const uint8_t*>(view.pixelAt(0, y));
        for (int x = 0; x < view.width; x++) {
            if (row[x * 4 + 3] > 0) return true;
        }
    }
    return false;
}

// =============================================================================
// Basic Scanline Rendering Tests
// =============================================================================

TEST_CASE("Scanline: basic rendering") {
    const int imgSize = 64;
    const int canvasSize = 128;

    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 255, 0, 0);
    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);

    SourceNode src(srcImg.view(), imgSize / 2.0f, imgSize / 2.0f);
    AffineNode affine;
    RendererNode renderer;
    SinkNode sink(dstImg.view(), canvasSize / 2.0f, canvasSize / 2.0f);

    src >> affine >> renderer >> sink;
    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    CHECK(hasNonZeroPixels(dstImg.view()));
}

TEST_CASE("Scanline: with rotation") {
    const int imgSize = 64;
    const int canvasSize = 128;

    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 0, 255, 0);
    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);

    SourceNode src(srcImg.view(), imgSize / 2.0f, imgSize / 2.0f);
    AffineNode affine;
    float angle = 45.0f * static_cast<float>(M_PI) / 180.0f;
    affine.setRotation(angle);
    RendererNode renderer;
    SinkNode sink(dstImg.view(), canvasSize / 2.0f, canvasSize / 2.0f);

    src >> affine >> renderer >> sink;
    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    CHECK(hasNonZeroPixels(dstImg.view()));
}

TEST_CASE("Scanline: with scale") {
    const int imgSize = 32;
    const int canvasSize = 128;

    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 0, 0, 255);
    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);

    SourceNode src(srcImg.view(), imgSize / 2.0f, imgSize / 2.0f);
    AffineNode affine;
    affine.setScale(2.0f, 2.0f);
    RendererNode renderer;
    SinkNode sink(dstImg.view(), canvasSize / 2.0f, canvasSize / 2.0f);

    src >> affine >> renderer >> sink;
    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    CHECK(hasNonZeroPixels(dstImg.view()));
}

// =============================================================================
// Scanline Tile Consistency Tests
// =============================================================================

TEST_CASE("Scanline: tiled vs non-tiled consistency") {
    const int imgSize = 48;
    const int canvasSize = 150;

    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 200, 100, 50);

    // 非タイル
    ImageBuffer dstImg1(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    {
        SourceNode src(srcImg.view(), imgSize / 2.0f, imgSize / 2.0f);
        AffineNode affine;
        float angle = 60.0f * static_cast<float>(M_PI) / 180.0f;
        affine.setRotation(angle);
        affine.setScale(1.5f, 1.5f);
        RendererNode renderer;
        SinkNode sink(dstImg1.view(), canvasSize / 2.0f, canvasSize / 2.0f);

        src >> affine >> renderer >> sink;
        renderer.setVirtualScreen(canvasSize, canvasSize);
        renderer.exec();
    }

    // 25x25タイル
    ImageBuffer dstImg2(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    {
        SourceNode src(srcImg.view(), imgSize / 2.0f, imgSize / 2.0f);
        AffineNode affine;
        float angle = 60.0f * static_cast<float>(M_PI) / 180.0f;
        affine.setRotation(angle);
        affine.setScale(1.5f, 1.5f);
        RendererNode renderer;
        SinkNode sink(dstImg2.view(), canvasSize / 2.0f, canvasSize / 2.0f);

        src >> affine >> renderer >> sink;
        renderer.setVirtualScreen(canvasSize, canvasSize);
        renderer.setTileConfig(25, 25);
        renderer.exec();
    }

    // 両方に出力があることを確認
    CHECK(hasNonZeroPixels(dstImg1.view()));
    CHECK(hasNonZeroPixels(dstImg2.view()));
}
