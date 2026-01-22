// fleximg M5Stack Format Matrix Benchmark
// ソース/シンク フォーマット総当たり性能計測

#include <M5Unified.h>

// fleximg
#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/core/common.h"
#include "fleximg/core/types.h"
#include "fleximg/core/perf_metrics.h"
#include "fleximg/core/format_metrics.h"
#include "fleximg/image/image_buffer.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/affine_node.h"
#include "fleximg/nodes/composite_node.h"
#include "fleximg/nodes/renderer_node.h"
#include "fleximg/nodes/sink_node.h"

// fleximg implementation files (header-only style inclusion for simplicity)
#include "fleximg/image/pixel_format.cpp"
#include "fleximg/image/viewport.cpp"
#include "fleximg/operations/blend.cpp"
#include "fleximg/operations/filters.cpp"
#include "fleximg/core/memory/platform.cpp"
#include "fleximg/core/memory/pool_allocator.cpp"

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
static constexpr size_t POOL_BLOCK_SIZE = 2 * 1024;   // 2KB per block
static constexpr size_t POOL_BLOCK_COUNT = 32;        // 32 blocks = 64KB
static uint8_t poolMemory[POOL_BLOCK_SIZE * POOL_BLOCK_COUNT];
static memory::PoolAllocator internalPool;
static PoolAllocatorAdapter* poolAdapter = nullptr;

// ========================================================================
// テスト対象フォーマット
// ========================================================================

struct FormatInfo {
    PixelFormatID id;
    const char* name;
    const char* shortName;  // 8文字以内
};

static const FormatInfo testFormats[] = {
    { PixelFormatIDs::RGBA8_Straight, "RGBA8_Straight", "RGBA8" },
    { PixelFormatIDs::RGB888,         "RGB888",         "RGB888" },
    { PixelFormatIDs::RGB565_LE,      "RGB565_LE",      "RGB565" },
    { PixelFormatIDs::RGB332,         "RGB332",         "RGB332" },
};
static constexpr int FORMAT_COUNT = sizeof(testFormats) / sizeof(testFormats[0]);

// ========================================================================
// ベンチマーク設定
// ========================================================================

static constexpr int WARMUP_FRAMES = 10;      // ウォームアップフレーム数
static constexpr int BENCHMARK_FRAMES = 50;   // 計測フレーム数
static constexpr int16_t RENDER_WIDTH = 64;   // レンダリング幅
static constexpr int16_t RENDER_HEIGHT = 64;  // レンダリング高さ

// ========================================================================
// ROM上の固定テスト画像（RGBA8マスターデータ）
// ========================================================================

// 8x8 チェッカーボード RGBA8 (256 bytes) - 赤/黄
static const uint8_t checkerData[8 * 8 * 4] = {
    // Row 0-7: チェッカーパターン
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    255,50,50,255,  255,220,50,255, 255,50,50,255,  255,220,50,255,
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
    255,220,50,255, 255,50,50,255,  255,220,50,255, 255,50,50,255,
};

// ROM画像用のViewPort作成ヘルパー
static ViewPort createRomView(const uint8_t* data, int w, int h, PixelFormatID fmt) {
    ViewPort vp;
    vp.data = const_cast<uint8_t*>(data);  // ROM参照のためconst_cast
    vp.width = static_cast<int16_t>(w);
    vp.height = static_cast<int16_t>(h);
    vp.stride = static_cast<int16_t>(w * getBytesPerPixel(fmt));
    vp.formatID = fmt;
    return vp;
}

// ========================================================================
// ソース画像バッファ（各フォーマット用）
// ========================================================================

static ImageBuffer sourceBuffers[FORMAT_COUNT];
static ViewPort sourceViews[FORMAT_COUNT];

// 背景画像用（青/シアンストライプ、半透明）
static const uint8_t bgData[8 * 8 * 4] = {
    // 縦ストライプ: 青 青 シアン シアン（alpha=200で半透明）
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
    50,100,200,200, 50,100,200,200, 80,180,200,200, 80,180,200,200,
};

static ImageBuffer bgBuffers[FORMAT_COUNT];
static ViewPort bgViews[FORMAT_COUNT];

