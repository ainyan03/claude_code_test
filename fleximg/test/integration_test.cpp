// fleximg 統合テスト
// コンパイル: g++ -std=c++17 -I../src integration_test.cpp ../src/fleximg/*.cpp ../src/fleximg/operations/*.cpp -o integration_test

#include <iostream>
#include <cmath>
#include <cstring>
#include "fleximg/common.h"
#include "fleximg/viewport.h"
#include "fleximg/image_buffer.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/sink_node.h"
#include "fleximg/nodes/transform_node.h"
#include "fleximg/nodes/grayscale_node.h"
#include "fleximg/nodes/composite_node.h"
#include "fleximg/nodes/renderer_node.h"

using namespace fleximg;

// テスト用画像を作成（赤いグラデーション）
ImageBuffer createTestImage(int width, int height) {
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
bool comparePixels(const ViewPort& a, const ViewPort& b, int tolerance = 0) {
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

// テスト結果表示
int testsPassed = 0;
int testsFailed = 0;

void reportTest(const char* name, bool passed) {
    if (passed) {
        std::cout << "[PASS] " << name << std::endl;
        testsPassed++;
    } else {
        std::cout << "[FAIL] " << name << std::endl;
        testsFailed++;
    }
}

// ========================================
// テスト1: 基本パイプライン (src >> renderer >> sink)
// ========================================
void testBasicPipeline() {
    ImageBuffer srcImg = createTestImage(64, 64);
    ImageBuffer dstImg(64, 64, PixelFormatIDs::RGBA8_Straight);

    SourceNode src;
    src.setSource(srcImg.view());
    src.setOrigin(0, 0);

    RendererNode renderer;
    renderer.setVirtualScreen(64, 64, 0, 0);

    SinkNode sink;
    sink.setTarget(dstImg.view());
    sink.setOrigin(0, 0);

    src >> renderer >> sink;
    renderer.exec();

    bool passed = comparePixels(srcImg.view(), dstImg.view());
    reportTest("Basic pipeline (src >> sink)", passed);
}

// ========================================
// テスト2: タイル分割パイプライン
// ========================================
void testTiledPipeline() {
    ImageBuffer srcImg = createTestImage(128, 128);
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

    bool passed = comparePixels(dstImg1.view(), dstImg2.view());
    reportTest("Tiled pipeline (32x32 tiles)", passed);
}

// ========================================
// テスト3: アフィン変換（回転）
// ========================================
void testAffineTransform() {
    ImageBuffer srcImg = createTestImage(64, 64);
    ImageBuffer dstImg(64, 64, PixelFormatIDs::RGBA8_Straight);

    SourceNode src;
    src.setSource(srcImg.view());
    src.setOrigin(32, 32);  // 中央基準

    TransformNode transform;
    transform.setRotation(0.0f);  // 回転なし（パススルー）

    RendererNode renderer;
    renderer.setVirtualScreen(64, 64, 32, 32);

    SinkNode sink;
    sink.setTarget(dstImg.view());
    sink.setOrigin(32, 32);

    src >> transform >> renderer >> sink;
    renderer.exec();

    // 回転0なのでほぼ同じはず（浮動小数点誤差許容）
    bool passed = comparePixels(srcImg.view(), dstImg.view(), 2);
    reportTest("Affine transform (identity rotation)", passed);
}

// ========================================
// テスト4: フィルタ（グレースケール）
// ========================================
void testGrayscaleFilter() {
    ImageBuffer srcImg = createTestImage(32, 32);
    ImageBuffer dstImg(32, 32, PixelFormatIDs::RGBA8_Straight);

    SourceNode src;
    src.setSource(srcImg.view());

    GrayscaleNode filter;

    RendererNode renderer;
    renderer.setVirtualScreen(32, 32, 0, 0);

    SinkNode sink;
    sink.setTarget(dstImg.view());

    src >> filter >> renderer >> sink;
    renderer.exec();

    // グレースケール確認: R=G=Bになっているか
    bool passed = true;
    ViewPort result = dstImg.view();
    for (int y = 0; y < result.height && passed; y++) {
        const uint8_t* row = static_cast<const uint8_t*>(result.pixelAt(0, y));
        for (int x = 0; x < result.width && passed; x++) {
            if (row[x*4] != row[x*4+1] || row[x*4+1] != row[x*4+2]) {
                passed = false;
            }
        }
    }
    reportTest("Grayscale filter", passed);
}

// ========================================
// テスト5: 合成ノード
// ========================================
void testComposite() {
    ImageBuffer bg(64, 64, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer fg(64, 64, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dstImg(64, 64, PixelFormatIDs::RGBA8_Straight);

    // 背景: 赤
    for (int y = 0; y < 64; y++) {
        uint8_t* row = static_cast<uint8_t*>(bg.pixelAt(0, y));
        for (int x = 0; x < 64; x++) {
            row[x*4] = 255; row[x*4+1] = 0; row[x*4+2] = 0; row[x*4+3] = 255;
        }
    }

    // 前景: 半透明緑
    for (int y = 0; y < 64; y++) {
        uint8_t* row = static_cast<uint8_t*>(fg.pixelAt(0, y));
        for (int x = 0; x < 64; x++) {
            row[x*4] = 0; row[x*4+1] = 255; row[x*4+2] = 0; row[x*4+3] = 128;
        }
    }

    SourceNode srcBg, srcFg;
    srcBg.setSource(bg.view());
    srcFg.setSource(fg.view());

    CompositeNode composite(2);

    RendererNode renderer;
    renderer.setVirtualScreen(64, 64, 0, 0);

    SinkNode sink;
    sink.setTarget(dstImg.view());

    srcBg >> composite;
    srcFg.connectTo(composite, 1);
    composite >> renderer >> sink;
    renderer.exec();

    // 合成結果が背景でも前景でもないことを確認
    bool isDifferentFromBg = !comparePixels(bg.view(), dstImg.view());
    bool isDifferentFromFg = !comparePixels(fg.view(), dstImg.view());
    bool passed = isDifferentFromBg && isDifferentFromFg;
    reportTest("Composite node", passed);
}

// ========================================
// メイン
// ========================================
int main() {
    std::cout << "=== fleximg Integration Tests ===" << std::endl;
    std::cout << std::endl;

    testBasicPipeline();
    testTiledPipeline();
    testAffineTransform();
    testGrayscaleFilter();
    testComposite();

    std::cout << std::endl;
    std::cout << "=== Results ===" << std::endl;
    std::cout << "Passed: " << testsPassed << std::endl;
    std::cout << "Failed: " << testsFailed << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
