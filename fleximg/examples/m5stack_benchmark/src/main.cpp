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
// PoolAllocatorアダプタ
// ========================================================================

class PoolAllocatorAdapter : public memory::IAllocator {
public:
    PoolAllocatorAdapter(memory::PoolAllocator& pool) : pool_(pool) {}

    void* allocate(size_t bytes, size_t /* alignment */ = 16) override {
        void* ptr = pool_.allocate(bytes);
        if (ptr) return ptr;
        // プールから確保できない場合はDefaultAllocatorにフォールバック
        return memory::DefaultAllocator::instance().allocate(bytes);
    }

    void deallocate(void* ptr) override {
        if (!pool_.deallocate(ptr)) {
            memory::DefaultAllocator::instance().deallocate(ptr);
        }
    }

    const char* name() const override { return "PoolAllocatorAdapter"; }

private:
    memory::PoolAllocator& pool_;
};

// PoolAllocator用のメモリプール
// 画面幅320px × RGBA8(4bytes) = 1280bytes/line、余裕を持って4KB/block
static constexpr size_t POOL_BLOCK_SIZE = 4 * 1024;  // 4KB per block
static constexpr size_t POOL_BLOCK_COUNT = 8;        // 8 blocks = 32KB
static uint8_t poolMemory[POOL_BLOCK_SIZE * POOL_BLOCK_COUNT];
static memory::PoolAllocator internalPool;
static PoolAllocatorAdapter* poolAdapter = nullptr;

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
// ROM上の固定テスト画像（ヒープを使わない）
// ========================================================================

// 8x8 チェッカーボード RGBA8 (256 bytes) - 赤/黄
static const uint8_t checkerData[8 * 8 * 4] = {
    // Row 0: R Y R Y R Y R Y
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    // Row 1: Y R Y R Y R Y R
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
    // Row 2
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    // Row 3
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
    // Row 4
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    // Row 5
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
    // Row 6
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    // Row 7
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
};

// 8x8 青/シアン ストライプ RGBA8 (256 bytes)
static const uint8_t stripeData[8 * 8 * 4] = {
    // 縦ストライプ: 青 青 シアン シアン 青 青 シアン シアン
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
    50,100,200,255, 50,100,200,255, 80,180,200,255, 80,180,200,255,
};

// 8x8 円形マスク Alpha8 (64 bytes)
static const uint8_t circleMaskData[8 * 8] = {
    0,   0,   128, 255, 255, 128, 0,   0,
    0,   200, 255, 255, 255, 255, 200, 0,
    128, 255, 255, 255, 255, 255, 255, 128,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    128, 255, 255, 255, 255, 255, 255, 128,
    0,   200, 255, 255, 255, 255, 200, 0,
    0,   0,   128, 255, 255, 128, 0,   0,
};

// ROM画像用のViewPort作成ヘルパー
static ViewPort createRomView(const uint8_t* data, int w, int h, PixelFormatID fmt) {
    ViewPort vp;
    vp.data = const_cast<uint8_t*>(data);  // ROM参照のためconst_cast
    vp.width = w;
    vp.height = h;
    vp.stride = w * getBytesPerPixel(fmt);
    vp.formatID = fmt;
    return vp;
}

// ========================================================================
// グローバル変数
// ========================================================================

// ROM画像用ViewPort（動的メモリ確保なし）
static ViewPort image1View;
static ViewPort image2View;
static ViewPort maskView;

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

// ノード初期化（setup()から1回だけ呼ぶ）
static void initializeNodes() {
    // 共通設定
    renderer.setVirtualScreen(drawW, drawH);
    renderer.setAllocator(poolAdapter);  // 内部バッファ用アロケータを設定
    lcdSink.setTarget(&M5.Display, drawX, drawY, drawW, drawH);
    lcdSink.setOrigin(float_to_fixed(drawW / 2.0f), float_to_fixed(drawH / 2.0f));

    // ソース設定
    // ROM画像をソースに設定
    source1.setSource(image1View);
    source1.setOrigin(float_to_fixed(image1View.width / 2.0f), float_to_fixed(image1View.height / 2.0f));

    source2.setSource(image2View);
    source2.setOrigin(float_to_fixed(image2View.width / 2.0f), float_to_fixed(image2View.height / 2.0f));

    maskSource.setSource(maskView);
    maskSource.setOrigin(float_to_fixed(maskView.width / 2.0f), float_to_fixed(maskView.height / 2.0f));
}