// マスターデータから各フォーマットのソースを準備
static void prepareSourceImages() {
    ViewPort masterView = createRomView(checkerData, 8, 8, PixelFormatIDs::RGBA8_Straight);
    ViewPort bgMasterView = createRomView(bgData, 8, 8, PixelFormatIDs::RGBA8_Straight);

    for (int i = 0; i < FORMAT_COUNT; ++i) {
        // 前景画像（チェッカー）
        sourceBuffers[i] = ImageBuffer(8, 8, testFormats[i].id);
        sourceViews[i] = sourceBuffers[i].view();
        view_ops::copy(sourceViews[i], 0, 0, masterView, 0, 0, 8, 8);

        // 背景画像（ストライプ）
        bgBuffers[i] = ImageBuffer(8, 8, testFormats[i].id);
        bgViews[i] = bgBuffers[i].view();
        view_ops::copy(bgViews[i], 0, 0, bgMasterView, 0, 0, 8, 8);
    }
}

// ========================================================================
// シンクバッファ（出力先）
// ========================================================================

static ImageBuffer sinkBuffer;

static void prepareSinkBuffer(PixelFormatID format) {
    sinkBuffer = ImageBuffer(RENDER_WIDTH, RENDER_HEIGHT, format);
}

// ========================================================================
// ノード
// ========================================================================

static SourceNode source1;   // 前景
static SourceNode source2;   // 背景
static AffineNode affine1;
static AffineNode affine2;
static CompositeNode composite;
static RendererNode renderer;
static SinkNode sink;

// シンプルパイプライン構築（合成なし）
static void setupSimplePipeline(int sourceFormatIdx, int sinkFormatIdx) {
    // 全ノードの接続をクリア
    source1.disconnectAll();
    affine1.disconnectAll();
    renderer.disconnectAll();
    sink.disconnectAll();

    // ソース設定
    source1.setSource(sourceViews[sourceFormatIdx]);
    source1.setOrigin(float_to_fixed(4.0f), float_to_fixed(4.0f));

    // シンクバッファ準備
    prepareSinkBuffer(testFormats[sinkFormatIdx].id);
    sink.setTarget(sinkBuffer.view());
    sink.setOrigin(float_to_fixed(RENDER_WIDTH / 2.0f), float_to_fixed(RENDER_HEIGHT / 2.0f));

    // レンダラー設定
    renderer.setVirtualScreen(RENDER_WIDTH, RENDER_HEIGHT);
    renderer.setAllocator(poolAdapter);

    // パイプライン接続: Source → Affine → Renderer → Sink
    source1 >> affine1 >> renderer >> sink;

    // 8x8を画面いっぱいに拡大（8倍 → 64x64）
    affine1.setRotationScale(0.0f, 8.0f, 8.0f);
}

// 合成パイプライン構築
static void setupCompositePipeline(int fgFormatIdx, int bgFormatIdx, int sinkFormatIdx) {
    // 全ノードの接続をクリア
    source1.disconnectAll();
    source2.disconnectAll();
    affine1.disconnectAll();
    affine2.disconnectAll();
    composite.disconnectAll();
    renderer.disconnectAll();
    sink.disconnectAll();

    // 前景ソース設定
    source1.setSource(sourceViews[fgFormatIdx]);
    source1.setOrigin(float_to_fixed(4.0f), float_to_fixed(4.0f));

    // 背景ソース設定
    source2.setSource(bgViews[bgFormatIdx]);
    source2.setOrigin(float_to_fixed(4.0f), float_to_fixed(4.0f));

    // シンクバッファ準備
    prepareSinkBuffer(testFormats[sinkFormatIdx].id);
    sink.setTarget(sinkBuffer.view());
    sink.setOrigin(float_to_fixed(RENDER_WIDTH / 2.0f), float_to_fixed(RENDER_HEIGHT / 2.0f));

    // レンダラー設定
    renderer.setVirtualScreen(RENDER_WIDTH, RENDER_HEIGHT);
    renderer.setAllocator(poolAdapter);

    // パイプライン接続:
    // Source1(前景) → Affine1 → Composite(0)
    // Source2(背景) → Affine2 → Composite(1)
    // Composite → Renderer → Sink
    source1 >> affine1;
    affine1.connectTo(composite, 0);  // 前景
    source2 >> affine2;
    affine2.connectTo(composite, 1);  // 背景
    composite >> renderer >> sink;

    // 8x8を画面いっぱいに拡大（8倍 → 64x64）
    affine1.setRotationScale(0.0f, 8.0f, 8.0f);
    affine2.setRotationScale(0.0f, 8.0f, 8.0f);
}

// ========================================================================
// ベンチマーク実行
// ========================================================================

