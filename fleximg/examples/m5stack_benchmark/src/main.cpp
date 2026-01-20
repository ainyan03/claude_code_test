// fleximg M5Stack Benchmark Example
// パフォーマンス計測サンプル

#include <M5Unified.h>

// fleximg
#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/core/common.h"
#include "fleximg/core/types.h"
#include "fleximg/core/perf_metrics.h"
#include "fleximg/image/image_buffer.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/affine_node.h"
#include "fleximg/nodes/composite_node.h"
#include "fleximg/nodes/matte_node.h"
#include "fleximg/nodes/renderer_node.h"

// fleximg implementation files (header-only style inclusion for simplicity)
#include "fleximg/image/pixel_format.cpp"
#include "fleximg/image/viewport.cpp"
#include "fleximg/operations/blend.cpp"
#include "fleximg/operations/filters.cpp"
#include "fleximg/core/memory/platform.cpp"
#include "fleximg/core/memory/pool_allocator.cpp"

// カスタムSinkNode
#include "lcd_sink_node.h"

#include <cmath>

using namespace fleximg;
using namespace fleximg::core;

// ========================================================================
// テストシナリオ
// ========================================================================
enum class Scenario {
    Source,         // 画像表示のみ（ベースライン）
    Affine,         // アフィン変換
    Composite,      // 2画像合成
    Matte,          // マット合成（3入力）
    Count
};

static const char* scenarioNames[] = {
    "Source",
    "Affine",
    "Composite",
    "Matte"
};

// ========================================================================
// 画像生成ユーティリティ
// ========================================================================

// チェッカーボード画像
static ImageBuffer createCheckerboard(int width, int height, int cellSize = 16) {
    ImageBuffer img(width, height, PixelFormatIDs::RGBA8_Straight, InitPolicy::Uninitialized);

    for (int y = 0; y < height; ++y) {
        uint8_t* row = static_cast<uint8_t*>(img.pixelAt(0, y));
        for (int x = 0; x < width; ++x) {
            bool isEven = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            if (isEven) {
                row[x * 4 + 0] = 255;  // R
                row[x * 4 + 1] = 50;   // G
                row[x * 4 + 2] = 50;   // B
            } else {
                row[x * 4 + 0] = 255;  // R
                row[x * 4 + 1] = 220;  // G
                row[x * 4 + 2] = 50;   // B
            }
            row[x * 4 + 3] = 255;  // A
        }
    }
    return img;
}

// ストライプ画像
static ImageBuffer createStripes(int width, int height, int stripeWidth = 12) {
    ImageBuffer img(width, height, PixelFormatIDs::RGBA8_Straight, InitPolicy::Uninitialized);

    for (int y = 0; y < height; ++y) {
        uint8_t* row = static_cast<uint8_t*>(img.pixelAt(0, y));
        float gradientFactor = static_cast<float>(y) / static_cast<float>(height);

        for (int x = 0; x < width; ++x) {
            bool isStripe = (x / stripeWidth) % 2 == 0;
            if (isStripe) {
                row[x * 4 + 0] = static_cast<uint8_t>(30 + 50 * gradientFactor);
                row[x * 4 + 1] = static_cast<uint8_t>(80 + 100 * gradientFactor);
                row[x * 4 + 2] = static_cast<uint8_t>(180 + 75 * gradientFactor);
            } else {
                row[x * 4 + 0] = static_cast<uint8_t>(30 + 70 * gradientFactor);
                row[x * 4 + 1] = static_cast<uint8_t>(150 + 105 * gradientFactor);
                row[x * 4 + 2] = static_cast<uint8_t>(180 + 75 * gradientFactor);
            }
            row[x * 4 + 3] = 255;
        }
    }
    return img;
}

// 円形マスク
static ImageBuffer createCircleMask(int width, int height) {
    ImageBuffer img(width, height, PixelFormatIDs::Alpha8, InitPolicy::Uninitialized);

    float centerX = static_cast<float>(width) / 2.0f;
    float centerY = static_cast<float>(height) / 2.0f;
    float radius = std::min(centerX, centerY) * 0.9f;

    for (int y = 0; y < height; ++y) {
        uint8_t* row = static_cast<uint8_t*>(img.pixelAt(0, y));
        for (int x = 0; x < width; ++x) {
            float dx = static_cast<float>(x) - centerX;
            float dy = static_cast<float>(y) - centerY;
            float dist = std::sqrt(dx * dx + dy * dy);
            row[x] = (dist < radius) ? 255 : 0;
        }
    }
    return img;
}

// ========================================================================
// グローバル変数
// ========================================================================

