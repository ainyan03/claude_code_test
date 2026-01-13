// fleximg AffineNode Unit Tests
// アフィン変換ノードのテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/core/common.h"
#include "fleximg/core/types.h"
#include "fleximg/image/render_types.h"
#include "fleximg/image/image_buffer.h"
#include "fleximg/operations/transform.h"
#include "fleximg/nodes/affine_node.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/sink_node.h"
#include "fleximg/nodes/renderer_node.h"

#include <cmath>
#include <algorithm>

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

// DDA シミュレーションで実際にアクセスされる範囲を計算
struct ActualAccessRange {
    int minX = INT32_MAX;
    int maxX = INT32_MIN;
    int minY = INT32_MAX;
    int maxY = INT32_MIN;
    bool hasAccess = false;

    void update(int x, int y) {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
        hasAccess = true;
    }
};

// DDA シミュレーション（applyAffine と同じロジック）
static ActualAccessRange simulateDDA(
    const RenderRequest& request,
    const Matrix2x2_fixed16& invMatrix,
    int_fixed8 txFixed8,
    int_fixed8 tyFixed8,
    int srcWidth,
    int srcHeight,
    int_fixed8 srcOriginX,
    int_fixed8 srcOriginY
) {
    ActualAccessRange range;

    const int outW = request.width;
    const int outH = request.height;

    const int32_t fixedInvA = invMatrix.a;
    const int32_t fixedInvB = invMatrix.b;
    const int32_t fixedInvC = invMatrix.c;
    const int32_t fixedInvD = invMatrix.d;

    const int32_t dstOriginXInt = from_fixed8(request.origin.x);
    const int32_t dstOriginYInt = from_fixed8(request.origin.y);
    const int32_t srcOriginXInt = from_fixed8(srcOriginX);
    const int32_t srcOriginYInt = from_fixed8(srcOriginY);

    int64_t invTx64 = -(static_cast<int64_t>(txFixed8) * fixedInvA
                      + static_cast<int64_t>(tyFixed8) * fixedInvB);
    int64_t invTy64 = -(static_cast<int64_t>(txFixed8) * fixedInvC
                      + static_cast<int64_t>(tyFixed8) * fixedInvD);
    int32_t invTxFixed = static_cast<int32_t>(invTx64 >> INT_FIXED8_SHIFT);
    int32_t invTyFixed = static_cast<int32_t>(invTy64 >> INT_FIXED8_SHIFT);

    const int32_t fixedInvTx = invTxFixed
                        - (dstOriginXInt * fixedInvA)
                        - (dstOriginYInt * fixedInvB)
                        + (srcOriginXInt << INT_FIXED16_SHIFT);
    const int32_t fixedInvTy = invTyFixed
                        - (dstOriginXInt * fixedInvC)
                        - (dstOriginYInt * fixedInvD)
                        + (srcOriginYInt << INT_FIXED16_SHIFT);

    const int32_t rowOffsetX = fixedInvB >> 1;
    const int32_t rowOffsetY = fixedInvD >> 1;
    const int32_t dxOffsetX = fixedInvA >> 1;
    const int32_t dxOffsetY = fixedInvC >> 1;

    for (int dy = 0; dy < outH; dy++) {
        int32_t rowBaseX = fixedInvB * dy + fixedInvTx + rowOffsetX;
        int32_t rowBaseY = fixedInvD * dy + fixedInvTy + rowOffsetY;

        auto [xStart, xEnd] = transform::calcValidRange(fixedInvA, rowBaseX, srcWidth, outW);
        auto [yStart, yEnd] = transform::calcValidRange(fixedInvC, rowBaseY, srcHeight, outW);
        int dxStart = std::max({0, xStart, yStart});
        int dxEnd = std::min({outW - 1, xEnd, yEnd});

        if (dxStart > dxEnd) continue;

        for (int dx = dxStart; dx <= dxEnd; dx++) {
            int32_t srcX_fixed = fixedInvA * dx + rowBaseX + dxOffsetX;
            int32_t srcY_fixed = fixedInvC * dx + rowBaseY + dxOffsetY;

            int srcX = static_cast<uint32_t>(srcX_fixed) >> INT_FIXED16_SHIFT;
            int srcY = static_cast<uint32_t>(srcY_fixed) >> INT_FIXED16_SHIFT;

            range.update(srcX, srcY);
        }
    }

    return range;
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
// AffineNode prepare() Tests
// =============================================================================

TEST_CASE("AffineNode prepare computes inverse matrix") {
    AffineNode node;

    SUBCASE("identity matrix") {
        RenderRequest req;
        req.width = 64;
        req.height = 64;
        req.origin = Point(to_fixed8(32), to_fixed8(32));
        node.prepare(req);

        const auto& inv = node.getInvMatrix();
        CHECK(inv.valid);
    }

    SUBCASE("rotation + scale") {
        float angle = static_cast<float>(M_PI / 4.0);  // 45度
        float scale = 2.0f;
        float c = std::cos(angle) * scale;
        float s = std::sin(angle) * scale;

        AffineMatrix m;
        m.a = c;  m.b = -s;
        m.c = s;  m.d = c;
        node.setMatrix(m);

        RenderRequest req;
        req.width = 64;
        req.height = 64;
        req.origin = Point(to_fixed8(32), to_fixed8(32));
        node.prepare(req);

        const auto& inv = node.getInvMatrix();
        CHECK(inv.valid);
    }
}

// =============================================================================
// AffineNode computeInputRegion Tests (Margin Validation)
// =============================================================================

TEST_CASE("AffineNode computeInputRegion margin validation") {
    // AABB が実際の DDA アクセス範囲をカバーしていることを確認

    auto testMargin = [](float angleDeg, float scale, float tx, float ty,
                         int outWidth, int outHeight) -> bool {
        AffineNode node;
        float rad = angleDeg * static_cast<float>(M_PI) / 180.0f;
        float c = std::cos(rad) * scale;
        float s = std::sin(rad) * scale;
        AffineMatrix matrix;
        matrix.a = c;   matrix.b = -s;
        matrix.c = s;   matrix.d = c;
        matrix.tx = tx; matrix.ty = ty;
        node.setMatrix(matrix);

        RenderRequest screenInfo;
        screenInfo.width = static_cast<int16_t>(outWidth);
        screenInfo.height = static_cast<int16_t>(outHeight);
        screenInfo.origin = Point(to_fixed8(outWidth / 2), to_fixed8(outHeight / 2));
        node.prepare(screenInfo);

        RenderRequest request;
        request.width = static_cast<int16_t>(outWidth);
        request.height = static_cast<int16_t>(outHeight);
        request.origin = Point(to_fixed8(outWidth / 2), to_fixed8(outHeight / 2));

        auto region = node.testComputeInputRegion(request);

        int inputWidth = region.aabbRight - region.aabbLeft + 1;
        int inputHeight = region.aabbBottom - region.aabbTop + 1;
        int_fixed8 srcOriginX = to_fixed8(-region.aabbLeft);
        int_fixed8 srcOriginY = to_fixed8(-region.aabbTop);

        ActualAccessRange actual = simulateDDA(
            request,
            node.getInvMatrix(),
            node.getTxFixed8(),
            node.getTyFixed8(),
            inputWidth,
            inputHeight,
            srcOriginX,
            srcOriginY
        );

        if (!actual.hasAccess) return true;

        int actualMinX = actual.minX + region.aabbLeft;
        int actualMaxX = actual.maxX + region.aabbLeft;
        int actualMinY = actual.minY + region.aabbTop;
        int actualMaxY = actual.maxY + region.aabbTop;

        return (region.aabbLeft <= actualMinX &&
                region.aabbRight >= actualMaxX &&
                region.aabbTop <= actualMinY &&
                region.aabbBottom >= actualMaxY);
    };

    SUBCASE("identity transform") {
        CHECK(testMargin(0.0f, 1.0f, 0, 0, 64, 64));
    }

    SUBCASE("45 degree rotation") {
        CHECK(testMargin(45.0f, 1.0f, 0, 0, 64, 64));
    }

    SUBCASE("90 degree rotation") {
        CHECK(testMargin(90.0f, 1.0f, 0, 0, 64, 64));
    }

    SUBCASE("30 degree with translation") {
        CHECK(testMargin(30.0f, 1.0f, 0.5f, 0.5f, 32, 32));
    }

    SUBCASE("scale 2x with rotation") {
        CHECK(testMargin(60.0f, 2.0f, 0, 0, 64, 64));
    }

    SUBCASE("149.8 degree scale 3x (known issue condition)") {
        CHECK(testMargin(149.8f, 3.0f, 0, 0, 64, 64));
    }
}

// =============================================================================
// AffineNode Pull Mode Tests
// =============================================================================

TEST_CASE("AffineNode pull mode translation only") {
    const int imgW = 32, imgH = 32;
    const int canvasW = 100, canvasH = 100;

    ImageBuffer srcImg = createTestImage(imgW, imgH);
    ViewPort srcView = srcImg.view();

    ImageBuffer dstImg(canvasW, canvasH, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    SourceNode src(srcView, imgW / 2.0f, imgH / 2.0f);
    AffineNode affine;
    RendererNode renderer;
    SinkNode sink(dstView, canvasW / 2.0f, canvasH / 2.0f);

    src >> affine >> renderer >> sink;

    float tx = 10.3f, ty = 5.7f;
    affine.setTranslation(tx, ty);

    renderer.setVirtualScreen(canvasW, canvasH);
    renderer.exec();

    PixelPos pos = findRedCenter(dstView);
    CHECK(pos.found);
    // 位置がキャンバス内に収まっていることを確認
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

    ImageBuffer dstImg(canvasW, canvasH, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    SourceNode src(srcView, imgW / 2.0f, imgH / 2.0f);
    AffineNode affine;
    RendererNode renderer;
    SinkNode sink(dstView, canvasW / 2.0f, canvasH / 2.0f);

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
    // 位置がキャンバス内に収まっていることを確認
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

    ImageBuffer dstImg(canvasW, canvasH, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dstImg.view();

    SourceNode src(srcView, imgW / 2.0f, imgH / 2.0f);
    AffineNode affine;
    RendererNode renderer;
    SinkNode sink(dstView, canvasW / 2.0f, canvasH / 2.0f);

    src >> affine >> renderer >> sink;

    float tx = 7.7f, ty = 3.3f;
    affine.setTranslation(tx, ty);

    renderer.setVirtualScreen(canvasW, canvasH);
    renderer.setTileConfig(16, 16);
    renderer.exec();

    PixelPos pos = findRedCenter(dstView);
    CHECK(pos.found);
    // 位置がキャンバス内に収まっていることを確認
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

    SourceNode src(srcView, imgW / 2.0f, imgH / 2.0f);
    AffineNode affine;
    RendererNode renderer;

    src >> affine >> renderer;
    renderer.setVirtualScreen(canvasW, canvasH);

    int lastX = -1;
    int backwardJumps = 0;

    // tx を 0.0 から 10.0 まで 0.5 刻みで変化させる
    for (int i = 0; i <= 20; i++) {
        float tx = i * 0.5f;

        ImageBuffer dstImg(canvasW, canvasH, PixelFormatIDs::RGBA8_Straight);
        ViewPort dstView = dstImg.view();
        SinkNode sink(dstView, canvasW / 2.0f, canvasH / 2.0f);

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
