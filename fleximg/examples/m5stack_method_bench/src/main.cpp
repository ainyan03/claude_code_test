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

// ========================================================================
// ベンチマーク関数
// ========================================================================

struct BenchResult {
    const char* name;
    uint32_t totalUs;
    uint32_t perFrameUs;
    float pixelsPerUs;
};

static BenchResult results[10];
static int resultCount = 0;

bool allocateBuffers() {
    // バッファ動的確保
    srcRGBA8 = static_cast<uint8_t*>(malloc(BENCH_PIXELS * 4));
    dstRGB565 = static_cast<uint8_t*>(malloc(BENCH_PIXELS * 2));
    dstRGBA16 = static_cast<uint16_t*>(malloc(BENCH_PIXELS * 8));

    if (!srcRGBA8 || !dstRGB565 || !dstRGBA16) {
        Serial.println("ERROR: Failed to allocate buffers!");
        return false;
    }

    Serial.printf("Buffers allocated: src=%p, dst565=%p, dst16=%p\n",
                  srcRGBA8, dstRGB565, dstRGBA16);
    return true;
}

void initTestData() {
    if (!srcRGBA8) return;

    // テストデータ初期化
    // 96ピクセル周期のアルファパターン:
    // [0-31]:  A=0 (透明 32px)
    // [32-47]: A=0→255 グラデーション (16px)
    // [48-79]: A=255 (不透明 32px)
    // [80-95]: A=255→0 グラデーション (16px)
    for (int i = 0; i < BENCH_PIXELS; i++) {
        int idx = i * 4;
        srcRGBA8[idx + 0] = static_cast<uint8_t>(i & 0xFF);         // R
        srcRGBA8[idx + 1] = static_cast<uint8_t>((i >> 4) & 0xFF);  // G
        srcRGBA8[idx + 2] = static_cast<uint8_t>((i >> 8) & 0xFF);  // B

        int phase = i % 96;
        uint8_t alpha;
        if (phase < 32) {
            alpha = 0;
        } else if (phase < 48) {
            // 中間値グラデーション 16→240 (16ステップ)
            alpha = static_cast<uint8_t>(16 + (phase - 32) * 15);  // 16,31,46,...,241
        } else if (phase < 80) {
            alpha = 255;
        } else {
            // 中間値グラデーション 240→16 (16ステップ)
            alpha = static_cast<uint8_t>(16 + (95 - phase) * 15);  // 241,226,...,31,16
        }
        srcRGBA8[idx + 3] = alpha;
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
// メイン
// ========================================================================

void runAllBenchmarks() {
    resultCount = 0;
    printHeader();

    Serial.println("Running benchmarks...\n");
    M5.Display.println("Running...\n");

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

    Serial.println("\n========================================");
    Serial.println("Benchmark complete!");
    Serial.println("========================================\n");

    M5.Display.println("\nComplete!");
    M5.Display.println("Touch to rerun");
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

    // タッチで再実行
    if (M5.Touch.getCount() > 0) {
        delay(500);  // デバウンス
        runAllBenchmarks();
    }

    // ボタンAでも再実行
    if (M5.BtnA.wasPressed()) {
        runAllBenchmarks();
    }

    delay(10);
}
