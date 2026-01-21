// fleximg Method Benchmark
// 変換関数の単体性能測定

#include <M5Unified.h>

// fleximg
#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/core/common.h"
#include "fleximg/image/pixel_format.h"

// fleximg implementation
#include "fleximg/image/pixel_format.cpp"

using namespace fleximg;

// ========================================================================
// ベンチマーク設定
// ========================================================================

// バッファサイズ（ESP32のSRAM制約を考慮して小さめに）
static constexpr int BENCH_PIXELS = 4096;

// 繰り返し回数（小さいバッファを多数回処理）
static constexpr int ITERATIONS = 1000;

// ウォームアップ回数
static constexpr int WARMUP = 10;

// バッファ（動的確保）
static uint8_t* srcRGBA8 = nullptr;      // RGBA8_Straight (4096 * 4 = 16KB)
static uint8_t* dstRGB565 = nullptr;     // RGB565 (4096 * 2 = 8KB)
static uint16_t* dstRGBA16 = nullptr;    // RGBA16_Premultiplied (4096 * 8 = 32KB)
static uint16_t* canvasRGBA16 = nullptr; // blendUnderPremul用キャンバス (4096 * 8 = 32KB)

// ========================================================================
// ベンチマーク関数
// ========================================================================

struct BenchResult {
    const char* name;
    uint32_t totalUs;
    uint32_t perFrameUs;
    float pixelsPerUs;
};

static BenchResult results[20];
static int resultCount = 0;

bool allocateBuffers() {
    // バッファ動的確保
    srcRGBA8 = static_cast<uint8_t*>(malloc(BENCH_PIXELS * 4));
    dstRGB565 = static_cast<uint8_t*>(malloc(BENCH_PIXELS * 2));
    dstRGBA16 = static_cast<uint16_t*>(malloc(BENCH_PIXELS * 8));
    canvasRGBA16 = static_cast<uint16_t*>(malloc(BENCH_PIXELS * 8));

    if (!srcRGBA8 || !dstRGB565 || !dstRGBA16 || !canvasRGBA16) {
        Serial.println("ERROR: Failed to allocate buffers!");
        return false;
    }

    Serial.printf("Buffers allocated: src=%p, dst565=%p, dst16=%p, canvas=%p\n",
                  srcRGBA8, dstRGB565, dstRGBA16, canvasRGBA16);
    return true;
}

// アルファパターンの種類
enum class AlphaPattern {
    Mixed,       // 透明/半透明/不透明の混在（デフォルト）
    AllOpaque,   // 全て不透明（alpha=255）
    AllTransparent, // 全て透明（alpha=0）
    AllSemi      // 全て半透明（alpha=128）
};

static AlphaPattern currentPattern = AlphaPattern::Mixed;

void initTestData(AlphaPattern pattern = AlphaPattern::Mixed) {
    if (!srcRGBA8) return;
    currentPattern = pattern;

    for (int i = 0; i < BENCH_PIXELS; i++) {
        int idx = i * 4;
        srcRGBA8[idx + 0] = static_cast<uint8_t>(i & 0xFF);         // R
        srcRGBA8[idx + 1] = static_cast<uint8_t>((i >> 4) & 0xFF);  // G
        srcRGBA8[idx + 2] = static_cast<uint8_t>((i >> 8) & 0xFF);  // B

        uint8_t alpha;
        switch (pattern) {
            case AlphaPattern::AllOpaque:
                alpha = 255;
                break;
            case AlphaPattern::AllTransparent:
                alpha = 0;
                break;
            case AlphaPattern::AllSemi:
                alpha = 128;
                break;
            case AlphaPattern::Mixed:
            default:
                // 96ピクセル周期のアルファパターン:
                // [0-31]:  A=0 (透明 32px)
                // [32-47]: A=0→255 グラデーション (16px)
                // [48-79]: A=255 (不透明 32px)
                // [80-95]: A=255→0 グラデーション (16px)
                {
                    int phase = i % 96;
                    if (phase < 32) {
                        alpha = 0;
                    } else if (phase < 48) {
                        alpha = static_cast<uint8_t>(16 + (phase - 32) * 15);
                    } else if (phase < 80) {
                        alpha = 255;
                    } else {
                        alpha = static_cast<uint8_t>(16 + (95 - phase) * 15);
                    }
                }
                break;
        }
        srcRGBA8[idx + 3] = alpha;
    }
}

// キャンバスを半透明で初期化（blendUnderPremul用）
void initCanvasHalfTransparent() {
    if (!canvasRGBA16) return;
    // 半透明の緑色（Premultiplied形式）
    // alpha = 32768 (約50%), G = 32768 (α×65535)
    for (int i = 0; i < BENCH_PIXELS; i++) {
        int idx = i * 4;
        canvasRGBA16[idx + 0] = 0;      // R
        canvasRGBA16[idx + 1] = 32768;  // G (premul)
        canvasRGBA16[idx + 2] = 0;      // B
        canvasRGBA16[idx + 3] = 32768;  // A
    }
}