// 結果格納用マトリクス [source][sink]
static uint32_t simpleMatrix[FORMAT_COUNT][FORMAT_COUNT];
static uint32_t compositeMatrix[FORMAT_COUNT][FORMAT_COUNT];

static void runSimpleBenchmark(int srcIdx, int sinkIdx) {
    setupSimplePipeline(srcIdx, sinkIdx);

    // ウォームアップ
    for (int i = 0; i < WARMUP_FRAMES; ++i) {
        renderer.exec();
        M5.delay(1);  // WDTフィード
    }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    PerfMetrics::instance().reset();
#endif

    // 計測
    unsigned long startTime = micros();
    for (int i = 0; i < BENCHMARK_FRAMES; ++i) {
        renderer.exec();
        if ((i & 0x0F) == 0) M5.delay(1);  // 16フレームごとにWDTフィード
    }
    unsigned long endTime = micros();

    uint32_t totalTime = static_cast<uint32_t>(endTime - startTime);
    simpleMatrix[srcIdx][sinkIdx] = totalTime / BENCHMARK_FRAMES;
}

static void runCompositeBenchmark(int fgIdx, int sinkIdx) {
    // 背景は RGBA8 固定（前景フォーマットの影響を見やすくする）
    setupCompositePipeline(fgIdx, 0, sinkIdx);

    // ウォームアップ
    for (int i = 0; i < WARMUP_FRAMES; ++i) {
        renderer.exec();
        M5.delay(1);  // WDTフィード
    }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    PerfMetrics::instance().reset();
#endif

    // 計測
    unsigned long startTime = micros();
    for (int i = 0; i < BENCHMARK_FRAMES; ++i) {
        renderer.exec();
        if ((i & 0x0F) == 0) M5.delay(1);  // 16フレームごとにWDTフィード
    }
    unsigned long endTime = micros();

    uint32_t totalTime = static_cast<uint32_t>(endTime - startTime);
    compositeMatrix[fgIdx][sinkIdx] = totalTime / BENCHMARK_FRAMES;
}

static void runAllBenchmarks() {
    M5_LOGI("=== Format Matrix Benchmark ===");
    M5_LOGI("Render: %dx%d, Frames: %d", RENDER_WIDTH, RENDER_HEIGHT, BENCHMARK_FRAMES);
#ifdef FLEXIMG_COMPOSITE_USE_STRAIGHT
    M5_LOGI("Composite Mode: 8bit Straight");
#else
    M5_LOGI("Composite Mode: 16bit Premul");
#endif
    M5_LOGI("");

    int total = FORMAT_COUNT * FORMAT_COUNT * 2;  // Simple + Composite
    int current = 0;

    // Simple パイプライン (Source → Affine → Sink)
    M5_LOGI("--- Simple Pipeline ---");
    for (int srcIdx = 0; srcIdx < FORMAT_COUNT; ++srcIdx) {
        for (int sinkIdx = 0; sinkIdx < FORMAT_COUNT; ++sinkIdx) {
            current++;
            M5_LOGI("Simple [%d/%d]: %s -> %s",
                    current, total,
                    testFormats[srcIdx].shortName,
                    testFormats[sinkIdx].shortName);

            M5.Display.fillRect(0, 40, 320, 20, TFT_BLACK);
            M5.Display.setCursor(0, 40);
            M5.Display.printf("Simple %d/%d: %s->%s",
                              current, total,
                              testFormats[srcIdx].shortName,
                              testFormats[sinkIdx].shortName);

            runSimpleBenchmark(srcIdx, sinkIdx);

            M5_LOGI("  Result: %lu us/frame", static_cast<unsigned long>(simpleMatrix[srcIdx][sinkIdx]));
        }
    }

    // Composite パイプライン (Source1 + Source2 → Composite → Sink)
    M5_LOGI("");
    M5_LOGI("--- Composite Pipeline (BG=RGBA8) ---");
    for (int fgIdx = 0; fgIdx < FORMAT_COUNT; ++fgIdx) {
        for (int sinkIdx = 0; sinkIdx < FORMAT_COUNT; ++sinkIdx) {
            current++;
            M5_LOGI("Composite [%d/%d]: FG=%s -> %s",
                    current, total,
                    testFormats[fgIdx].shortName,
                    testFormats[sinkIdx].shortName);

            M5.Display.fillRect(0, 40, 320, 20, TFT_BLACK);
            M5.Display.setCursor(0, 40);
            M5.Display.printf("Comp %d/%d: %s->%s",
                              current, total,
                              testFormats[fgIdx].shortName,
                              testFormats[sinkIdx].shortName);

            runCompositeBenchmark(fgIdx, sinkIdx);

            M5_LOGI("  Result: %lu us/frame", static_cast<unsigned long>(compositeMatrix[fgIdx][sinkIdx]));
        }
    }
}

