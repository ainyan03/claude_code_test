// fleximg CompositeNode Unit Tests
// 合成ノードのテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/core/common.h"
#include "fleximg/core/types.h"
#include "fleximg/image/render_types.h"
#include "fleximg/image/image_buffer.h"
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
    SourceNode src(srcView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    CompositeNode composite(1);
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

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
    SourceNode bgSrc(bgView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    SourceNode fgSrc(fgView, float_to_fixed(imgSize / 2.0f), float_to_fixed(imgSize / 2.0f));
    CompositeNode composite(2);
    RendererNode renderer;
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

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
    SinkNode sink(dstView, float_to_fixed(canvasSize / 2.0f), float_to_fixed(canvasSize / 2.0f));

    composite >> renderer >> sink;

    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();

    // エラーなく完了すればOK
    CHECK(true);
}

// =============================================================================
// CompositeNode Non-Overlapping Region Optimization Tests
// =============================================================================

TEST_CASE("CompositeNode non-overlapping regions use convertFormat") {
    // テスト構成: 2つの SourceNode を異なるX位置に配置し、CompositeNode で合成
    // SourceNode の pivot=(0,0), setTranslation で位置制御
    // SinkNode で結果を受け取りピクセル値を検証

    SUBCASE("complete non-overlap (right side, with gap)") {
        // layer1=[0,10), layer2=[20,30) → ギャップ[10,20)が透明
        const int layerW = 10;
        const int canvasW = 30;

        ImageBuffer img1 = createSolidImage(layerW, 1, 255, 0, 0, 255);  // 赤
        ImageBuffer img2 = createSolidImage(layerW, 1, 0, 0, 255, 255);  // 青

        ImageBuffer dstImg(canvasW, 1, PixelFormatIDs::RGBA8_Straight);
        ViewPort dstView = dstImg.view();

        // pivot=(0,0) でソースを作成、setTranslation でX位置指定
        // pivot=(0,0) のとき、ソースの左端が translation の位置に来る
        SourceNode src1(img1.view(), 0, 0);
        src1.setTranslation(0, 0);
        SourceNode src2(img2.view(), 0, 0);
        src2.setTranslation(20, 0);

        CompositeNode composite(2);
        RendererNode renderer;
        SinkNode sink(dstView, 0, 0);

        src1 >> composite;
        src2.connectTo(composite, 1);
        composite >> renderer >> sink;

        renderer.setVirtualScreen(canvasW, 1);
        renderer.exec();

        // [0,10): 赤
        for (int x = 0; x < 10; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, 0, r, g, b, a);
            CHECK(r == 255);
            CHECK(g == 0);
            CHECK(b == 0);
            CHECK(a == 255);
        }

        // [10,20): 透明（ギャップ）
        for (int x = 10; x < 20; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, 0, r, g, b, a);
            CHECK(a == 0);
        }

        // [20,30): 青
        for (int x = 20; x < 30; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, 0, r, g, b, a);
            CHECK(r == 0);
            CHECK(g == 0);
            CHECK(b == 255);
            CHECK(a == 255);
        }
    }

    SUBCASE("complete non-overlap (adjacent, no gap)") {
        // layer1=[0,10), layer2=[10,20) → ギャップなし
        const int layerW = 10;
        const int canvasW = 20;

        ImageBuffer img1 = createSolidImage(layerW, 1, 255, 0, 0, 255);  // 赤
        ImageBuffer img2 = createSolidImage(layerW, 1, 0, 255, 0, 255);  // 緑

        ImageBuffer dstImg(canvasW, 1, PixelFormatIDs::RGBA8_Straight);
        ViewPort dstView = dstImg.view();

        SourceNode src1(img1.view(), 0, 0);
        src1.setTranslation(0, 0);
        SourceNode src2(img2.view(), 0, 0);
        src2.setTranslation(10, 0);

        CompositeNode composite(2);
        RendererNode renderer;
        SinkNode sink(dstView, 0, 0);

        src1 >> composite;
        src2.connectTo(composite, 1);
        composite >> renderer >> sink;

        renderer.setVirtualScreen(canvasW, 1);
        renderer.exec();

        // [0,10): 赤
        for (int x = 0; x < 10; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, 0, r, g, b, a);
            CHECK(r == 255);
            CHECK(g == 0);
            CHECK(b == 0);
            CHECK(a == 255);
        }

        // [10,20): 緑
        for (int x = 10; x < 20; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, 0, r, g, b, a);
            CHECK(r == 0);
            CHECK(g == 255);
            CHECK(b == 0);
            CHECK(a == 255);
        }
    }

    SUBCASE("partial overlap") {
        // layer1=[0,20), layer2=[10,30) → 重複[10,20), 非重複[20,30)はconvertFormat
        const int layerW = 20;
        const int canvasW = 30;

        ImageBuffer img1 = createSolidImage(layerW, 1, 255, 0, 0, 255);  // 赤（不透明）
        ImageBuffer img2 = createSolidImage(layerW, 1, 0, 0, 255, 255);  // 青（不透明）

        ImageBuffer dstImg(canvasW, 1, PixelFormatIDs::RGBA8_Straight);
        ViewPort dstView = dstImg.view();

        SourceNode src1(img1.view(), 0, 0);
        src1.setTranslation(0, 0);
        SourceNode src2(img2.view(), 0, 0);
        src2.setTranslation(10, 0);

        CompositeNode composite(2);
        RendererNode renderer;
        SinkNode sink(dstView, 0, 0);

        src1 >> composite;
        src2.connectTo(composite, 1);
        composite >> renderer >> sink;

        renderer.setVirtualScreen(canvasW, 1);
        renderer.exec();

        // [0,10): 赤のみ（layer1のみ）
        for (int x = 0; x < 10; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, 0, r, g, b, a);
            CHECK(r == 255);
            CHECK(g == 0);
            CHECK(b == 0);
            CHECK(a == 255);
        }

        // [10,20): 重複領域（under合成: layer1が前面、layer2が背面）
        // layer1が不透明なのでlayer2は見えない → 赤のまま
        for (int x = 10; x < 20; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, 0, r, g, b, a);
            CHECK(r == 255);
            CHECK(g == 0);
            CHECK(b == 0);
            CHECK(a == 255);
        }

        // [20,30): 非重複（convertFormatで直接書き込み）→ 青
        for (int x = 20; x < 30; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, 0, r, g, b, a);
            CHECK(r == 0);
            CHECK(g == 0);
            CHECK(b == 255);
            CHECK(a == 255);
        }
    }

    SUBCASE("full containment") {
        // layer1=[0,30), layer2=[5,25) → 全範囲blend（既存動作と同等）
        const int canvasW = 30;

        ImageBuffer img1 = createSolidImage(canvasW, 1, 255, 0, 0, 255);  // 赤（不透明）
        ImageBuffer img2 = createSolidImage(20, 1, 0, 255, 0, 128);       // 緑（半透明）

        ImageBuffer dstImg(canvasW, 1, PixelFormatIDs::RGBA8_Straight);
        ViewPort dstView = dstImg.view();

        SourceNode src1(img1.view(), 0, 0);
        src1.setTranslation(0, 0);
        SourceNode src2(img2.view(), 0, 0);
        src2.setTranslation(5, 0);

        CompositeNode composite(2);
        RendererNode renderer;
        SinkNode sink(dstView, 0, 0);

        src1 >> composite;
        src2.connectTo(composite, 1);
        composite >> renderer >> sink;

        renderer.setVirtualScreen(canvasW, 1);
        renderer.exec();

        // [0,5): 赤のみ（layer1のみ、layer2範囲外）
        for (int x = 0; x < 5; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, 0, r, g, b, a);
            CHECK(r == 255);
            CHECK(g == 0);
            CHECK(b == 0);
            CHECK(a == 255);
        }

        // [5,25): under合成（layer1前面＝赤不透明、layer2背面＝緑半透明）
        // layer1が不透明なのでlayer2は見えない → 赤のまま
        for (int x = 5; x < 25; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, 0, r, g, b, a);
            CHECK(r == 255);
            CHECK(g == 0);
            CHECK(b == 0);
            CHECK(a == 255);
        }

        // [25,30): 赤のみ（layer1のみ、layer2範囲外）
        for (int x = 25; x < 30; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, 0, r, g, b, a);
            CHECK(r == 255);
            CHECK(g == 0);
            CHECK(b == 0);
            CHECK(a == 255);
        }
    }

    SUBCASE("partial overlap with semi-transparent foreground") {
        // layer1=[0,20) 半透明赤, layer2=[10,30) 不透明青
        // 重複[10,20): under合成（半透明赤の後ろに青）
        // 非重複[20,30): convertFormatで青
        const int layerW = 20;
        const int canvasW = 30;

        ImageBuffer img1 = createSolidImage(layerW, 1, 255, 0, 0, 128);  // 赤（半透明）
        ImageBuffer img2 = createSolidImage(layerW, 1, 0, 0, 255, 255);  // 青（不透明）

        ImageBuffer dstImg(canvasW, 1, PixelFormatIDs::RGBA8_Straight);
        ViewPort dstView = dstImg.view();

        SourceNode src1(img1.view(), 0, 0);
        src1.setTranslation(0, 0);
        SourceNode src2(img2.view(), 0, 0);
        src2.setTranslation(10, 0);

        CompositeNode composite(2);
        RendererNode renderer;
        SinkNode sink(dstView, 0, 0);

        src1 >> composite;
        src2.connectTo(composite, 1);
        composite >> renderer >> sink;

        renderer.setVirtualScreen(canvasW, 1);
        renderer.exec();

        // [0,10): 赤半透明のみ
        for (int x = 0; x < 10; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, 0, r, g, b, a);
            CHECK(r == 255);
            CHECK(g == 0);
            CHECK(b == 0);
            CHECK(a == 128);
        }

        // [10,20): under合成（半透明赤 + 背面不透明青）
        // 結果: 赤と青が混ざり、不透明になる
        for (int x = 10; x < 20; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, 0, r, g, b, a);
            CHECK(r > 50);   // 赤成分あり
            CHECK(b > 50);   // 青成分あり
            CHECK(a == 255); // 不透明（背面が不透明なので）
        }

        // [20,30): 非重複（convertFormat）→ 青不透明
        for (int x = 20; x < 30; x++) {
            uint8_t r, g, b, a;
            getPixelRGBA8(dstView, x, 0, r, g, b, a);
            CHECK(r == 0);
            CHECK(g == 0);
            CHECK(b == 255);
            CHECK(a == 255);
        }
    }
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