template<typename Func>
BenchResult runBench(const char* name, Func func) {
    // ウォームアップ
    for (int i = 0; i < WARMUP; i++) {
        func();
    }

    // 計測
    uint32_t start = micros();
    for (int i = 0; i < ITERATIONS; i++) {
        func();
    }
    uint32_t elapsed = micros() - start;

    BenchResult result;
    result.name = name;
    result.totalUs = elapsed;
    result.perFrameUs = elapsed / ITERATIONS;
    result.pixelsPerUs = static_cast<float>(BENCH_PIXELS) * ITERATIONS / elapsed;

    return result;
}

void printResult(const BenchResult& r) {
    Serial.printf("%-24s: %6u us/frame, %.2f Mpix/s\n",
                  r.name, r.perFrameUs, r.pixelsPerUs);

    M5.Display.printf("%-20s %5u us\n", r.name, r.perFrameUs);
}

void printHeader() {
    Serial.println("\n========================================");
    Serial.printf("Method Benchmark (%d pixels x %d iter)\n",
                  BENCH_PIXELS, ITERATIONS);
    Serial.printf("Total: %.1f Mpixels, Warmup: %d\n",
                  static_cast<float>(BENCH_PIXELS) * ITERATIONS / 1000000.0f, WARMUP);
    Serial.println("========================================\n");

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(0, 0);
    M5.Display.printf("Bench %d px x %d\n\n", BENCH_PIXELS, ITERATIONS);
}

// ========================================================================
// 個別ベンチマーク
// ========================================================================

void benchRgb565beFromStraight() {
    auto result = runBench("rgb565be_fromStraight", []() {
        BuiltinFormats::RGB565_BE.fromStraight(
            dstRGB565, srcRGBA8, BENCH_PIXELS, nullptr);
    });
    results[resultCount++] = result;
    printResult(result);
}

void benchRgb565leFromStraight() {
    auto result = runBench("rgb565le_fromStraight", []() {
        BuiltinFormats::RGB565_LE.fromStraight(
            dstRGB565, srcRGBA8, BENCH_PIXELS, nullptr);
    });
    results[resultCount++] = result;
    printResult(result);
}

void benchRgba8ToPremul() {
    auto result = runBench("rgba8_toPremul", []() {
        BuiltinFormats::RGBA8_Straight.toPremul(
            dstRGBA16, srcRGBA8, BENCH_PIXELS, nullptr);
    });
    results[resultCount++] = result;
    printResult(result);
}

void benchRgba8FromPremul() {
    // 先にPremulデータを準備
    BuiltinFormats::RGBA8_Straight.toPremul(
        dstRGBA16, srcRGBA8, BENCH_PIXELS, nullptr);

    auto result = runBench("rgba8_fromPremul", []() {
        BuiltinFormats::RGBA8_Straight.fromPremul(
            srcRGBA8, dstRGBA16, BENCH_PIXELS, nullptr);
    });
    results[resultCount++] = result;
    printResult(result);
}

void benchRgb565beToPremul() {
    // 先にRGB565_BEデータを準備
    BuiltinFormats::RGB565_BE.fromStraight(
        dstRGB565, srcRGBA8, BENCH_PIXELS, nullptr);

    auto result = runBench("rgb565be_toPremul", []() {
        BuiltinFormats::RGB565_BE.toPremul(
            dstRGBA16, dstRGB565, BENCH_PIXELS, nullptr);
    });
    results[resultCount++] = result;
    printResult(result);
}

void benchRgb565beFromPremul() {
    // 先にPremulデータを準備
    BuiltinFormats::RGBA8_Straight.toPremul(
        dstRGBA16, srcRGBA8, BENCH_PIXELS, nullptr);

    auto result = runBench("rgb565be_fromPremul", []() {
        BuiltinFormats::RGB565_BE.fromPremul(
            dstRGB565, dstRGBA16, BENCH_PIXELS, nullptr);
    });
    results[resultCount++] = result;
    printResult(result);
}

void benchRgb565leToPremul() {
    // 先にRGB565_LEデータを準備
    BuiltinFormats::RGB565_LE.fromStraight(
        dstRGB565, srcRGBA8, BENCH_PIXELS, nullptr);

    auto result = runBench("rgb565le_toPremul", []() {
        BuiltinFormats::RGB565_LE.toPremul(
            dstRGBA16, dstRGB565, BENCH_PIXELS, nullptr);
    });
    results[resultCount++] = result;
    printResult(result);
}

void benchRgb565leFromPremul() {
    // 先にPremulデータを準備
    BuiltinFormats::RGBA8_Straight.toPremul(
        dstRGBA16, srcRGBA8, BENCH_PIXELS, nullptr);

    auto result = runBench("rgb565le_fromPremul", []() {
        BuiltinFormats::RGB565_LE.fromPremul(
            dstRGB565, dstRGBA16, BENCH_PIXELS, nullptr);
    });
    results[resultCount++] = result;
    printResult(result);
}

void benchMemcpy() {
    auto result = runBench("memcpy (baseline)", []() {
        memcpy(dstRGB565, srcRGBA8, BENCH_PIXELS * 2);
    });
    results[resultCount++] = result;
    printResult(result);
}

