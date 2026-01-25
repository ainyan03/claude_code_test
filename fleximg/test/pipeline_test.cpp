// fleximg Pipeline Integration Tests
// パイプライン統合テスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/core/common.h"
#include "fleximg/core/types.h"
#include "fleximg/image/render_types.h"
#include "fleximg/image/image_buffer.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/sink_node.h"
#include "fleximg/nodes/affine_node.h"
#include "fleximg/nodes/grayscale_node.h"
#include "fleximg/nodes/composite_node.h"
#include "fleximg/nodes/renderer_node.h"

#include <cmath>

using namespace fleximg;

// =============================================================================
// Helper Functions
// =============================================================================

// テスト用画像を作成（グラデーション）
static ImageBuffer createGradientImage(int width, int height) {
    ImageBuffer img(width, height, PixelFormatIDs::RGBA8_Straight);
    for (int y = 0; y < height; y++) {
        uint8_t* row = static_cast<uint8_t*>(img.pixelAt(0, y));
        for (int x = 0; x < width; x++) {
            row[x * 4 + 0] = static_cast<uint8_t>(x * 255 / width);   // R
            row[x * 4 + 1] = static_cast<uint8_t>(y * 255 / height);  // G
            row[x * 4 + 2] = 128;                                      // B
            row[x * 4 + 3] = 255;                                      // A
        }
    }
    return img;
}

// ピクセル比較
static bool comparePixels(const ViewPort& a, const ViewPort& b, int tolerance = 0) {
    if (a.width != b.width || a.height != b.height) return false;
    for (int y = 0; y < a.height; y++) {
        const uint8_t* rowA = static_cast<const uint8_t*>(a.pixelAt(0, y));
        const uint8_t* rowB = static_cast<const uint8_t*>(b.pixelAt(0, y));
        for (int x = 0; x < a.width * 4; x++) {
            int diff = std::abs(static_cast<int>(rowA[x]) - static_cast<int>(rowB[x]));
            if (diff > tolerance) return false;
        }
    }
    return true;
}

// 有効なピクセルがあるかチェック
static bool hasNonZeroPixels(const ViewPort& view) {
    for (int y = 0; y < view.height; y++) {
        const uint8_t* row = static_cast<const uint8_t*>(view.pixelAt(0, y));
        for (int x = 0; x < view.width; x++) {
            if (row[x * 4 + 3] > 0) return true;  // 透明でないピクセル
        }
    }
    return false;
}

// =============================================================================
// Basic Pipeline Tests
// =============================================================================

TEST_CASE("Pipeline: basic source -> renderer -> sink") {
    ImageBuffer srcImg = createGradientImage(64, 64);
    ImageBuffer dstImg(64, 64, PixelFormatIDs::RGBA8_Straight);

    SourceNode src;
    src.setSource(srcImg.view());
    src.setPivot(0, 0);

    RendererNode renderer;
    renderer.setVirtualScreen(64, 64, 0, 0);

    SinkNode sink;
    sink.setTarget(dstImg.view());
    sink.setOrigin(0, 0);

    src >> renderer >> sink;
    renderer.exec();

    // ソースとデストが一致
    CHECK(comparePixels(srcImg.view(), dstImg.view()));
}

TEST_CASE("Pipeline: with centered origin") {
    const int imgSize = 64;
    ImageBuffer srcImg = createGradientImage(imgSize, imgSize);
    ImageBuffer dstImg(imgSize, imgSize, PixelFormatIDs::RGBA8_Straight);

    SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    RendererNode renderer;
    SinkNode sink(dstImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));

    src >> renderer >> sink;
    renderer.setVirtualScreen(imgSize, imgSize);
    renderer.exec();

    // 出力があることを確認（フォーマット変換により完全一致しない場合がある）
    CHECK(hasNonZeroPixels(dstImg.view()));
}

// =============================================================================
// Tiled Pipeline Tests
// =============================================================================

TEST_CASE("Pipeline: tiled rendering produces same result") {
    ImageBuffer srcImg = createGradientImage(128, 128);
    ImageBuffer dstImg1(128, 128, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dstImg2(128, 128, PixelFormatIDs::RGBA8_Straight);

    // タイルなし
    {
        SourceNode src;
        src.setSource(srcImg.view());
        RendererNode renderer;
        renderer.setVirtualScreen(128, 128, 0, 0);
        SinkNode sink;
        sink.setTarget(dstImg1.view());
        src >> renderer >> sink;
        renderer.exec();
    }

    // 32x32タイル
    {
        SourceNode src;
        src.setSource(srcImg.view());
        RendererNode renderer;
        renderer.setVirtualScreen(128, 128, 0, 0);
        renderer.setTileConfig(32, 32);
        SinkNode sink;
        sink.setTarget(dstImg2.view());
        src >> renderer >> sink;
        renderer.exec();
    }

    CHECK(comparePixels(dstImg1.view(), dstImg2.view()));
}

TEST_CASE("Pipeline: various tile sizes") {
    ImageBuffer srcImg = createGradientImage(100, 100);
    ImageBuffer dstImgBase(100, 100, PixelFormatIDs::RGBA8_Straight);

    // ベース（タイルなし）
    {
        SourceNode src;
        src.setSource(srcImg.view());
        RendererNode renderer;
        renderer.setVirtualScreen(100, 100, 0, 0);
        SinkNode sink;
        sink.setTarget(dstImgBase.view());
        src >> renderer >> sink;
        renderer.exec();
    }

    SUBCASE("16x16 tiles") {
        ImageBuffer dstImg(100, 100, PixelFormatIDs::RGBA8_Straight);
        SourceNode src;
        src.setSource(srcImg.view());
        RendererNode renderer;
        renderer.setVirtualScreen(100, 100, 0, 0);
        renderer.setTileConfig(16, 16);
        SinkNode sink;
        sink.setTarget(dstImg.view());
        src >> renderer >> sink;
        renderer.exec();
        CHECK(comparePixels(dstImgBase.view(), dstImg.view()));
    }

    SUBCASE("64x64 tiles") {
        ImageBuffer dstImg(100, 100, PixelFormatIDs::RGBA8_Straight);
        SourceNode src;
        src.setSource(srcImg.view());
        RendererNode renderer;
        renderer.setVirtualScreen(100, 100, 0, 0);
        renderer.setTileConfig(64, 64);
        SinkNode sink;
        sink.setTarget(dstImg.view());
        src >> renderer >> sink;
        renderer.exec();
        CHECK(comparePixels(dstImgBase.view(), dstImg.view()));
    }
}

// =============================================================================
// Filter Pipeline Tests
// =============================================================================

TEST_CASE("Pipeline: source -> grayscale -> sink") {
    const int imgSize = 64;
    ImageBuffer srcImg = createGradientImage(imgSize, imgSize);
    ImageBuffer dstImg(imgSize, imgSize, PixelFormatIDs::RGBA8_Straight);

    SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    GrayscaleNode grayscale;
    RendererNode renderer;
    SinkNode sink(dstImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));

    src >> grayscale >> renderer >> sink;
    renderer.setVirtualScreen(imgSize, imgSize);
    renderer.exec();

    // 出力があることを確認
    CHECK(hasNonZeroPixels(dstImg.view()));
}