// 画像バッファ
static ImageBuffer image1;
static ImageBuffer image2;
static ImageBuffer maskImage;

// ノード
static SourceNode source1;
static SourceNode source2;
static SourceNode maskSource;
static AffineNode affine1;
static AffineNode affine2;
static AffineNode maskAffine;
static CompositeNode composite;
static MatteNode matte;
static RendererNode renderer;
static LcdSinkNode lcdSink;

// 計測状態
static Scenario currentScenario = Scenario::Source;
static float animationTime = 0.0f;
static int frameCount = 0;
static unsigned long lastReportTime = 0;
static const int REPORT_INTERVAL_MS = 2000;  // 2秒ごとにレポート
static const int FRAMES_PER_REPORT = 60;     // または60フレームごと

// 画面サイズ
static int16_t screenW, screenH;
static int16_t drawW, drawH;
static int16_t drawX, drawY;

// ========================================================================
// パイプライン構築
// ========================================================================

static void setupPipeline(Scenario scenario) {
    // 全接続をリセット（ノードの入力ポートをクリア）
    source1 = SourceNode();
    source2 = SourceNode();
    maskSource = SourceNode();
    affine1 = AffineNode();
    affine2 = AffineNode();
    maskAffine = AffineNode();
    composite = CompositeNode(2);
    matte = MatteNode();
    renderer = RendererNode();
    lcdSink = LcdSinkNode();

    // 共通設定
    renderer.setVirtualScreen(drawW, drawH);
    lcdSink.setTarget(&M5.Display, drawX, drawY, drawW, drawH);
    lcdSink.setOrigin(float_to_fixed(drawW / 2.0f), float_to_fixed(drawH / 2.0f));

    // ソース設定
    source1.setSource(image1.view());
    source1.setOrigin(float_to_fixed(image1.width() / 2.0f), float_to_fixed(image1.height() / 2.0f));

    source2.setSource(image2.view());
    source2.setOrigin(float_to_fixed(image2.width() / 2.0f), float_to_fixed(image2.height() / 2.0f));

    maskSource.setSource(maskImage.view());
    maskSource.setOrigin(float_to_fixed(maskImage.width() / 2.0f), float_to_fixed(maskImage.height() / 2.0f));

    // シナリオ別パイプライン構築
    switch (scenario) {
        case Scenario::Source:
            // Source → Affine → Renderer → LCD
            source1 >> affine1 >> renderer >> lcdSink;
            affine1.setRotationScale(0, 6.0f, 6.0f);
            break;

        case Scenario::Affine:
            // Source → Affine(回転) → Renderer → LCD
            source1 >> affine1 >> renderer >> lcdSink;
            break;

        case Scenario::Composite:
            // Source1 → Affine1 → Composite(0)
            // Source2 → Affine2 → Composite(1)
            // Composite → Renderer → LCD
            source1 >> affine1;
            affine1.connectTo(composite, 0);
            source2 >> affine2;
            affine2.connectTo(composite, 1);
            composite >> renderer >> lcdSink;
            break;

        case Scenario::Matte:
            // Source1 → Affine1 → Matte(0) 前景
            // Source2 → Affine2 → Matte(1) 背景
            // Mask → MaskAffine → Matte(2) マスク
            // Matte → Renderer → LCD
            source1 >> affine1;
            affine1.connectTo(matte, 0);
            source2 >> affine2;
            affine2.connectTo(matte, 1);
            maskSource >> maskAffine;
            maskAffine.connectTo(matte, 2);
            matte >> renderer >> lcdSink;
            break;

        default:
            break;
    }
}

// ========================================================================
// アニメーション更新
// ========================================================================

static void updateAnimation() {
    animationTime += 0.03f;
    if (animationTime > 2.0f * static_cast<float>(M_PI)) {
        animationTime -= 2.0f * static_cast<float>(M_PI);
    }

    float rotation = animationTime;
    float scale = 5.0f + 2.0f * std::sin(animationTime * 2.0f);

    switch (currentScenario) {
        case Scenario::Source:
            // 静止
            break;

        case Scenario::Affine:
            affine1.setRotationScale(rotation, scale, scale);
            break;

        case Scenario::Composite:
            affine1.setRotationScale(rotation, 5.0f, 5.0f);
            affine2.setRotationScale(-rotation * 0.5f, 6.0f, 6.0f);
            break;

        case Scenario::Matte:
            affine1.setRotationScale(rotation, 6.0f, 6.0f);
            affine2.setRotationScale(-rotation * 0.3f, 7.0f, 7.0f);
            maskAffine.setRotationScale(rotation * 0.5f, scale, scale);
            break;

        default:
            break;
    }
}