// ========================================================================
// CompositeNode関連ベンチマーク（追加）
// ========================================================================

void benchRgba16PremulToStraight() {
    // 先にPremulデータを準備
    BuiltinFormats::RGBA8_Straight.toPremul(
        dstRGBA16, srcRGBA8, BENCH_PIXELS, nullptr);

    auto result = runBench("rgba16p_toStraight", []() {
        BuiltinFormats::RGBA16_Premultiplied.toStraight(
            srcRGBA8, dstRGBA16, BENCH_PIXELS, nullptr);
    });
    results[resultCount++] = result;
    printResult(result);
}

void benchRgba8BlendUnderPremul() {
    // キャンバスを半透明で初期化
    initCanvasHalfTransparent();

    auto result = runBench("rgba8_blendUnder", []() {
        // キャンバスを毎回リセット（公平な測定のため）
        initCanvasHalfTransparent();
        BuiltinFormats::RGBA8_Straight.blendUnderPremul(
            canvasRGBA16, srcRGBA8, BENCH_PIXELS, nullptr);
    });
    results[resultCount++] = result;
    printResult(result);
}

void benchRgba16PremulBlendUnderPremul() {
    // srcをPremul形式で準備
    BuiltinFormats::RGBA8_Straight.toPremul(
        dstRGBA16, srcRGBA8, BENCH_PIXELS, nullptr);

    // キャンバスを半透明で初期化
    initCanvasHalfTransparent();

    auto result = runBench("rgba16p_blendUnder", []() {
        initCanvasHalfTransparent();
        BuiltinFormats::RGBA16_Premultiplied.blendUnderPremul(
            canvasRGBA16, dstRGBA16, BENCH_PIXELS, nullptr);
    });
    results[resultCount++] = result;
    printResult(result);
}

// ========================================================================
// メイン
// ========================================================================

void runAllBenchmarks() {
    resultCount = 0;
    printHeader();

    Serial.println("Running benchmarks (Mixed alpha)...\n");
    M5.Display.println("Running...\n");

    // テストデータ初期化（混在パターン）
    initTestData(AlphaPattern::Mixed);

    // ベースライン
    benchMemcpy();

    // fromStraight系
    benchRgb565beFromStraight();
    benchRgb565leFromStraight();

    // Premul系
    benchRgba8ToPremul();
    benchRgba8FromPremul();
    benchRgb565beToPremul();
    benchRgb565beFromPremul();
    benchRgb565leToPremul();
    benchRgb565leFromPremul();

    // CompositeNode関連（追加）
    Serial.println("\n--- CompositeNode methods ---");
    M5.Display.println("\n-- Composite --");
    benchRgba16PremulToStraight();
    benchRgba8BlendUnderPremul();
    benchRgba16PremulBlendUnderPremul();

    Serial.println("\n========================================");
    Serial.println("Benchmark complete!");
    Serial.println("========================================\n");

    M5.Display.println("\nComplete!");
    M5.Display.println("Touch to rerun");
}

// アルファパターン別ベンチマーク（blendUnderPremulの条件分岐影響測定）
void runBlendUnderBenchByPattern() {
    Serial.println("\n========================================");
    Serial.println("blendUnderPremul by Alpha Pattern");
    Serial.println("========================================\n");

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("BlendUnder by Pattern\n");

    const char* patternNames[] = {"Mixed", "Opaque", "Transparent", "Semi"};
    AlphaPattern patterns[] = {
        AlphaPattern::Mixed,
        AlphaPattern::AllOpaque,
        AlphaPattern::AllTransparent,
        AlphaPattern::AllSemi
    };

    for (int p = 0; p < 4; p++) {
        Serial.printf("\n--- Pattern: %s ---\n", patternNames[p]);
        M5.Display.printf("\n%s:\n", patternNames[p]);

        initTestData(patterns[p]);

        // RGBA8_Straight → blendUnderPremul
        initCanvasHalfTransparent();
        auto result = runBench("rgba8_blendUnder", []() {
            initCanvasHalfTransparent();
            BuiltinFormats::RGBA8_Straight.blendUnderPremul(
                canvasRGBA16, srcRGBA8, BENCH_PIXELS, nullptr);
        });
        printResult(result);
    }

    Serial.println("\n========================================\n");
    M5.Display.println("\nTouch for main bench");
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    delay(1000);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(1);

    Serial.println("\nfleximg Method Benchmark");
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

    if (!allocateBuffers()) {
        M5.Display.println("Alloc failed!");
        return;
    }

    Serial.printf("Free heap after alloc: %d bytes\n", ESP.getFreeHeap());

    initTestData();
    runAllBenchmarks();
}

void loop() {
    M5.update();
#if 0
    // タッチで再実行
    if (M5.Touch.getCount() > 0) {
        delay(500);  // デバウンス
        runAllBenchmarks();
    }
#endif
    // ボタンAで通常ベンチマーク
    if (M5.BtnA.wasPressed()) {
        runAllBenchmarks();
    }

    // ボタンBでアルファパターン別ベンチマーク
    if (M5.BtnB.wasPressed()) {
        runBlendUnderBenchByPattern();
    }

    delay(10);
}
