// fleximg M5Stack Basic Example
// 矩形画像の回転デモ

#include <M5Unified.h>

// fleximg
#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/core/common.h"
#include "fleximg/core/types.h"
#include "fleximg/image/image_buffer.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/affine_node.h"
#include "fleximg/nodes/renderer_node.h"

// fleximg implementation files (header-only style inclusion for simplicity)
#include "fleximg/image/pixel_format.cpp"
#include "fleximg/image/viewport.cpp"
#include "fleximg/operations/filters.cpp"
#include "fleximg/core/memory/platform.cpp"
#include "fleximg/core/memory/pool_allocator.cpp"

// カスタムSinkNode
#include "lcd_sink_node.h"

#include <cmath>

using namespace fleximg;

// グラデーション画像を作成
static ImageBuffer createGradientImage(int width, int height) {
    ImageBuffer img(width, height, PixelFormatIDs::RGBA8_Straight);

    for (int y = 0; y < height; ++y) {
        uint8_t* row = static_cast<uint8_t*>(img.pixelAt(0, y));
        for (int x = 0; x < width; ++x) {
            // 赤-緑グラデーション
            row[x * 4 + 0] = static_cast<uint8_t>(x * 255 / width);   // R
            row[x * 4 + 1] = static_cast<uint8_t>(y * 255 / height);  // G
            row[x * 4 + 2] = 128;                                      // B
            row[x * 4 + 3] = 255;                                      // A
        }
    }

    return img;
}

// グローバル変数
static ImageBuffer srcImage;
static float rotationAngle = 0.0f;

static SourceNode source;
static AffineNode affine;
static RendererNode renderer;
static LcdSinkNode lcdSink;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);

    // テスト画像を作成（100x100）
    srcImage = createGradientImage(100, 100);

    M5.Display.setCursor(0, 0);
    M5.Display.println("fleximg M5Stack Demo");
    M5.Display.println("Rotating square...");

    // 画面サイズ取得
    int16_t screenW = static_cast<int16_t>(M5.Display.width());
    int16_t screenH = static_cast<int16_t>(M5.Display.height());

    // 描画領域（画面中央に配置）
    int16_t drawW = 320;
    int16_t drawH = 240;
    int16_t drawX = (screenW - drawW) / 2;
    int16_t drawY = (screenH - drawH) / 2;

    // fleximg パイプライン構築
    source.setSource(srcImage.view());
    source.setOrigin(
        float_to_fixed(srcImage.width() / 2.0f),
        float_to_fixed(srcImage.height() / 2.0f)
    );

    renderer.setVirtualScreen(drawW, drawH);

    lcdSink.setTarget(&M5.Display, drawX, drawY, drawW, drawH);
    lcdSink.setOrigin(
        float_to_fixed(drawW / 2.0f),
        float_to_fixed(drawH / 2.0f)
    );

    // パイプライン接続
    source >> affine >> renderer >> lcdSink;

    M5.Display.startWrite();
}

void loop() {
#if defined ( M5UNIFIED_PC_BUILD )
    // フレームレート制限（約60fps）
    lgfx::delay(16);
#endif
    M5.update();

    // 回転角度を更新
    rotationAngle += 0.05f;
    if (rotationAngle > 2.0f * static_cast<float>(M_PI)) {
        rotationAngle -= 2.0f * static_cast<float>(M_PI);
    }
    affine.setRotation(rotationAngle);

    // レンダリング実行
    renderer.exec();

    // FPS表示
    static unsigned long lastTime = 0;
    static int frameCount = 0;
    static float fps = 0.0f;

    frameCount++;
    unsigned long now = lgfx::millis();
    if (now - lastTime >= 1000) {
        fps = static_cast<float>(frameCount) * 1000.0f / static_cast<float>(now - lastTime);
        frameCount = 0;
        lastTime = now;

        // FPS表示更新
        int16_t dispH = static_cast<int16_t>(M5.Display.height());
        M5.Display.fillRect(0, dispH - 20, 100, 20, TFT_BLACK);
        M5.Display.setCursor(0, dispH - 20);
        M5.Display.printf("FPS: %.1f", static_cast<double>(fps));
    }
}