// ========================================================================
// メトリクスレポート
// ========================================================================

static void printMetricsHeader() {
    Serial.println();
    Serial.println("=== fleximg Benchmark ===");
    Serial.printf("Screen: %dx%d, Draw: %dx%d\n", screenW, screenH, drawW, drawH);
    Serial.println();
    Serial.println("CSV Format: Scenario,Frames,TotalTime_us,AvgFrame_us,FPS,AllocBytes,AllocCount");
    Serial.println();
}

static void printMetricsReport() {
    auto& metrics = PerfMetrics::instance();

    // 合計時間とフレーム数
    uint32_t totalTime = metrics.totalTime();
    uint32_t rendererTime = metrics.nodes[NodeType::Renderer].time_us;
    int frames = metrics.nodes[NodeType::Renderer].count;

    if (frames == 0) return;

    float avgFrameTime = static_cast<float>(rendererTime) / static_cast<float>(frames);
    float fps = (avgFrameTime > 0) ? 1000000.0f / avgFrameTime : 0;

    // CSV出力
    Serial.printf("%s,%d,%u,%.1f,%.1f,%u,%d\n",
                  scenarioNames[static_cast<int>(currentScenario)],
                  frames,
                  rendererTime,
                  static_cast<double>(avgFrameTime),
                  static_cast<double>(fps),
                  metrics.totalAllocatedBytes,
                  static_cast<int>(metrics.totalNodeAllocatedBytes()));

    // 詳細出力（シリアル）
    Serial.println("--- Node Details ---");
    for (int i = 0; i < NodeType::Count; ++i) {
        const auto& n = metrics.nodes[i];
        if (n.count > 0) {
            float avgTime = static_cast<float>(n.time_us) / static_cast<float>(n.count);
            Serial.printf("  [%2d] time=%6uus cnt=%4d avg=%.1fus alloc=%uB\n",
                          i, n.time_us, n.count, static_cast<double>(avgTime), n.allocatedBytes);
        }
    }
    Serial.printf("Peak memory: %u bytes\n", metrics.peakMemoryBytes);
    Serial.println();

    // 画面表示
    M5.Display.fillRect(0, 0, screenW, drawY - 5, TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextSize(1);
    M5.Display.printf("[%s] FPS:%.1f\n", scenarioNames[static_cast<int>(currentScenario)],
                      static_cast<double>(fps));
    M5.Display.printf("Frame:%uus Peak:%uB\n",
                      static_cast<unsigned int>(avgFrameTime),
                      metrics.peakMemoryBytes);
}

static void switchScenario() {
    int next = (static_cast<int>(currentScenario) + 1) % static_cast<int>(Scenario::Count);
    currentScenario = static_cast<Scenario>(next);

    Serial.printf("\n>>> Switching to scenario: %s\n\n", scenarioNames[next]);

    PerfMetrics::instance().reset();
    setupPipeline(currentScenario);
    frameCount = 0;
    lastReportTime = lgfx::millis();
}

// ========================================================================
// Arduino Entry Points
// ========================================================================

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    delay(100);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);

    screenW = static_cast<int16_t>(M5.Display.width());
    screenH = static_cast<int16_t>(M5.Display.height());

    // 描画領域
    drawW = 280;
    drawH = 180;
    drawX = (screenW - drawW) / 2;
    drawY = (screenH - drawH) / 2 + 15;

    // 画像作成
    image1 = createCheckerboard(30, 30);
    image2 = createStripes(40, 40);
    maskImage = createCircleMask(50, 50);

    // 初期パイプライン構築
    setupPipeline(currentScenario);

    printMetricsHeader();

    M5.Display.setCursor(0, 0);
    M5.Display.println("fleximg Benchmark");
    M5.Display.println("BtnA: Switch scenario");

    lastReportTime = lgfx::millis();

    M5.Display.startWrite();
}

void loop() {
#if defined ( M5UNIFIED_PC_BUILD )
    lgfx::delay(16);
#else
    // ESP32: ウォッチドッグタイマーをフィード
    yield();
#endif
    M5.update();

    // ボタンでシナリオ切り替え
    if (M5.BtnA.wasPressed()) {
        switchScenario();
    }

    // アニメーション更新
    updateAnimation();

    // レンダリング実行
    renderer.exec();
    frameCount++;

    // 定期レポート
    unsigned long now = lgfx::millis();
    if (frameCount >= FRAMES_PER_REPORT || (now - lastReportTime) >= REPORT_INTERVAL_MS) {
        printMetricsReport();
        PerfMetrics::instance().reset();
        frameCount = 0;
        lastReportTime = now;
    }
}