TEST_CASE("Pipeline: source -> affine -> sink") {
    const int imgSize = 64;
    ImageBuffer srcImg = createGradientImage(imgSize, imgSize);
    ImageBuffer dstImg(imgSize, imgSize, PixelFormatIDs::RGBA8_Straight);

    SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    AffineNode affine;
    affine.setRotation(0.0f);  // 回転なし
    RendererNode renderer;
    SinkNode sink(dstImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));

    src >> affine >> renderer >> sink;
    renderer.setVirtualScreen(imgSize, imgSize);
    renderer.exec();

    // 出力があることを確認
    CHECK(hasNonZeroPixels(dstImg.view()));
}

TEST_CASE("Pipeline: affine rotation 30 degrees") {
    // 30度回転テスト
    const int imgSize = 64;
    const int canvasSize = 100;
    ImageBuffer srcImg = createGradientImage(imgSize, imgSize);
    ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);

    SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    AffineNode affine;
    affine.setRotation(static_cast<float>(M_PI / 6.0));  // 30度
    RendererNode renderer;
    SinkNode sink(dstImg.view(), float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

    src >> affine >> renderer >> sink;
    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // 出力があることを確認
    CHECK(hasNonZeroPixels(dstImg.view()));
}

// =============================================================================
// Composite Pipeline Tests
// =============================================================================

TEST_CASE("Pipeline: composite two sources") {
    const int imgSize = 64;
    ImageBuffer srcImg1 = createGradientImage(imgSize, imgSize);
    ImageBuffer srcImg2 = createGradientImage(imgSize, imgSize);
    ImageBuffer dstImg(imgSize, imgSize, PixelFormatIDs::RGBA8_Straight);

    SourceNode src1(srcImg1.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    SourceNode src2(srcImg2.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    CompositeNode composite(2);
    RendererNode renderer;
    SinkNode sink(dstImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));

    src1 >> composite;
    src2.connectTo(composite, 1);
    composite >> renderer >> sink;

    renderer.setVirtualScreen(imgSize, imgSize);
    renderer.exec();

    CHECK(hasNonZeroPixels(dstImg.view()));
}

// =============================================================================
// Complex Pipeline Tests
// =============================================================================

TEST_CASE("Pipeline: source -> affine -> grayscale -> sink") {
    const int imgSize = 64;
    ImageBuffer srcImg = createGradientImage(imgSize, imgSize);
    ImageBuffer dstImg(imgSize, imgSize, PixelFormatIDs::RGBA8_Straight);

    SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    AffineNode affine;
    affine.setRotation(static_cast<float>(M_PI / 6.0));  // 30度
    GrayscaleNode grayscale;
    RendererNode renderer;
    SinkNode sink(dstImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));

    src >> affine >> grayscale >> renderer >> sink;
    renderer.setVirtualScreen(imgSize, imgSize);
    renderer.exec();

    CHECK(hasNonZeroPixels(dstImg.view()));
}

TEST_CASE("Pipeline: complex with tiled rendering") {
    const int imgSize = 100;
    ImageBuffer srcImg = createGradientImage(imgSize, imgSize);
    ImageBuffer dstImg1(imgSize, imgSize, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dstImg2(imgSize, imgSize, PixelFormatIDs::RGBA8_Straight);

    // タイルなし
    {
        SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
        AffineNode affine;
        affine.setRotation(static_cast<float>(M_PI / 4.0));  // 45度
        GrayscaleNode grayscale;
        RendererNode renderer;
        SinkNode sink(dstImg1.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));

        src >> affine >> grayscale >> renderer >> sink;
        renderer.setVirtualScreen(imgSize, imgSize);
        renderer.exec();
    }

    // 25x25タイル
    {
        SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
        AffineNode affine;
        affine.setRotation(static_cast<float>(M_PI / 4.0));  // 45度
        GrayscaleNode grayscale;
        RendererNode renderer;
        SinkNode sink(dstImg2.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));

        src >> affine >> grayscale >> renderer >> sink;
        renderer.setVirtualScreen(imgSize, imgSize);
        renderer.setTileConfig(25, 25);
        renderer.exec();
    }

    // タイル処理でも結果が一致することを確認（許容誤差あり）
    CHECK(comparePixels(dstImg1.view(), dstImg2.view(), 5));
}