static void printMatrix(const char* title, uint32_t matrix[FORMAT_COUNT][FORMAT_COUNT]) {
    M5_LOGI("");
    M5_LOGI("=== %s (us/frame) ===", title);
    M5_LOGI("");

    // ヘッダー行
    char header[128];
    int pos = snprintf(header, sizeof(header), "Src\\Sink  ");
    for (int i = 0; i < FORMAT_COUNT; ++i) {
        pos += snprintf(header + pos, sizeof(header) - static_cast<size_t>(pos), "%8s", testFormats[i].shortName);
    }
    M5_LOGI("%s", header);

    // 区切り線
    M5_LOGI("--------  --------------------------------");

    // データ行
    for (int srcIdx = 0; srcIdx < FORMAT_COUNT; ++srcIdx) {
        char row[128];
        int rowPos = snprintf(row, sizeof(row), "%-8s  ", testFormats[srcIdx].shortName);
        for (int sinkIdx = 0; sinkIdx < FORMAT_COUNT; ++sinkIdx) {
            rowPos += snprintf(row + rowPos, sizeof(row) - static_cast<size_t>(rowPos), "%8lu",
                               static_cast<unsigned long>(matrix[srcIdx][sinkIdx]));
        }
        M5_LOGI("%s", row);
    }
}

static void printResultMatrix() {
#ifdef FLEXIMG_COMPOSITE_USE_STRAIGHT
    M5_LOGI("=== Composite Mode: 8bit Straight ===");
#else
    M5_LOGI("=== Composite Mode: 16bit Premul ===");
#endif

    printMatrix("Simple Pipeline", simpleMatrix);
    printMatrix("Composite Pipeline (BG=RGBA8)", compositeMatrix);

    // CSV形式も出力
    M5_LOGI("");
    M5_LOGI("=== CSV Format (Simple) ===");
    char csvHeader[128];
    int csvPos = snprintf(csvHeader, sizeof(csvHeader), "Source");
    for (int i = 0; i < FORMAT_COUNT; ++i) {
        csvPos += snprintf(csvHeader + csvPos, sizeof(csvHeader) - static_cast<size_t>(csvPos), ",%s", testFormats[i].shortName);
    }
    M5_LOGI("%s", csvHeader);

    for (int srcIdx = 0; srcIdx < FORMAT_COUNT; ++srcIdx) {
        char csvRow[128];
        int csvRowPos = snprintf(csvRow, sizeof(csvRow), "%s", testFormats[srcIdx].shortName);
        for (int sinkIdx = 0; sinkIdx < FORMAT_COUNT; ++sinkIdx) {
            csvRowPos += snprintf(csvRow + csvRowPos, sizeof(csvRow) - static_cast<size_t>(csvRowPos), ",%lu",
                                  static_cast<unsigned long>(simpleMatrix[srcIdx][sinkIdx]));
        }
        M5_LOGI("%s", csvRow);
    }

    M5_LOGI("");
    M5_LOGI("=== CSV Format (Composite) ===");
    M5_LOGI("%s", csvHeader);

    for (int srcIdx = 0; srcIdx < FORMAT_COUNT; ++srcIdx) {
        char csvRow[128];
        int csvRowPos = snprintf(csvRow, sizeof(csvRow), "%s", testFormats[srcIdx].shortName);
        for (int sinkIdx = 0; sinkIdx < FORMAT_COUNT; ++sinkIdx) {
            csvRowPos += snprintf(csvRow + csvRowPos, sizeof(csvRow) - static_cast<size_t>(csvRowPos), ",%lu",
                                  static_cast<unsigned long>(compositeMatrix[srcIdx][sinkIdx]));
        }
        M5_LOGI("%s", csvRow);
    }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    // フォーマット変換統計
    auto& fmtMetrics = FormatMetrics::instance();
    M5_LOGI("");
    M5_LOGI("=== Format Conversion Stats ===");
    static const char* fmtNames[] = {
        "RGBA16P", "RGBA8", "RGB565LE", "RGB565BE", "RGB332", "RGB888", "BGR888", "Alpha8"
    };
    static const char* opNames[] = {
        "ToStr", "FrStr", "ToPre", "FrPre", "BlnUn", "BlnUnS"
    };
    for (int f = 0; f < FormatIdx::Count; ++f) {
        auto fmtTotal = fmtMetrics.totalByFormat(f);
        if (fmtTotal.callCount > 0) {
            M5_LOGI("%s: calls=%lu px=%llu",
                    fmtNames[f],
                    static_cast<unsigned long>(fmtTotal.callCount),
                    static_cast<unsigned long long>(fmtTotal.pixelCount));
            for (int o = 0; o < OpType::Count; ++o) {
                const auto& entry = fmtMetrics.data[f][o];
                if (entry.callCount > 0) {
                    M5_LOGI("  %s: calls=%lu px=%llu",
                            opNames[o],
                            static_cast<unsigned long>(entry.callCount),
                            static_cast<unsigned long long>(entry.pixelCount));
                }
            }
        }
    }
#endif
}

