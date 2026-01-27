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

    SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    AffineNode affine;
    RendererNode renderer;
    SinkNode sink(dstImg.view(), float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

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

    SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    AffineNode affine;
    float angle = 45.0f * static_cast<float>(M_PI) / 180.0f;
    affine.setRotation(angle);
    RendererNode renderer;
    SinkNode sink(dstImg.view(), float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

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

    SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    AffineNode affine;
    affine.setScale(2.0f, 2.0f);
    RendererNode renderer;
    SinkNode sink(dstImg.view(), float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

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
        SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
        AffineNode affine;
        float angle = 60.0f * static_cast<float>(M_PI) / 180.0f;
        affine.setRotation(angle);
        affine.setScale(1.5f, 1.5f);
        RendererNode renderer;
        SinkNode sink(dstImg1.view(), float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

        src >> affine >> renderer >> sink;
        renderer.setVirtualScreen(canvasSize, canvasSize);
        renderer.exec();
    }

    // 25x25タイル
    ImageBuffer dstImg2(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
    {
        SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
        AffineNode affine;
        float angle = 60.0f * static_cast<float>(M_PI) / 180.0f;
        affine.setRotation(angle);
        affine.setScale(1.5f, 1.5f);
        RendererNode renderer;
        SinkNode sink(dstImg2.view(), float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

        src >> affine >> renderer >> sink;
        renderer.setVirtualScreen(canvasSize, canvasSize);
        renderer.setTileConfig(25, 25);
        renderer.exec();
    }

    // 両方に出力があることを確認
    CHECK(hasNonZeroPixels(dstImg1.view()));
    CHECK(hasNonZeroPixels(dstImg2.view()));
}

// =============================================================================
// Non-affine / Affine Path Consistency Tests（ピクセル中心モデル検証）
// =============================================================================

// 非アフィンパスとアフィンパス(identity)の出力が一致することを検証
// 小数pivotを使用して、ピクセル中心モデルの正しさを確認
TEST_CASE("Scanline: non-affine vs affine identity consistency with fractional pivot") {
    const int imgSize = 16;
    const int canvasSize = 32;

    // ソース画像: 各ピクセルに異なる値を設定（一致判定を厳密に）
    ImageBuffer srcImg(imgSize, imgSize, PixelFormatIDs::RGBA8_Straight);
    for (int y = 0; y < imgSize; y++) {
        uint8_t* row = static_cast<uint8_t*>(srcImg.pixelAt(0, y));
        for (int x = 0; x < imgSize; x++) {
            row[x * 4 + 0] = static_cast<uint8_t>((x * 17 + y * 31) & 0xFF);
            row[x * 4 + 1] = static_cast<uint8_t>((x * 53 + y * 7) & 0xFF);
            row[x * 4 + 2] = static_cast<uint8_t>((x * 11 + y * 43) & 0xFF);
            row[x * 4 + 3] = 255;
        }
    }

    // 小数pivot (2.5, 2.5) を使用
    int_fixed pivotX = float_to_fixed(2.5f);
    int_fixed pivotY = float_to_fixed(2.5f);

    // パターン1: 非アフィンパス（SourceNode直接、AffineNodeなし）
    ImageBuffer dstDirect(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);
    {
        SourceNode src(srcImg.view(), pivotX, pivotY);
        RendererNode renderer;
        SinkNode sink(dstDirect.view(),
                      float_to_fixed(canvasSize / 2.0f),
                      float_to_fixed(canvasSize / 2.0f));

        src >> renderer >> sink;
        renderer.setVirtualScreen(canvasSize, canvasSize);
        renderer.exec();
    }

    // パターン2: アフィンパス（identity AffineNode経由）
    ImageBuffer dstAffine(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);
    {
        SourceNode src(srcImg.view(), pivotX, pivotY);
        AffineNode affine;  // identity変換
        RendererNode renderer;
        SinkNode sink(dstAffine.view(),
                      float_to_fixed(canvasSize / 2.0f),
                      float_to_fixed(canvasSize / 2.0f));

        src >> affine >> renderer >> sink;
        renderer.setVirtualScreen(canvasSize, canvasSize);
        renderer.exec();
    }

    // 両パスとも出力があること
    CHECK(hasNonZeroPixels(dstDirect.view()));
    CHECK(hasNonZeroPixels(dstAffine.view()));

    // 全ピクセル一致を検証
    int mismatchCount = 0;
    for (int y = 0; y < canvasSize; y++) {
        const uint8_t* rowDirect = static_cast<const uint8_t*>(dstDirect.view().pixelAt(0, y));
        const uint8_t* rowAffine = static_cast<const uint8_t*>(dstAffine.view().pixelAt(0, y));
        for (int x = 0; x < canvasSize; x++) {
            for (int c = 0; c < 4; c++) {
                if (rowDirect[x * 4 + c] != rowAffine[x * 4 + c]) {
                    mismatchCount++;
                }
            }
        }
    }
    CHECK(mismatchCount == 0);
}

// 小数position（平行移動）でも非アフィン/アフィンパスが一致することを検証
TEST_CASE("Scanline: non-affine vs affine with fractional position") {
    const int imgSize = 12;
    const int canvasSize = 32;

    ImageBuffer srcImg(imgSize, imgSize, PixelFormatIDs::RGBA8_Straight);
    for (int y = 0; y < imgSize; y++) {
        uint8_t* row = static_cast<uint8_t*>(srcImg.pixelAt(0, y));
        for (int x = 0; x < imgSize; x++) {
            row[x * 4 + 0] = static_cast<uint8_t>((x * 23 + y * 37) & 0xFF);
            row[x * 4 + 1] = static_cast<uint8_t>((x * 41 + y * 13) & 0xFF);
            row[x * 4 + 2] = static_cast<uint8_t>((x * 59 + y * 19) & 0xFF);
            row[x * 4 + 3] = 255;
        }
    }

    // 小数pivotと小数positionの組み合わせ
    float pivotX = 3.25f;
    float pivotY = 3.75f;
    float posX = 5.5f;
    float posY = 7.25f;

    // パターン1: 非アフィンパス
    ImageBuffer dstDirect(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);
    {
        SourceNode src(srcImg.view(), float_to_fixed(pivotX), float_to_fixed(pivotY));
        src.setPosition(posX, posY);
        RendererNode renderer;
        SinkNode sink(dstDirect.view(),
                      float_to_fixed(canvasSize / 2.0f),
                      float_to_fixed(canvasSize / 2.0f));

        src >> renderer >> sink;
        renderer.setVirtualScreen(canvasSize, canvasSize);
        renderer.exec();
    }

    // パターン2: アフィンパス（identity AffineNode経由）
    ImageBuffer dstAffine(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);
    {
        SourceNode src(srcImg.view(), float_to_fixed(pivotX), float_to_fixed(pivotY));
        src.setPosition(posX, posY);
        AffineNode affine;  // identity変換
        RendererNode renderer;
        SinkNode sink(dstAffine.view(),
                      float_to_fixed(canvasSize / 2.0f),
                      float_to_fixed(canvasSize / 2.0f));

        src >> affine >> renderer >> sink;
        renderer.setVirtualScreen(canvasSize, canvasSize);
        renderer.exec();
    }

    CHECK(hasNonZeroPixels(dstDirect.view()));
    CHECK(hasNonZeroPixels(dstAffine.view()));

    int mismatchCount = 0;
    for (int y = 0; y < canvasSize; y++) {
        const uint8_t* rowDirect = static_cast<const uint8_t*>(dstDirect.view().pixelAt(0, y));
        const uint8_t* rowAffine = static_cast<const uint8_t*>(dstAffine.view().pixelAt(0, y));
        for (int x = 0; x < canvasSize; x++) {
            for (int c = 0; c < 4; c++) {
                if (rowDirect[x * 4 + c] != rowAffine[x * 4 + c]) {
                    mismatchCount++;
                }
            }
        }
    }
    CHECK(mismatchCount == 0);
}

// position を段階的に変化させた際のピクセル位置一貫性テスト
TEST_CASE("Scanline: position smoothness consistency") {
    const int imgSize = 8;
    const int canvasSize = 24;

    ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 255, 128, 64);

    int_fixed pivotX = float_to_fixed(imgSize / 2.0f);
    int_fixed pivotY = float_to_fixed(imgSize / 2.0f);

    // position を 0.25 刻みで変化させ、各ステップで非アフィン/アフィン一致を確認
    for (int step = 0; step < 8; step++) {
        float posX = step * 0.25f;
        float posY = step * 0.25f;

        ImageBuffer dstDirect(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);
        {
            SourceNode src(srcImg.view(), pivotX, pivotY);
            src.setPosition(posX, posY);
            RendererNode renderer;
            SinkNode sink(dstDirect.view(),
                          float_to_fixed(canvasSize / 2.0f),
                          float_to_fixed(canvasSize / 2.0f));

            src >> renderer >> sink;
            renderer.setVirtualScreen(canvasSize, canvasSize);
            renderer.exec();
        }

        ImageBuffer dstAffine(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);
        {
            SourceNode src(srcImg.view(), pivotX, pivotY);
            src.setPosition(posX, posY);
            AffineNode affine;
            RendererNode renderer;
            SinkNode sink(dstAffine.view(),
                          float_to_fixed(canvasSize / 2.0f),
                          float_to_fixed(canvasSize / 2.0f));

            src >> affine >> renderer >> sink;
            renderer.setVirtualScreen(canvasSize, canvasSize);
            renderer.exec();
        }

        int mismatchCount = 0;
        for (int y = 0; y < canvasSize; y++) {
            const uint8_t* rowDirect = static_cast<const uint8_t*>(dstDirect.view().pixelAt(0, y));
            const uint8_t* rowAffine = static_cast<const uint8_t*>(dstAffine.view().pixelAt(0, y));
            for (int x = 0; x < canvasSize; x++) {
                for (int c = 0; c < 4; c++) {
                    if (rowDirect[x * 4 + c] != rowAffine[x * 4 + c]) {
                        mismatchCount++;
                    }
                }
            }
        }
        CAPTURE(step);
        CAPTURE(posX);
        CAPTURE(posY);
        CHECK(mismatchCount == 0);
    }
}