static void setupPipeline(Scenario scenario) {
    // 全ノードの接続をクリア（ノード自体は再生成しない）
    source1.disconnectAll();
    source2.disconnectAll();
    maskSource.disconnectAll();
    affine1.disconnectAll();
    affine2.disconnectAll();
    maskAffine.disconnectAll();
    composite.disconnectAll();
    matte.disconnectAll();
    renderer.disconnectAll();
    lcdSink.disconnectAll();

    // シナリオ別パイプライン構築
    // 8x8画像を画面いっぱいに拡大（約40倍）して描画負荷を高める
    constexpr float baseScale = 40.0f;

    switch (scenario) {
        case Scenario::Source:
            // Source → Affine → Renderer → LCD（静止画、最大スケール）
            source1 >> affine1 >> renderer >> lcdSink;
            affine1.setRotationScale(0, baseScale, baseScale);
            break;

        case Scenario::Affine:
            // Source → Affine(回転+スケール変動) → Renderer → LCD
            source1 >> affine1 >> renderer >> lcdSink;
            affine1.setRotationScale(0, baseScale, baseScale);
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
            affine1.setRotationScale(0, baseScale, baseScale);
            affine2.setRotationScale(0, baseScale * 0.8f, baseScale * 0.8f);
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
            affine1.setRotationScale(0, baseScale, baseScale);
            affine2.setRotationScale(0, baseScale * 0.9f, baseScale * 0.9f);
            maskAffine.setRotationScale(0, baseScale, baseScale);
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

    // 画面いっぱいのスケール（8x8 → 320x320相当）
    constexpr float baseScale = 40.0f;
    float rotation = animationTime;
    float scaleVar = baseScale + 5.0f * std::sin(animationTime * 2.0f);

    switch (currentScenario) {
        case Scenario::Source:
            // 静止（setupPipelineで設定済み）
            break;

        case Scenario::Affine:
            // 回転+スケール変動
            affine1.setRotationScale(rotation, scaleVar, scaleVar);
            break;

        case Scenario::Composite:
            // 2画像を異なる回転で合成
            affine1.setRotationScale(rotation, baseScale, baseScale);
            affine2.setRotationScale(-rotation * 0.5f, baseScale * 0.8f, baseScale * 0.8f);
            break;

        case Scenario::Matte:
            // 前景、背景、マスクを異なるパラメータで変換
            affine1.setRotationScale(rotation, baseScale, baseScale);
            affine2.setRotationScale(-rotation * 0.3f, baseScale * 0.9f, baseScale * 0.9f);
            maskAffine.setRotationScale(rotation * 0.5f, scaleVar, scaleVar);
            break;

        default:
            break;
    }
}

// ========================================================================
// メトリクスレポート
// ========================================================================

static void printMetricsHeader() {
    M5_LOGI("=== fleximg Benchmark ===");
    M5_LOGI("Screen: %dx%d, Draw: %dx%d", screenW, screenH, drawW, drawH);
    M5_LOGI("CSV Format: Scenario,Frames,TotalTime_us,AvgFrame_us,FPS,AllocBytes,AllocCount");
}

static void printMetricsReport() {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    auto& metrics = PerfMetrics::instance();

    // 合計時間とフレーム数
    uint32_t rendererTime = metrics.nodes[NodeType::Renderer].time_us;
    int frames = metrics.nodes[NodeType::Renderer].count;

    if (frames == 0) return;

    float avgFrameTime = static_cast<float>(rendererTime) / static_cast<float>(frames);
    float fps = (avgFrameTime > 0) ? 1000000.0f / avgFrameTime : 0;

    // CSV出力
    M5_LOGI("%s,%d,%lu,%.1f,%.1f,%lu,%d",
            scenarioNames[static_cast<int>(currentScenario)],
            frames,
            static_cast<unsigned long>(rendererTime),
            static_cast<double>(avgFrameTime),
            static_cast<double>(fps),
            static_cast<unsigned long>(metrics.totalAllocatedBytes),
            static_cast<int>(metrics.totalNodeAllocatedBytes()));

    // 詳細出力
    M5_LOGI("--- Node Details ---");
    for (int i = 0; i < NodeType::Count; ++i) {
        const auto& n = metrics.nodes[i];
        if (n.count > 0) {
            float avgTime = static_cast<float>(n.time_us) / static_cast<float>(n.count);
            M5_LOGI("  [%2d] time=%6luus cnt=%4d avg=%.1fus alloc=%luB",
                    i, static_cast<unsigned long>(n.time_us), n.count,
                    static_cast<double>(avgTime), static_cast<unsigned long>(n.allocatedBytes));
        }
    }
    M5_LOGI("Peak memory: %lu bytes", static_cast<unsigned long>(metrics.peakMemoryBytes));

    // 画面表示
    M5.Display.fillRect(0, 0, screenW, drawY - 5, TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextSize(1);
    M5.Display.printf("[%s] FPS:%.1f\n", scenarioNames[static_cast<int>(currentScenario)],
                      static_cast<double>(fps));
    M5.Display.printf("Frame:%uus Peak:%luB\n",
                      static_cast<unsigned int>(avgFrameTime),
                      static_cast<unsigned long>(metrics.peakMemoryBytes));
#else
    // FLEXIMG_DEBUG無効時は簡易FPS表示のみ
    static unsigned long lastMs = 0;
    unsigned long now = M5.millis();
    float fps = (now > lastMs) ? 1000.0f * static_cast<float>(frameCount) / static_cast<float>(now - lastMs) : 0;
    lastMs = now;

    M5_LOGI("%s: FPS=%.1f", scenarioNames[static_cast<int>(currentScenario)], static_cast<double>(fps));

    M5.Display.fillRect(0, 0, screenW, drawY - 5, TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextSize(1);
    M5.Display.printf("[%s] FPS:%.1f\n", scenarioNames[static_cast<int>(currentScenario)], static_cast<double>(fps));
#endif
}

static void switchScenario() {
    int next = (static_cast<int>(currentScenario) + 1) % static_cast<int>(Scenario::Count);
    currentScenario = static_cast<Scenario>(next);

    M5_LOGI(">>> Switching to scenario: %s", scenarioNames[next]);

    PerfMetrics::instance().reset();
    setupPipeline(currentScenario);
    frameCount = 0;
    lastReportTime = M5.millis();
}

// ========================================================================
// Arduino Entry Points
// ========================================================================

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Log.setLogLevel(m5::log_target_t::log_target_serial, esp_log_level_t::ESP_LOG_INFO);

    M5.delay(100);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);

    screenW = static_cast<int16_t>(M5.Display.width());
    screenH = static_cast<int16_t>(M5.Display.height());

    // 描画領域（画面いっぱい、上部にメトリクス表示領域を確保）
    drawW = screenW;
    drawH = screenH - 20;  // 上部20pxをメトリクス表示に使用
    drawX = 0;
    drawY = 20;

    // PoolAllocatorを初期化（fleximg内部バッファ用）
    internalPool.initialize(poolMemory, POOL_BLOCK_SIZE, POOL_BLOCK_COUNT, false);
    static PoolAllocatorAdapter adapter(internalPool);
    poolAdapter = &adapter;

    // ROM上の固定画像をViewPortとして参照（ヒープ確保なし）
    image1View = createRomView(checkerData, 8, 8, PixelFormatIDs::RGBA8_Straight);
    image2View = createRomView(stripeData, 8, 8, PixelFormatIDs::RGBA8_Straight);
    maskView = createRomView(circleMaskData, 8, 8, PixelFormatIDs::Alpha8);

    // ノード初期化（1回だけ）
    initializeNodes();

    // 初期パイプライン構築
    setupPipeline(currentScenario);

    printMetricsHeader();

    M5.Display.setCursor(0, 0);
    M5.Display.println("fleximg Benchmark");
    M5.Display.println("BtnA: Switch scenario");

    lastReportTime = M5.millis();

    M5.Display.startWrite();
}

void loop() {
    // nativeビルド時は16ms待機、ESP32時はWDTフィード
    M5.delay(M5.getBoard() == m5::board_t::board_unknown ? 16 : 1);
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
    unsigned long now = M5.millis();
    if (frameCount >= FRAMES_PER_REPORT || (now - lastReportTime) >= REPORT_INTERVAL_MS) {
        printMetricsReport();
        PerfMetrics::instance().reset();
        frameCount = 0;
        lastReportTime = now;
    }
}