static int displayPage = 0;  // 0: Simple, 1: Composite

static void displayResultMatrix() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextSize(1);

#ifdef FLEXIMG_COMPOSITE_USE_STRAIGHT
    M5.Display.print("Mode: 8bit Straight  ");
#else
    M5.Display.print("Mode: 16bit Premul  ");
#endif

    uint32_t (*matrix)[FORMAT_COUNT] = (displayPage == 0) ? simpleMatrix : compositeMatrix;
    const char* pageName = (displayPage == 0) ? "[Simple]" : "[Composite]";
    M5.Display.println(pageName);
    M5.Display.println("");

    // ヘッダー
    M5.Display.print("Src\\Snk ");
    for (int i = 0; i < FORMAT_COUNT; ++i) {
        M5.Display.printf("%6s", testFormats[i].shortName);
    }
    M5.Display.println("");

    // データ
    for (int srcIdx = 0; srcIdx < FORMAT_COUNT; ++srcIdx) {
        M5.Display.printf("%-7s ", testFormats[srcIdx].shortName);
        for (int sinkIdx = 0; sinkIdx < FORMAT_COUNT; ++sinkIdx) {
            M5.Display.printf("%6lu", static_cast<unsigned long>(matrix[srcIdx][sinkIdx]));
        }
        M5.Display.println("");
    }

    M5.Display.println("");
    M5.Display.println("Unit: us/frame");
    M5.Display.println("BtnA:Re-run BtnB:Toggle");
}

// ========================================================================
// Arduino Entry Points
// ========================================================================

static bool benchmarkDone = false;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Log.setLogLevel(m5::log_target_t::log_target_serial, esp_log_level_t::ESP_LOG_INFO);

    M5.delay(100);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(1);

    M5.Display.setCursor(0, 0);
    M5.Display.println("Format Matrix Benchmark");
    M5.Display.println("");
#ifdef FLEXIMG_COMPOSITE_USE_STRAIGHT
    M5.Display.println("Mode: 8bit Straight");
#else
    M5.Display.println("Mode: 16bit Premul");
#endif
    M5.Display.println("");
    M5.Display.println("Preparing...");

    // PoolAllocatorを初期化
    internalPool.initialize(poolMemory, POOL_BLOCK_SIZE, POOL_BLOCK_COUNT, false);
    static PoolAllocatorAdapter adapter(internalPool);
    poolAdapter = &adapter;

    // ソース画像を各フォーマットで準備
    prepareSourceImages();

    M5.Display.println("Starting benchmark...");
    M5.delay(500);

    // ベンチマーク実行
    runAllBenchmarks();

    // 結果出力
    printResultMatrix();
    displayResultMatrix();

    benchmarkDone = true;
}

void loop() {
    M5.delay(100);
    M5.update();

    if (!benchmarkDone) return;

    // BtnA: 再実行
    if (M5.BtnA.wasPressed()) {
        benchmarkDone = false;

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setCursor(0, 0);
        M5.Display.println("Re-running benchmark...");

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().reset();
        FormatMetrics::instance().reset();
#endif

        runAllBenchmarks();
        printResultMatrix();
        displayResultMatrix();

        benchmarkDone = true;
    }

    // BtnB: 表示切り替え（Simple / Composite）
    if (M5.BtnB.wasPressed()) {
        displayPage = 1 - displayPage;
        displayResultMatrix();
    }
}
