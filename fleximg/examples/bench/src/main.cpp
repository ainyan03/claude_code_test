/**
 * @file main.cpp
 * @brief fleximg Unified Benchmark
 *
 * PC and M5Stack compatible benchmark for pixel format operations.
 *
 * Usage:
 *   PC:      pio run -e native && .pio/build/native/program
 *   M5Stack: pio run -e m5stack_core2 -t upload
 *
 * Commands:
 *   c [fmt]  : Conversion benchmark (toStraight/fromStraight)
 *   b [fmt]  : BlendUnder benchmark (direct vs indirect path)
 *   u [pat]  : blendUnderStraight benchmark with dst pattern variations
 *   t [grp] [bpp] : copyRowDDA benchmark (DDA scanline transform)
 *   m [pat]  : Matte composite benchmark (direct, no pipeline)
 *   p [pat]  : Matte pipeline benchmark (full node pipeline)
 *   d        : Analyze alpha distribution of test data
 *   s        : ImageBufferSet/RenderResponse move cost benchmark
 *   r        : RenderResponse move count in pipeline
 *   a        : All benchmarks
 *   l        : List available formats
 *   h        : Help
 *
 *   [fmt] = all | rgb332 | rgb565le | rgb565be | rgb888 | bgr888 | rgba8
 *   [pat] = all | trans | opaque | semi | mixed
 *   [grp] = all | h (horizontal) | v (vertical) | d (diagonal)
 *   [bpp] = all | 4 | 3 | 2 | 1
 */

// =============================================================================
// Platform Detection and Includes
// =============================================================================

#ifdef BENCH_M5STACK
    #include <M5Unified.h>
    #define BENCH_SERIAL Serial
#else
    // Native PC build
    #include <iostream>
    #include <string>
    #include <chrono>
    #include <cstring>
    #include <cstdarg>
    #include <cstdint>
#endif

// fleximg (stb-style: define FLEXIMG_IMPLEMENTATION before including headers)
#define FLEXIMG_NAMESPACE fleximg
#define FLEXIMG_DEBUG_MOVE_COUNT  // ムーブ回数カウンタ有効化
#define FLEXIMG_IMPLEMENTATION
#include "fleximg/core/common.h"
#include "fleximg/image/pixel_format.h"
#include "fleximg/image/viewport.h"
#include "fleximg/image/image_buffer.h"
#include "fleximg/image/image_buffer_set.h"
#include "fleximg/image/image_buffer_entry_pool.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/matte_node.h"
#include "fleximg/nodes/renderer_node.h"
#include "fleximg/nodes/sink_node.h"

using namespace fleximg;

// =============================================================================
// DDA Helper
// =============================================================================

// ViewPort から直接 copyRowDDA を呼び出すヘルパー
static void benchCopyRowDDA(
    void* dst,
    const ViewPort& src,
    int count,
    int_fixed srcX,
    int_fixed srcY,
    int_fixed incrX,
    int_fixed incrY
) {
    if (!src.isValid() || count <= 0) return;
    DDAParam param = { src.stride, srcX, srcY, incrX, incrY };
    if (src.formatID && src.formatID->copyRowDDA) {
        src.formatID->copyRowDDA(
            static_cast<uint8_t*>(dst),
            static_cast<const uint8_t*>(src.data),
            count,
            &param
        );
    }
}

// ViewPort から直接 copyRowDDABilinear を呼び出すヘルパー
static void benchCopyRowDDABilinear(
    void* dst,
    const ViewPort& src,
    int count,
    int_fixed srcX,
    int_fixed srcY,
    int_fixed incrX,
    int_fixed incrY
) {
    view_ops::copyRowDDABilinear(dst, src, count, srcX, srcY, incrX, incrY);
}

// =============================================================================
// Platform Abstraction
// =============================================================================

#ifdef BENCH_M5STACK

static void benchPrint(const char* str) { BENCH_SERIAL.print(str); }
static void benchPrintln(const char* str = "") { BENCH_SERIAL.println(str); }
static void benchPrintf(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    BENCH_SERIAL.print(buf);
}

static uint32_t benchMicros() { return micros(); }

[[maybe_unused]] static bool benchAvailable() { return BENCH_SERIAL.available() > 0; }

static int benchRead(char* buf, int maxLen) {
    int len = 0;
    while (BENCH_SERIAL.available() && len < maxLen - 1) {
        char c = static_cast<char>(BENCH_SERIAL.read());
        if (c == '\n' || c == '\r') {
            if (len > 0) break;
            continue;
        }
        buf[len++] = c;
    }
    buf[len] = '\0';
    return len;
}

[[maybe_unused]] static void benchDelay(int ms) { delay(ms); }

// CPUサイクルカウンタ（Xtensa LX6/LX7）
static inline uint32_t getCycleCount() {
    uint32_t count;
    asm volatile("rsr %0, ccount" : "=a"(count));
    return count;
}

static inline uint32_t getCpuFreqMHz() {
    return ESP.getCpuFreqMHz();  // 通常240MHz
}

#else  // Native PC

static void benchPrint(const char* str) { std::cout << str; }
static void benchPrintln(const char* str = "") { std::cout << str << std::endl; }
__attribute__((format(printf, 1, 2)))
static void benchPrintf(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::cout << buf;
}

static uint32_t benchMicros() {
    using namespace std::chrono;
    static auto start = high_resolution_clock::now();
    auto now = high_resolution_clock::now();
    return static_cast<uint32_t>(duration_cast<microseconds>(now - start).count());
}

[[maybe_unused]]
static bool benchAvailable() {
    // For native, we'll use a simple command-line interface
    return false;  // Will be handled differently
}

static int benchRead(char* buf, int maxLen) {
    std::string line;
    if (std::getline(std::cin, line)) {
        int len = static_cast<int>(line.length());
        if (len >= maxLen) len = maxLen - 1;
        std::memcpy(buf, line.c_str(), static_cast<size_t>(len));
        buf[len] = '\0';
        return len;
    }
    return 0;
}

[[maybe_unused]]
static void benchDelay(int) { /* no-op on PC */ }

// PC用サイクルカウンタ代替（ナノ秒ベース）
// 実際のCPUサイクルではないが、高精度計測として機能
static inline uint32_t getCycleCount() {
    using namespace std::chrono;
    static auto start = high_resolution_clock::now();
    auto now = high_resolution_clock::now();
    return static_cast<uint32_t>(duration_cast<nanoseconds>(now - start).count());
}

// PC用: 1GHzとして扱い、ナノ秒→マイクロ秒変換を簡略化
static inline uint32_t getCpuFreqMHz() {
    return 1000;  // 1GHz = 1000MHz (1 cycle = 1ns)
}

#endif

// =============================================================================
// Benchmark Configuration
// =============================================================================

#ifdef BENCH_M5STACK
    static constexpr int BENCH_PIXELS = 4096;
    static constexpr int BENCH_WIDTH = 64;    // 64x64 = 4096
    static constexpr int BENCH_HEIGHT = 64;
    static constexpr int ITERATIONS = 100;    // 最小値採用のため少数で十分
    static constexpr int WARMUP = 10;
    // Matte pipeline scaled output (2x scale)
    static constexpr int MATTE_RENDER_WIDTH = 128;
    static constexpr int MATTE_RENDER_HEIGHT = 128;
    static constexpr int MATTE_ITERATIONS = 100;
#else
    // PC is much faster, use larger pixel count for accurate measurement
    static constexpr int BENCH_PIXELS = 65536;
    static constexpr int BENCH_WIDTH = 256;   // 256x256 = 65536
    static constexpr int BENCH_HEIGHT = 256;
    static constexpr int ITERATIONS = 100;    // 最小値採用のため少数で十分
    static constexpr int WARMUP = 10;
    // Matte pipeline scaled output (2x scale)
    static constexpr int MATTE_RENDER_WIDTH = 512;
    static constexpr int MATTE_RENDER_HEIGHT = 512;
    static constexpr int MATTE_ITERATIONS = 100;
#endif

// =============================================================================
// Buffer Management
// =============================================================================

static uint8_t* bufRGBA8 = nullptr;      // RGBA8 buffer
static uint8_t* bufRGBA8_2 = nullptr;    // Second RGBA8 buffer (canvas for Straight)
static uint8_t* bufRGB888 = nullptr;     // RGB888/BGR888 buffer
static uint8_t* bufRGB565 = nullptr;     // RGB565 buffer
static uint8_t* bufRGB332 = nullptr;     // RGB332 buffer
static uint8_t* bufDDALine = nullptr;    // DDA output line buffer

// DDA benchmark: output line size (supports up to 8x scale from 64px source)
static constexpr int DDA_DST_LINE_SIZE = 512;

// Platform-specific memory allocation
// ESP32: Use internal SRAM (not PSRAM) for accurate benchmarking
// PC: Use standard malloc
#ifdef BENCH_M5STACK
    #define BENCH_MALLOC(size) heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
    #define BENCH_FREE(ptr) free(ptr)
#else
    #define BENCH_MALLOC(size) malloc(size)
    #define BENCH_FREE(ptr) free(ptr)
#endif

static bool allocateBuffers() {
    // Skip if already allocated
    if (bufRGBA8 && bufRGBA8_2 && bufRGB888 && bufRGB565 && bufRGB332 && bufDDALine) {
        return true;
    }

    bufRGBA8 = static_cast<uint8_t*>(BENCH_MALLOC(BENCH_PIXELS * 4));
    bufRGBA8_2 = static_cast<uint8_t*>(BENCH_MALLOC(BENCH_PIXELS * 4));
    bufRGB888 = static_cast<uint8_t*>(BENCH_MALLOC(BENCH_PIXELS * 3));
    bufRGB565 = static_cast<uint8_t*>(BENCH_MALLOC(BENCH_PIXELS * 2));
    bufRGB332 = static_cast<uint8_t*>(BENCH_MALLOC(BENCH_PIXELS * 1));
    bufDDALine = static_cast<uint8_t*>(BENCH_MALLOC(DDA_DST_LINE_SIZE * 4));

    if (!bufRGBA8 || !bufRGBA8_2 || !bufRGB888 || !bufRGB565 || !bufRGB332 || !bufDDALine) {
        benchPrintln("ERROR: Buffer allocation failed!");
#ifdef BENCH_M5STACK
        benchPrintf("Internal SRAM may be insufficient. Free: %u bytes\n",
                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#endif
        return false;
    }
#ifdef BENCH_M5STACK
    benchPrintf("Buffers allocated in internal SRAM (Free: %u bytes)\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#endif
    return true;
}

// =============================================================================
// Alpha Distribution Analysis
// =============================================================================

struct AlphaDistribution {
    int_fast32_t transparent;   // alpha == 0
    int_fast32_t opaque;        // alpha == 255
    int_fast32_t semiLow;       // alpha 1-127
    int_fast32_t semiHigh;      // alpha 128-254
    int_fast32_t total;

    void reset() {
        transparent = opaque = semiLow = semiHigh = 0;
        total = 0;
    }

    void count(uint8_t alpha) {
        total++;
        if (alpha == 0) transparent++;
        else if (alpha == 255) opaque++;
        else if (alpha < 128) semiLow++;
        else semiHigh++;
    }

    void print(const char* label) {
        if (total == 0) return;
        float t = static_cast<float>(total);
        benchPrintf("  %-12s: trans=%5.1f%% opaque=%5.1f%% semi=%5.1f%% (low=%5.1f%% high=%5.1f%%)\n",
            label,
            static_cast<double>(100.0f * static_cast<float>(transparent) / t),
            static_cast<double>(100.0f * static_cast<float>(opaque) / t),
            static_cast<double>(100.0f * static_cast<float>(semiLow + semiHigh) / t),
            static_cast<double>(100.0f * static_cast<float>(semiLow) / t),
            static_cast<double>(100.0f * static_cast<float>(semiHigh) / t));
    }
};

// Analyze alpha distribution of a buffer
static void analyzeAlphaDistribution(const uint8_t* buf, int_fast32_t pixelCount, AlphaDistribution& dist) {
    dist.reset();
    for (int_fast32_t i = 0; i < pixelCount; i++) {
        dist.count(buf[i * 4 + 3]);
    }
}

// =============================================================================
// Test Data Initialization
// =============================================================================

// Initialize test data with varied patterns
static void initTestData() {
    for (int i = 0; i < BENCH_PIXELS; i++) {
        // RGBA8 with alpha pattern
        int phase = i % 96;
        uint8_t alpha;
        if (phase < 32) alpha = 0;
        else if (phase < 48) alpha = static_cast<uint8_t>(16 + (phase - 32) * 15);
        else if (phase < 80) alpha = 255;
        else alpha = static_cast<uint8_t>(16 + (95 - phase) * 15);

        bufRGBA8[i * 4 + 0] = static_cast<uint8_t>(i & 0xFF);
        bufRGBA8[i * 4 + 1] = static_cast<uint8_t>((i >> 4) & 0xFF);
        bufRGBA8[i * 4 + 2] = static_cast<uint8_t>((i >> 8) & 0xFF);
        bufRGBA8[i * 4 + 3] = alpha;

        // RGB888
        bufRGB888[i * 3 + 0] = static_cast<uint8_t>((i * 37) & 0xFF);
        bufRGB888[i * 3 + 1] = static_cast<uint8_t>((i * 73) & 0xFF);
        bufRGB888[i * 3 + 2] = static_cast<uint8_t>((i * 111) & 0xFF);

        // RGB565
        uint16_t rgb565 = static_cast<uint16_t>((i * 37) & 0xFFFF);
        bufRGB565[i * 2 + 0] = static_cast<uint8_t>(rgb565 & 0xFF);
        bufRGB565[i * 2 + 1] = static_cast<uint8_t>(rgb565 >> 8);

        // RGB332
        bufRGB332[i] = static_cast<uint8_t>((i * 37) & 0xFF);
    }
}

// =============================================================================
// Dst Pattern Types for blendUnderStraight
// =============================================================================

enum class DstPattern {
    Transparent,   // All pixels alpha = 0 (best case: copy)
    Opaque,        // All pixels alpha = 255 (best case: skip)
    SemiTransparent, // All pixels alpha = 128 (worst case: full calc)
    Mixed,         // Same 96-cycle pattern as src
    COUNT
};

static const char* dstPatternNames[] = {
    "transparent",
    "opaque",
    "semi",
    "mixed"
};

static const char* dstPatternShortNames[] = {
    "trans",
    "opaque",
    "semi",
    "mixed"
};

// Initialize RGBA8 canvas with specified pattern
static void initCanvasRGBA8WithPattern(DstPattern pattern) {
    for (int i = 0; i < BENCH_PIXELS; i++) {
        uint8_t alpha;
        switch (pattern) {
            case DstPattern::Transparent:
                alpha = 0;
                break;
            case DstPattern::Opaque:
                alpha = 255;
                break;
            case DstPattern::SemiTransparent:
                alpha = 128;
                break;
            case DstPattern::Mixed:
            default: {
                // Same 96-cycle pattern as src
                int phase = i % 96;
                if (phase < 32) alpha = 0;
                else if (phase < 48) alpha = static_cast<uint8_t>(16 + (phase - 32) * 15);
                else if (phase < 80) alpha = 255;
                else alpha = static_cast<uint8_t>(16 + (95 - phase) * 15);
                break;
            }
        }
        bufRGBA8_2[i * 4 + 0] = 0;
        bufRGBA8_2[i * 4 + 1] = 255;   // Green
        bufRGBA8_2[i * 4 + 2] = 0;
        bufRGBA8_2[i * 4 + 3] = alpha;
    }
}


// =============================================================================
// Benchmark Runner
// =============================================================================

// ベンチマーク結果構造体
struct BenchResult {
    uint32_t avgCycles;     // 平均サイクル数
    uint32_t minCycles;     // 最小サイクル数（最も信頼性の高い値）
    uint32_t maxCycles;     // 最大サイクル数
    uint32_t avgMicros;     // 平均マイクロ秒
    uint32_t minMicros;     // 最小マイクロ秒（推奨：外乱の影響を受けにくい）
    uint32_t overhead;      // 計測オーバーヘッド（サイクル）
};

// キャリブレーション用：計測オーバーヘッドを測定
static uint32_t calibrateOverhead() {
    uint32_t total = 0;
    constexpr int CAL_ITERATIONS = 100;

    for (int i = 0; i < CAL_ITERATIONS; i++) {
#ifdef BENCH_M5STACK
        portDISABLE_INTERRUPTS();
#endif
        uint32_t start = getCycleCount();
        // 空の計測（オーバーヘッドのみ）
        uint32_t end = getCycleCount();
#ifdef BENCH_M5STACK
        portENABLE_INTERRUPTS();
#endif
        total += (end - start);
    }

    return total / CAL_ITERATIONS;
}

// グローバルオーバーヘッド値（初回計測時に設定）
static uint32_t g_measureOverhead = 0;

// 厳密計測版ベンチマーク（CPUサイクルカウンタ + 割り込み禁止）
template<typename Func>
static BenchResult runBenchmarkCycles(Func func, int iterations = ITERATIONS) {
    BenchResult result = {};

    // オーバーヘッドの初回計測
    if (g_measureOverhead == 0) {
        g_measureOverhead = calibrateOverhead();
    }
    result.overhead = g_measureOverhead;

    // ウォームアップ
    for (int i = 0; i < WARMUP; i++) {
        func();
    }

    uint64_t totalCycles = 0;
    result.minCycles = UINT32_MAX;
    result.maxCycles = 0;

    for (int i = 0; i < iterations; i++) {
        // 定期的にタスク切り替え（ウォッチドッグ対策）
#ifdef BENCH_M5STACK
        if ((i & 0xFF) == 0) {
            vTaskDelay(1);  // タスク切り替えを許可
        }
        taskYIELD();
        portDISABLE_INTERRUPTS();
#endif
        uint32_t start = getCycleCount();

        func();

        uint32_t end = getCycleCount();
#ifdef BENCH_M5STACK
        portENABLE_INTERRUPTS();
#endif

        // カウンタラップアラウンド対応（32bit減算は自動的に正しい差分を計算）
        uint32_t elapsed = end - start;

        // オーバーヘッド補正
        if (elapsed > g_measureOverhead) {
            elapsed -= g_measureOverhead;
        }

        totalCycles += elapsed;

        if (elapsed < result.minCycles) result.minCycles = elapsed;
        if (elapsed > result.maxCycles) result.maxCycles = elapsed;
    }

    result.avgCycles = static_cast<uint32_t>(totalCycles / static_cast<uint64_t>(iterations));

    // サイクル→マイクロ秒変換: cycles / MHz = microseconds
    uint32_t freqMHz = getCpuFreqMHz();
    result.avgMicros = result.avgCycles / freqMHz;
    result.minMicros = result.minCycles / freqMHz;

    return result;
}

// 後方互換性のためのラッパー（既存コードとの互換）
// minMicrosを返す：外乱の影響を受けにくい最小値を採用
template<typename Func>
static uint32_t runBenchmark(Func func) {
    BenchResult result = runBenchmarkCycles(func);
    return result.minMicros;
}

// 詳細結果を表示するヘルパー（将来の拡張用）
[[maybe_unused]]
static void printBenchResultDetail(const char* label, const BenchResult& r) {
    uint32_t freqMHz = getCpuFreqMHz();
    benchPrintf("  %-20s: %6u us (min=%u avg=%u max=%u cycles, @%uMHz)\n",
                label,
                r.minMicros,
                r.minCycles,
                r.avgCycles,
                r.maxCycles,
                freqMHz);
}

// キャリブレーション情報を表示
static void printCalibrationInfo() {
    if (g_measureOverhead == 0) {
        g_measureOverhead = calibrateOverhead();
    }
    uint32_t freqMHz = getCpuFreqMHz();
    benchPrintf("Calibration: overhead=%u cycles (~%u ns), CPU=%u MHz\n",
                g_measureOverhead,
                g_measureOverhead * 1000 / freqMHz,
                freqMHz);
}

// =============================================================================
// Format Registry
// =============================================================================

struct FormatInfo {
    const char* name;
    const char* shortName;
    const PixelFormatDescriptor* format;
    uint8_t* srcBuffer;
    int bytesPerPixel;
};

static FormatInfo formats[] = {
    {"RGB332",          "rgb332",   &BuiltinFormats::RGB332,              nullptr, 1},
    {"RGB565_LE",       "rgb565le", &BuiltinFormats::RGB565_LE,           nullptr, 2},
    {"RGB565_BE",       "rgb565be", &BuiltinFormats::RGB565_BE,           nullptr, 2},
    {"RGB888",          "rgb888",   &BuiltinFormats::RGB888,              nullptr, 3},
    {"BGR888",          "bgr888",   &BuiltinFormats::BGR888,              nullptr, 3},
    {"RGBA8_Straight",  "rgba8",    &BuiltinFormats::RGBA8_Straight,      nullptr, 4},
};
static constexpr int NUM_FORMATS = sizeof(formats) / sizeof(formats[0]);

static void initFormatBuffers() {
    formats[0].srcBuffer = bufRGB332;   // RGB332
    formats[1].srcBuffer = bufRGB565;   // RGB565_LE
    formats[2].srcBuffer = bufRGB565;   // RGB565_BE
    formats[3].srcBuffer = bufRGB888;   // RGB888
    formats[4].srcBuffer = bufRGB888;   // BGR888
    formats[5].srcBuffer = bufRGBA8;    // RGBA8_Straight
}

static int findFormat(const char* name) {
    for (int i = 0; i < NUM_FORMATS; i++) {
        if (strcmp(formats[i].shortName, name) == 0) return i;
    }
    return -1;
}

// =============================================================================
// Conversion Benchmark
// =============================================================================

static void benchConvertFormat(int idx) {
    const auto& fmt = formats[idx];
    benchPrintf("%-16s", fmt.name);

    // toStraight
    if (fmt.format->toStraight) {
        uint32_t us = runBenchmark([&]() {
            fmt.format->toStraight(bufRGBA8, fmt.srcBuffer, BENCH_PIXELS, nullptr);
        });
        benchPrintf(" %6u", us);
    } else {
        benchPrint("      -");
    }

    // fromStraight
    if (fmt.format->fromStraight) {
        uint32_t us = runBenchmark([&]() {
            fmt.format->fromStraight(fmt.srcBuffer, bufRGBA8, BENCH_PIXELS, nullptr);
        });
        benchPrintf(" %6u", us);
    } else {
        benchPrint("      -");
    }

    benchPrintln();
}

static void runConvertBenchmark(const char* fmtName) {
    benchPrintln();
    benchPrintln("=== Conversion Benchmark ===");
    benchPrintf("Pixels: %d, Iterations: %d\n", BENCH_PIXELS, ITERATIONS);
    benchPrintln();
    benchPrintln("Format           toStr  frStr (us/frame)");
    benchPrintln("---------------- ------ ------");

    if (strcmp(fmtName, "all") == 0) {
        for (int i = 0; i < NUM_FORMATS; i++) {
            benchConvertFormat(i);
        }
    } else {
        int idx = findFormat(fmtName);
        if (idx >= 0) {
            benchConvertFormat(idx);
        } else {
            benchPrintf("Unknown format: %s\n", fmtName);
        }
    }
    benchPrintln();
}

// =============================================================================
// BlendUnder Benchmark (Direct vs Indirect)
// =============================================================================

// Straight mode: compare direct blendUnderStraight vs indirect (toStraight + blend)
static void benchBlendFormatStraight(int idx) {
    const auto& fmt = formats[idx];

    // Indirect path requires toStraight
    if (!fmt.format->toStraight) {
        benchPrintf("%-16s   (no toStraight)\n", fmt.name);
        return;
    }

    bool hasDirect = (fmt.format->blendUnderStraight != nullptr);

    benchPrintf("%-16s", fmt.name);

    // Direct path (if available)
    uint32_t directUs = 0;
    if (hasDirect) {
        directUs = runBenchmark([&]() {
            initCanvasRGBA8WithPattern(DstPattern::SemiTransparent);
            fmt.format->blendUnderStraight(bufRGBA8_2, fmt.srcBuffer, BENCH_PIXELS, nullptr);
        });
        benchPrintf(" %6u", directUs);
    } else {
        benchPrint("      -");
    }

    // Indirect path (toStraight + RGBA8_Straight blend)
    uint32_t indirectUs = runBenchmark([&]() {
        initCanvasRGBA8WithPattern(DstPattern::SemiTransparent);
        fmt.format->toStraight(bufRGBA8, fmt.srcBuffer, BENCH_PIXELS, nullptr);
        BuiltinFormats::RGBA8_Straight.blendUnderStraight(
            bufRGBA8_2, bufRGBA8, BENCH_PIXELS, nullptr);
    });
    benchPrintf(" %6u", indirectUs);

    // Ratio (only if direct path exists)
    if (hasDirect && directUs > 0) {
        double ratio = static_cast<double>(indirectUs) / static_cast<double>(directUs);
        benchPrintf("  %5.2fx\n", ratio);
    } else {
        benchPrintln("      -");
    }
}

static void runBlendBenchmark(const char* fmtName) {
    benchPrintln();
    benchPrintln("=== BlendUnder Benchmark (Direct vs Indirect) ===");
    benchPrintf("Pixels: %d, Iterations: %d\n", BENCH_PIXELS, ITERATIONS);
    benchPrintln();
    benchPrintln("Format           Direct Indir  Ratio");
    benchPrintln("---------------- ------ ------ ------");

    if (strcmp(fmtName, "all") == 0) {
        for (int i = 0; i < NUM_FORMATS; i++) {
            benchBlendFormatStraight(i);
        }
    } else {
        int idx = findFormat(fmtName);
        if (idx >= 0) {
            benchBlendFormatStraight(idx);
        } else {
            benchPrintf("Unknown format: %s\n", fmtName);
        }
    }
    benchPrintln("(Ratio > 1 means Direct is faster)");
    benchPrintln();
}

// =============================================================================
// blendUnderStraight Benchmark with Dst Pattern Variations
// =============================================================================

// Count expected processing paths based on src and dst alpha
struct PathCounts {
    int dstSkip;     // dstA == 255 (skip entirely)
    int srcSkip;     // srcA == 0 (skip)
    int copy;        // dstA == 0 (simple copy)
    int fullCalc;    // all other cases (full under-blend calculation)
    int total;

    void reset() {
        dstSkip = srcSkip = copy = fullCalc = 0;
        total = 0;
    }

    void analyze(const uint8_t* src, const uint8_t* dst, int pixelCount) {
        reset();
        for (int i = 0; i < pixelCount; i++) {
            total++;
            uint8_t dstA = dst[i * 4 + 3];
            uint8_t srcA = src[i * 4 + 3];

            if (dstA == 255) {
                dstSkip++;
            } else if (srcA == 0) {
                srcSkip++;
            } else if (dstA == 0) {
                copy++;
            } else {
                fullCalc++;
            }
        }
    }

    void print() {
        if (total == 0) return;
        benchPrintf("    Paths: dstSkip=%5.1f%% srcSkip=%5.1f%% copy=%5.1f%% fullCalc=%5.1f%%\n",
            static_cast<double>(100.0f * dstSkip / total),
            static_cast<double>(100.0f * srcSkip / total),
            static_cast<double>(100.0f * copy / total),
            static_cast<double>(100.0f * fullCalc / total));
    }
};

static void runBlendUnderStraightBenchmark(DstPattern pattern) {
    const char* patternName = dstPatternNames[static_cast<int>(pattern)];

    // Initialize dst canvas with pattern
    initCanvasRGBA8WithPattern(pattern);

    // Analyze path distribution
    PathCounts paths;
    paths.analyze(bufRGBA8, bufRGBA8_2, BENCH_PIXELS);

    benchPrintf("  Pattern: %-12s", patternName);

    // Run benchmark
    uint32_t us = runBenchmark([&]() {
        // Restore dst pattern before each blend
        initCanvasRGBA8WithPattern(pattern);
        BuiltinFormats::RGBA8_Straight.blendUnderStraight(
            bufRGBA8_2, bufRGBA8, BENCH_PIXELS, nullptr);
    });

    // Calculate ns per pixel
    double nsPerPixel = (static_cast<double>(us) * 1000.0) / BENCH_PIXELS;

    benchPrintf(" %6u us  %6.2f ns/px\n", us, nsPerPixel);
    paths.print();
}

static void runBlendUnderStraightBenchmarks(const char* patternArg) {
    benchPrintln();
    benchPrintln("=== blendUnderStraight Benchmark (Dst Pattern Variations) ===");
    benchPrintf("Pixels: %d, Iterations: %d\n", BENCH_PIXELS, ITERATIONS);
    benchPrintln();

    // Show src alpha distribution
    AlphaDistribution srcDist;
    analyzeAlphaDistribution(bufRGBA8, BENCH_PIXELS, srcDist);
    benchPrintln("Source buffer alpha distribution:");
    srcDist.print("src");
    benchPrintln();

    benchPrintln("Results:");

    if (strcmp(patternArg, "all") == 0) {
        for (int i = 0; i < static_cast<int>(DstPattern::COUNT); i++) {
            runBlendUnderStraightBenchmark(static_cast<DstPattern>(i));
        }
    } else {
        // Find matching pattern
        bool found = false;
        for (int i = 0; i < static_cast<int>(DstPattern::COUNT); i++) {
            if (strcmp(patternArg, dstPatternShortNames[i]) == 0 ||
                strcmp(patternArg, dstPatternNames[i]) == 0) {
                runBlendUnderStraightBenchmark(static_cast<DstPattern>(i));
                found = true;
                break;
            }
        }
        if (!found) {
            benchPrintf("Unknown pattern: %s\n", patternArg);
            benchPrintln("Available patterns: all | trans | opaque | semi | mixed");
        }
    }

    benchPrintln();
}

// =============================================================================
// Alpha Distribution Analysis Command
// =============================================================================

static void runAlphaDistributionAnalysis() {
    benchPrintln();
    benchPrintln("=== Alpha Distribution Analysis ===");
    benchPrintf("Pixels: %d\n", BENCH_PIXELS);
    benchPrintln();

    // Source buffer analysis
    AlphaDistribution srcDist;
    analyzeAlphaDistribution(bufRGBA8, BENCH_PIXELS, srcDist);
    benchPrintln("Source buffer (bufRGBA8):");
    srcDist.print("src");
    benchPrintln();

    // Dst pattern analysis
    benchPrintln("Destination patterns:");
    for (int i = 0; i < static_cast<int>(DstPattern::COUNT); i++) {
        DstPattern pat = static_cast<DstPattern>(i);
        initCanvasRGBA8WithPattern(pat);
        AlphaDistribution dstDist;
        analyzeAlphaDistribution(bufRGBA8_2, BENCH_PIXELS, dstDist);
        dstDist.print(dstPatternNames[i]);
    }
    benchPrintln();

    // Expected processing path analysis for each dst pattern
    benchPrintln("Expected processing paths (src x dst combinations):");
    benchPrintln("  dstSkip:  dst is opaque, no blending needed");
    benchPrintln("  srcSkip:  src is transparent, no change to dst");
    benchPrintln("  copy:     dst is transparent, simple copy from src");
    benchPrintln("  fullCalc: semi-transparent, requires full calculation");
    benchPrintln();

    for (int i = 0; i < static_cast<int>(DstPattern::COUNT); i++) {
        DstPattern pat = static_cast<DstPattern>(i);
        initCanvasRGBA8WithPattern(pat);
        PathCounts paths;
        paths.analyze(bufRGBA8, bufRGBA8_2, BENCH_PIXELS);
        benchPrintf("  %-12s: dstSkip=%5.1f%% srcSkip=%5.1f%% copy=%5.1f%% fullCalc=%5.1f%%\n",
            dstPatternNames[i],
            static_cast<double>(100.0f * paths.dstSkip / paths.total),
            static_cast<double>(100.0f * paths.srcSkip / paths.total),
            static_cast<double>(100.0f * paths.copy / paths.total),
            static_cast<double>(100.0f * paths.fullCalc / paths.total));
    }
    benchPrintln();
}

// =============================================================================
// copyRowDDA Benchmark
// =============================================================================

#ifdef BENCH_M5STACK
    static constexpr int DDA_SRC_SIZE = 64;    // 64x64 = 4096 pixels = BENCH_PIXELS
#else
    static constexpr int DDA_SRC_SIZE = 256;   // 256x256 = 65536 pixels = BENCH_PIXELS
#endif

struct DDATestCase {
    const char* name;
    const char* group;   // "h", "v", "d"
    int_fixed incrX;
    int_fixed incrY;
};

static const DDATestCase ddaTests[] = {
    // Horizontal only (incrY == 0) — scale without rotation
    {"H 1:1",  "h", INT_FIXED_ONE,      0},
    {"H x2",   "h", INT_FIXED_ONE / 2,  0},               // 2x zoom
    {"H x4",   "h", INT_FIXED_ONE / 4,  0},               // 4x zoom
    {"H x8",   "h", INT_FIXED_ONE / 8,  0},               // 8x zoom
    {"H /2",   "h", INT_FIXED_ONE * 2,  0},               // 2x shrink
    // Vertical only (incrX == 0) — 90-degree rotation equivalent
    {"V 1:1",  "v", 0,                  INT_FIXED_ONE},
    {"V x2",   "v", 0,                  INT_FIXED_ONE / 2},
    {"V x4",   "v", 0,                  INT_FIXED_ONE / 4},
    {"V /2",   "v", 0,                  INT_FIXED_ONE * 2},
    // Diagonal (both nonzero) — rotation case
    {"D 1:1",  "d", 46341,              46341},            // cos(45)*65536
    {"D x2",   "d", 23170,              23170},            // cos(45)*65536/2
    {"D x4",   "d", 11585,              11585},            // cos(45)*65536/4
};
static constexpr int NUM_DDA_TESTS = sizeof(ddaTests) / sizeof(ddaTests[0]);

// Compute safe pixel count per copyRowDDA call within source bounds
static int computeDDASafeCount(int_fixed incrX, int_fixed incrY) {
    int count = DDA_SRC_SIZE * DDA_SRC_SIZE;
    if (incrX > 0) {
        count = std::min(count, static_cast<int>(
            static_cast<int64_t>(DDA_SRC_SIZE - 1) * INT_FIXED_ONE / incrX + 1));
    }
    if (incrY > 0) {
        count = std::min(count, static_cast<int>(
            static_cast<int64_t>(DDA_SRC_SIZE - 1) * INT_FIXED_ONE / incrY + 1));
    }
    return count;
}

// BPP configurations for multi-format DDA benchmark
struct DDABppConfig {
    int bpp;
    const char* label;
    PixelFormatID formatID;
};

static const DDABppConfig ddaBppConfigs[] = {
    {4, "BPP4", PixelFormatIDs::RGBA8_Straight},
    {3, "BPP3", PixelFormatIDs::RGB888},
    {2, "BPP2", PixelFormatIDs::RGB565_LE},
    {1, "BPP1", PixelFormatIDs::RGB332},
};
static constexpr int NUM_DDA_BPP = sizeof(ddaBppConfigs) / sizeof(ddaBppConfigs[0]);

static uint8_t* getDDASourceBuffer(int bpp) {
    switch (bpp) {
        case 3: return bufRGB888;
        case 2: return bufRGB565;
        case 1: return bufRGB332;
        default: return bufRGBA8;
    }
}

// Run single DDA test, return ns/px
static double runDDATest(const ViewPort& srcVP, int bpp, int testIdx) {
    const auto& test = ddaTests[testIdx];

    // 出力ピクセル数を計算（ソース範囲内に収まる最大数、ラインバッファ上限も考慮）
    int count = computeDDASafeCount(test.incrX, test.incrY);
    count = std::min(count, DDA_DST_LINE_SIZE);

    // 総ピクセル数を一定に保つため、行数を調整
    int numRows = (BENCH_PIXELS * 4) / count;  // 4倍のピクセル数を処理
    if (numRows < 1) numRows = 1;
    int totalPixels = count * numRows;

    uint32_t us = runBenchmark([&]() {
        uint8_t* dst = bufDDALine;
        for (int row = 0; row < numRows; row++) {
            int_fixed srcX = 0;
            int_fixed srcY = 0;
            if (test.incrY == 0 && test.incrX != 0) {
                // Horizontal: vary source row
                srcY = static_cast<int_fixed>((row % DDA_SRC_SIZE) << INT_FIXED_SHIFT);
            } else if (test.incrX == 0 && test.incrY != 0) {
                // Vertical: vary source column
                srcX = static_cast<int_fixed>((row % DDA_SRC_SIZE) << INT_FIXED_SHIFT);
            }
            // Diagonal: always start from (0, 0)

            benchCopyRowDDA(dst, srcVP, count, srcX, srcY,
                                 test.incrX, test.incrY);
        }
    });

    return (static_cast<double>(us) * 1000.0) / totalPixels;
}

// Run single DDA Bilinear test, return ns/px (RGBA8888 only)
static double runDDATestBilinear(const ViewPort& srcVP, int testIdx) {
    const auto& test = ddaTests[testIdx];

    // 出力ピクセル数を計算（ソース範囲内に収まる最大数、ラインバッファ上限も考慮）
    int count = computeDDASafeCount(test.incrX, test.incrY);
    count = std::min(count, DDA_DST_LINE_SIZE);

    // 総ピクセル数を一定に保つため、行数を調整
    int numRows = (BENCH_PIXELS * 4) / count;  // 4倍のピクセル数を処理
    if (numRows < 1) numRows = 1;
    int totalPixels = count * numRows;

    uint32_t us = runBenchmark([&]() {
        uint8_t* dst = bufDDALine;
        for (int row = 0; row < numRows; row++) {
            int_fixed srcX = 0;
            int_fixed srcY = 0;
            if (test.incrY == 0 && test.incrX != 0) {
                // Horizontal: vary source row
                srcY = static_cast<int_fixed>((row % DDA_SRC_SIZE) << INT_FIXED_SHIFT);
            } else if (test.incrX == 0 && test.incrY != 0) {
                // Vertical: vary source column
                srcX = static_cast<int_fixed>((row % DDA_SRC_SIZE) << INT_FIXED_SHIFT);
            }
            // Diagonal: always start from (0, 0)

            benchCopyRowDDABilinear(dst, srcVP, count, srcX, srcY,
                                    test.incrX, test.incrY);
        }
    });

    return (static_cast<double>(us) * 1000.0) / totalPixels;
}

static void runDDABenchmark(const char* args) {
    // Parse: "[grp] [bpp]"  grp = all|h|v|d  bpp = all|4|3|2|1
    char groupStr[16] = "all";
    char bppStr[16] = "all";
    {
        const char* p = args;
        int len = 0;
        while (*p && *p != ' ' && len < 15) { groupStr[len++] = *p++; }
        groupStr[len] = '\0';
        while (*p == ' ') p++;
        if (*p) {
            len = 0;
            while (*p && *p != ' ' && len < 15) { bppStr[len++] = *p++; }
            bppStr[len] = '\0';
        }
    }

    // Parse BPP filter (0 = all)
    int bppFilter = 0;
    if (strcmp(bppStr, "all") != 0) {
        if (bppStr[0] >= '1' && bppStr[0] <= '4' && bppStr[1] == '\0') {
            bppFilter = bppStr[0] - '0';
        } else {
            benchPrintf("Unknown bpp: %s\n", bppStr);
            benchPrintln("Available: all | 4 | 3 | 2 | 1");
            return;
        }
    }

    // Build active BPP list
    int activeBppIdx[NUM_DDA_BPP];
    int numActive = 0;
    for (int b = 0; b < NUM_DDA_BPP; b++) {
        if (bppFilter == 0 || ddaBppConfigs[b].bpp == bppFilter) {
            activeBppIdx[numActive++] = b;
        }
    }

    bool allGroups = (strcmp(groupStr, "all") == 0);

    benchPrintln();
    benchPrintln("=== copyRowDDA Benchmark ===");
    benchPrintf("Source: %dx%d, Iterations: %d\n", DDA_SRC_SIZE, DDA_SRC_SIZE, ITERATIONS);
    benchPrintln();

    if (numActive == 1) {
        // Single-BPP mode: detailed output with us/frm
        const auto& cfg = ddaBppConfigs[activeBppIdx[0]];
        ViewPort srcVP(getDDASourceBuffer(cfg.bpp),
                       DDA_SRC_SIZE, DDA_SRC_SIZE, cfg.formatID);

        const bool showBilinear = (cfg.bpp == 4);  // Bilinear is RGBA8 only

        benchPrintf("Format: %s\n", cfg.formatID->name);
        benchPrintln();
        if (showBilinear) {
            benchPrintf("%-12s %8s %8s %6s\n", "Test", "Nearest", "Bilinear", "Ratio");
            benchPrintln("------------ -------- -------- ------");
        } else {
            benchPrintf("%-12s %7s %7s\n", "Test", "us/frm", "ns/px");
            benchPrintln("------------ ------- -------");
        }

        bool found = false;
        for (int i = 0; i < NUM_DDA_TESTS; i++) {
            if (allGroups || strcmp(ddaTests[i].group, groupStr) == 0) {
                int count = computeDDASafeCount(ddaTests[i].incrX, ddaTests[i].incrY);
                int numRows = BENCH_PIXELS / count;
                if (numRows < 1) numRows = 1;
                int totalPixels = count * numRows;

                double nsNearest = runDDATest(srcVP, cfg.bpp, i);

                if (showBilinear) {
                    double nsBilinear = runDDATestBilinear(srcVP, i);
                    double ratio = nsBilinear / nsNearest;
                    benchPrintf("%-12s %8.2f %8.2f %5.1fx\n",
                        ddaTests[i].name, nsNearest, nsBilinear, ratio);
                } else {
                    auto us = static_cast<uint32_t>(nsNearest * totalPixels / 1000.0 + 0.5);
                    benchPrintf("%-12s %7u %7.2f  (%d x %d rows)\n",
                        ddaTests[i].name, us, nsNearest, count, numRows);
                }
                found = true;
            }
        }

        if (!found) {
            benchPrintf("Unknown group: %s\n", groupStr);
            benchPrintln("Available: all | h | v | d");
        }
    } else {
        // Multi-BPP mode: ns/px columns side by side
        benchPrintln("[Nearest interpolation]");
        benchPrintf("%-12s", "Test");
        for (int b = 0; b < numActive; b++) {
            benchPrintf(" %7s", ddaBppConfigs[activeBppIdx[b]].label);
        }
        benchPrintln();
        benchPrint("------------");
        for (int b = 0; b < numActive; b++) {
            benchPrint(" -------");
        }
        benchPrintln();

        // 4BPP結果を保存（Bilinear比較用）
        double nearest4bpp[NUM_DDA_TESTS] = {};
        bool has4bpp = false;

        bool found = false;
        for (int i = 0; i < NUM_DDA_TESTS; i++) {
            if (allGroups || strcmp(ddaTests[i].group, groupStr) == 0) {
                benchPrintf("%-12s", ddaTests[i].name);
                for (int b = 0; b < numActive; b++) {
                    const auto& cfg = ddaBppConfigs[activeBppIdx[b]];
                    ViewPort srcVP(getDDASourceBuffer(cfg.bpp),
                                   DDA_SRC_SIZE, DDA_SRC_SIZE, cfg.formatID);
                    double ns = runDDATest(srcVP, cfg.bpp, i);
                    benchPrintf(" %7.2f", ns);
                    if (cfg.bpp == 4) {
                        nearest4bpp[i] = ns;
                        has4bpp = true;
                    }
                }
                benchPrintln();
                found = true;
            }
        }

        if (!found) {
            benchPrintf("Unknown group: %s\n", groupStr);
            benchPrintln("Available: all | h | v | d");
        }

        // 4BPPが含まれていればBilinear結果を追加
        if (has4bpp && found) {
            benchPrintln();
            benchPrintln("[Bilinear interpolation - RGBA8 only]");
            benchPrintf("%-12s %8s %8s %6s\n", "Test", "Nearest", "Bilinear", "Ratio");
            benchPrintln("------------ -------- -------- ------");

            ViewPort srcVP(getDDASourceBuffer(4),
                           DDA_SRC_SIZE, DDA_SRC_SIZE, PixelFormatIDs::RGBA8_Straight);

            for (int i = 0; i < NUM_DDA_TESTS; i++) {
                if (allGroups || strcmp(ddaTests[i].group, groupStr) == 0) {
                    double nsBilinear = runDDATestBilinear(srcVP, i);
                    double ratio = nsBilinear / nearest4bpp[i];
                    benchPrintf("%-12s %8.2f %8.2f %5.1fx\n",
                        ddaTests[i].name, nearest4bpp[i], nsBilinear, ratio);
                }
            }
        }
    }

    benchPrintln();
}

// =============================================================================
// Matte Mask Skip Benchmark
// =============================================================================

// Mask patterns for matte skip benchmark
enum class MaskPattern {
    AllZero,        // All 0 (early return, background only)
    All255,         // All 255 (foreground only)
    LeftZero,       // Left half 0, right half 255 (skip optimization)
    RightZero,      // Left half 255, right half 0
    Gradient,       // 0 to 255 gradient (worst case: many runs)
    Random,         // Random values (realistic case)
    COUNT
};

static const char* maskPatternNames[] = {
    "all_zero",
    "all_255",
    "left_zero",
    "right_zero",
    "gradient",
    "random"
};

static const char* maskPatternShortNames[] = {
    "zero",
    "255",
    "left",
    "right",
    "grad",
    "rand"
};

// Mask buffer for matte benchmark
static uint8_t* bufMask = nullptr;

static bool allocateMaskBuffer() {
    if (bufMask) return true;
    bufMask = static_cast<uint8_t*>(BENCH_MALLOC(BENCH_PIXELS));
    if (!bufMask) {
        benchPrintln("ERROR: Mask buffer allocation failed!");
        return false;
    }
    return true;
}

// Initialize mask with specified pattern (1D - single row)
static void initMaskWithPattern1D(uint8_t* dst, MaskPattern pattern, int width) {
    switch (pattern) {
        case MaskPattern::AllZero:
            std::memset(dst, 0, static_cast<size_t>(width));
            break;
        case MaskPattern::All255:
            std::memset(dst, 255, static_cast<size_t>(width));
            break;
        case MaskPattern::LeftZero:
            std::memset(dst, 0, static_cast<size_t>(width / 2));
            std::memset(dst + width / 2, 255, static_cast<size_t>(width - width / 2));
            break;
        case MaskPattern::RightZero:
            std::memset(dst, 255, static_cast<size_t>(width / 2));
            std::memset(dst + width / 2, 0, static_cast<size_t>(width - width / 2));
            break;
        case MaskPattern::Gradient:
            for (int i = 0; i < width; i++) {
                dst[i] = static_cast<uint8_t>((i * 255) / (width - 1));
            }
            break;
        case MaskPattern::Random:
        default:
            // Pseudo-random pattern using simple LCG
            {
                uint32_t seed = 12345;
                for (int i = 0; i < width; i++) {
                    seed = seed * 1103515245 + 12345;
                    dst[i] = static_cast<uint8_t>((seed >> 16) & 0xFF);
                }
            }
            break;
    }
}

// Initialize mask with specified pattern (for matte mask skip benchmark - 1D)
[[maybe_unused]] static void initMaskWithPattern(MaskPattern pattern, int width) {
    initMaskWithPattern1D(bufMask, pattern, width);
}

// Initialize 2D mask buffer with specified pattern (for pipeline benchmark)
static void initMask2DWithPattern(MaskPattern pattern, int width, int height) {
    for (int y = 0; y < height; y++) {
        initMaskWithPattern1D(bufMask + y * width, pattern, width);
    }
}

// =============================================================================
// Matte Benchmark Common
// =============================================================================

// Output buffer for matte benchmarks
static uint8_t* bufOutput = nullptr;
static size_t bufOutputSize = 0;

static bool allocateOutputBuffer(size_t requiredSize) {
    if (bufOutput && bufOutputSize >= requiredSize) return true;
    // Free existing if too small
    if (bufOutput) {
        BENCH_FREE(bufOutput);
        bufOutput = nullptr;
        bufOutputSize = 0;
    }
    bufOutput = static_cast<uint8_t*>(BENCH_MALLOC(requiredSize));
    if (!bufOutput) {
        benchPrintln("ERROR: Output buffer allocation failed!");
        return false;
    }
    bufOutputSize = requiredSize;
    return true;
}

// Initialize foreground buffer with a pattern
static void initForegroundBuffer() {
    // Red-ish pattern with varying alpha
    for (int i = 0; i < BENCH_PIXELS; i++) {
        bufRGBA8[i * 4 + 0] = 255;  // R
        bufRGBA8[i * 4 + 1] = static_cast<uint8_t>((i * 3) & 0xFF);  // G
        bufRGBA8[i * 4 + 2] = 50;   // B
        bufRGBA8[i * 4 + 3] = 255;  // A (opaque)
    }
}

// Initialize background buffer with a pattern
static void initBackgroundBuffer() {
    // Blue-ish pattern with varying alpha
    for (int i = 0; i < BENCH_PIXELS; i++) {
        bufRGBA8_2[i * 4 + 0] = 50;   // R
        bufRGBA8_2[i * 4 + 1] = static_cast<uint8_t>((i * 5) & 0xFF);  // G
        bufRGBA8_2[i * 4 + 2] = 255;  // B
        bufRGBA8_2[i * 4 + 3] = 255;  // A (opaque)
    }
}

// =============================================================================
// Matte Composite Benchmark (applyMatteOverlay equivalent)
// =============================================================================

// Apply matte composite for 2D image (overlay approach)
// Step 1: Copy bg to output
// Step 2: Apply fg/mask overlay using core MatteNode implementation
static void matteComposite2D(uint8_t* outBuf, int width, int height, int outStride,
                             const uint8_t* fgBuf, int fgStride,
                             const uint8_t* bgBuf, int bgStride,
                             const uint8_t* maskBuf, int maskStride) {
    const size_t rowBytes = static_cast<size_t>(width) * 4;

    for (int y = 0; y < height; ++y) {
        uint8_t* outRow = outBuf + y * outStride;
        const uint8_t* bgRow = bgBuf + y * bgStride;
        const uint8_t* fgRow = fgBuf + y * fgStride;
        const uint8_t* maskRow = maskBuf + y * maskStride;

        // Step 1: Copy bg to output
        std::memcpy(outRow, bgRow, rowBytes);

        // Step 2: Apply fg/mask overlay (using core implementation)
        MatteNode::benchProcessRowWithFg(outRow, maskRow, fgRow, width);
    }
}

static void runMatteCompositeBenchmark(MaskPattern pattern) {
    const char* patternName = maskPatternNames[static_cast<int>(pattern)];

    // Initialize buffers
    initForegroundBuffer();
    initBackgroundBuffer();
    initMask2DWithPattern(pattern, BENCH_WIDTH, BENCH_HEIGHT);

    // Benchmark
    uint32_t us = runBenchmark([&]() {
        matteComposite2D(bufOutput, BENCH_WIDTH, BENCH_HEIGHT, BENCH_WIDTH * 4,
                         bufRGBA8, BENCH_WIDTH * 4,
                         bufRGBA8_2, BENCH_WIDTH * 4,
                         bufMask, BENCH_WIDTH);
    });

    // Calculate throughput
    int pixelsPerIteration = BENCH_WIDTH * BENCH_HEIGHT;
    float nsPerPx = static_cast<float>(us) * 1000.0f / static_cast<float>(pixelsPerIteration);
    float mpps = static_cast<float>(pixelsPerIteration) / static_cast<float>(us);

    benchPrintf("  %-12s %6u us  %5.1f ns/px  %5.2f Mpix/s\n",
                patternName, us, static_cast<double>(nsPerPx), static_cast<double>(mpps));
}

static void runMatteCompositeBenchmarks(const char* patternArg) {
    if (!allocateBuffers()) return;
    if (!allocateMaskBuffer()) return;
    // Use BENCH_WIDTH x BENCH_HEIGHT for composite benchmark (smaller than pipeline)
    size_t outputSize = static_cast<size_t>(BENCH_WIDTH) * BENCH_HEIGHT * 4;
    if (!allocateOutputBuffer(outputSize)) return;

    benchPrintln();
    benchPrintln("=== Matte Composite Benchmark ===");
    benchPrintf("Image: %dx%d, Iterations: %d\n", BENCH_WIDTH, BENCH_HEIGHT, ITERATIONS);
    benchPrintln();
    benchPrintln("Direct applyMatteComposite (no pipeline overhead)");
    benchPrintln();

    if (strcmp(patternArg, "all") == 0) {
        for (int i = 0; i < static_cast<int>(MaskPattern::COUNT); i++) {
            runMatteCompositeBenchmark(static_cast<MaskPattern>(i));
        }
    } else {
        bool found = false;
        for (int i = 0; i < static_cast<int>(MaskPattern::COUNT); i++) {
            if (strcmp(patternArg, maskPatternShortNames[i]) == 0 ||
                strcmp(patternArg, maskPatternNames[i]) == 0) {
                runMatteCompositeBenchmark(static_cast<MaskPattern>(i));
                found = true;
                break;
            }
        }
        if (!found) {
            benchPrintf("Unknown pattern: %s\n", patternArg);
            benchPrintln("Available: all | zero | 255 | left | right | grad | rand");
        }
    }

    benchPrintln();
}

// =============================================================================
// Matte Pipeline Benchmark
// =============================================================================

static void runMattePipelineBenchmark(MaskPattern pattern) {
    const char* patternName = maskPatternNames[static_cast<int>(pattern)];

    // Initialize buffers
    initForegroundBuffer();
    initBackgroundBuffer();
    initMask2DWithPattern(pattern, BENCH_WIDTH, BENCH_HEIGHT);

    // Create ViewPorts for source images (data, format, stride, width, height)
    ViewPort fgView(bufRGBA8, PixelFormatIDs::RGBA8_Straight,
                    BENCH_WIDTH * 4, BENCH_WIDTH, BENCH_HEIGHT);
    ViewPort bgView(bufRGBA8_2, PixelFormatIDs::RGBA8_Straight,
                    BENCH_WIDTH * 4, BENCH_WIDTH, BENCH_HEIGHT);
    ViewPort maskView(bufMask, PixelFormatIDs::Alpha8,
                      BENCH_WIDTH, BENCH_WIDTH, BENCH_HEIGHT);
    // Output viewport uses scaled dimensions
    ViewPort outView(bufOutput, PixelFormatIDs::RGBA8_Straight,
                     MATTE_RENDER_WIDTH * 4, MATTE_RENDER_WIDTH, MATTE_RENDER_HEIGHT);

    // Calculate scale factor
    float scaleX = static_cast<float>(MATTE_RENDER_WIDTH) / BENCH_WIDTH;
    float scaleY = static_cast<float>(MATTE_RENDER_HEIGHT) / BENCH_HEIGHT;

    // Build pipeline with scaled sources
    SourceNode fgSrc(fgView, float_to_fixed(BENCH_WIDTH / 2.0f),
                     float_to_fixed(BENCH_HEIGHT / 2.0f));
    fgSrc.setScale(scaleX, scaleY);

    SourceNode bgSrc(bgView, float_to_fixed(BENCH_WIDTH / 2.0f),
                     float_to_fixed(BENCH_HEIGHT / 2.0f));
    bgSrc.setScale(scaleX, scaleY);

    SourceNode maskSrc(maskView, float_to_fixed(BENCH_WIDTH / 2.0f),
                       float_to_fixed(BENCH_HEIGHT / 2.0f));
    maskSrc.setScale(scaleX, scaleY);

    MatteNode matte;
    RendererNode renderer;
    SinkNode sink(outView, float_to_fixed(MATTE_RENDER_WIDTH / 2.0f),
                  float_to_fixed(MATTE_RENDER_HEIGHT / 2.0f));

    // Connect pipeline
    fgSrc >> matte;
    bgSrc.connectTo(matte, 1);
    maskSrc.connectTo(matte, 2);
    matte >> renderer >> sink;

    renderer.setVirtualScreen(MATTE_RENDER_WIDTH, MATTE_RENDER_HEIGHT);

    // Warm up
    renderer.exec();

    // Benchmark with reduced iterations for scaled output
    uint32_t start = benchMicros();
    for (int i = 0; i < MATTE_ITERATIONS; i++) {
        renderer.exec();
    }
    uint32_t us = (benchMicros() - start) / MATTE_ITERATIONS;

    // Calculate throughput (us is per-iteration average)
    int pixelsPerIteration = MATTE_RENDER_WIDTH * MATTE_RENDER_HEIGHT;
    float nsPerPx = static_cast<float>(us) * 1000.0f / static_cast<float>(pixelsPerIteration);
    float mpps = static_cast<float>(pixelsPerIteration) / static_cast<float>(us);  // Mpix/sec

    benchPrintf("  %-12s %6u us  %5.1f ns/px  %5.2f Mpix/s\n",
                patternName, us, static_cast<double>(nsPerPx), static_cast<double>(mpps));
}

static void runMattePipelineBenchmarks(const char* patternArg) {
    if (!allocateBuffers()) return;
    if (!allocateMaskBuffer()) return;
    // Use MATTE_RENDER_WIDTH x MATTE_RENDER_HEIGHT for scaled pipeline output
    size_t outputSize = static_cast<size_t>(MATTE_RENDER_WIDTH) * MATTE_RENDER_HEIGHT * 4;
    if (!allocateOutputBuffer(outputSize)) return;

    benchPrintln();
    benchPrintln("=== Matte Pipeline Benchmark ===");
    benchPrintf("Source: %dx%d, Output: %dx%d (%.1fx scale)\n",
                BENCH_WIDTH, BENCH_HEIGHT, MATTE_RENDER_WIDTH, MATTE_RENDER_HEIGHT,
                static_cast<double>(MATTE_RENDER_WIDTH) / BENCH_WIDTH);
    benchPrintf("Iterations: %d\n", MATTE_ITERATIONS);
    benchPrintln();
    benchPrintln("Pipeline: SourceNode(fg) + SourceNode(bg) + SourceNode(mask)");
    benchPrintln("          -> MatteNode -> RendererNode -> SinkNode");
    benchPrintln();

    if (strcmp(patternArg, "all") == 0) {
        for (int i = 0; i < static_cast<int>(MaskPattern::COUNT); i++) {
            runMattePipelineBenchmark(static_cast<MaskPattern>(i));
        }
    } else {
        // Find matching pattern
        bool found = false;
        for (int i = 0; i < static_cast<int>(MaskPattern::COUNT); i++) {
            if (strcmp(patternArg, maskPatternShortNames[i]) == 0 ||
                strcmp(patternArg, maskPatternNames[i]) == 0) {
                runMattePipelineBenchmark(static_cast<MaskPattern>(i));
                found = true;
                break;
            }
        }
        if (!found) {
            benchPrintf("Unknown pattern: %s\n", patternArg);
            benchPrintln("Available: all | zero | 255 | left | right | grad | rand");
        }
    }

    benchPrintln();
}

// =============================================================================
// Command Interface
// =============================================================================

static void printHelp() {
    benchPrintln();
    benchPrintln("=== fleximg Unified Benchmark ===");
    benchPrintln();
    benchPrintln("Commands:");
    benchPrintln("  c [fmt]  : Conversion benchmark");
    benchPrintln("  b [fmt]  : BlendUnder benchmark (Direct vs Indirect)");
    benchPrintln("  u [pat]  : blendUnderStraight with dst pattern variations");
    benchPrintln("  t [grp] [bpp] : copyRowDDA benchmark (DDA scanline transform)");
    benchPrintln("  m [pat]  : Matte composite benchmark (direct, no pipeline)");
    benchPrintln("  p [pat]  : Matte pipeline benchmark (full node pipeline)");
    benchPrintln("  d        : Analyze alpha distribution of test data");
    benchPrintln("  s        : ImageBufferSet/RenderResponse move cost benchmark");
    benchPrintln("  r        : RenderResponse move count in pipeline");
    benchPrintln("  a        : All benchmarks");
    benchPrintln("  l        : List formats");
    benchPrintln("  k        : Show calibration info (CPU freq, overhead)");
    benchPrintln("  h        : This help");
    benchPrintln();
    benchPrintln("Formats:");
    benchPrint("  all");
    for (int i = 0; i < NUM_FORMATS; i++) {
        benchPrint(" | ");
        benchPrint(formats[i].shortName);
    }
    benchPrintln();
    benchPrintln();
    benchPrintln("Dst Patterns (for 'u' command):");
    benchPrintln("  all | trans | opaque | semi | mixed");
    benchPrintln();
    benchPrintln("DDA Groups (for 't' command):");
    benchPrintln("  [grp] = all | h (horizontal) | v (vertical) | d (diagonal)");
    benchPrintln("  [bpp] = all | 4 | 3 | 2 | 1");
    benchPrintln();
    benchPrintln("Mask Patterns (for 'm'/'p' commands):");
    benchPrintln("  all | zero | 255 | left | right | grad | rand");
    benchPrintln();
    benchPrintln("Examples:");
    benchPrintln("  c all     - All conversion benchmarks");
    benchPrintln("  c rgb332  - RGB332 conversion only");
    benchPrintln("  b rgba8   - RGBA8 blend benchmark");
    benchPrintln("  u all     - blendUnderStraight with all dst patterns");
    benchPrintln("  u trans   - blendUnderStraight with transparent dst");
    benchPrintln("  t all     - copyRowDDA all directions, all BPPs");
    benchPrintln("  t h       - copyRowDDA horizontal, all BPPs");
    benchPrintln("  t all 4   - copyRowDDA all directions, BPP4 only");
    benchPrintln("  t h 2     - copyRowDDA horizontal, BPP2 only");
    benchPrintln("  m all     - Matte composite with all patterns");
    benchPrintln("  m grad    - Matte composite with gradient pattern");
    benchPrintln("  p all     - Matte pipeline with all mask patterns");
    benchPrintln("  p grad    - Matte pipeline with gradient mask");
    benchPrintln("  d         - Show alpha distribution analysis");
    benchPrintln();
}

static void listFormats() {
    benchPrintln();
    benchPrintln("Available formats:");
    for (int i = 0; i < NUM_FORMATS; i++) {
        benchPrintf("  %-10s : %s\n", formats[i].shortName, formats[i].name);
    }
    benchPrintln();
}

// =============================================================================
// ImageBufferSet Benchmark
// =============================================================================

static constexpr int IBS_ITERATIONS = 1000;
static constexpr int IBS_WIDTH = 320;  // Typical scanline width

static void runImageBufferSetBenchmark() {
    benchPrintln();
    benchPrintln("=== RenderResponse/ImageBufferSet Move Cost Benchmark ===");
    benchPrintf("Width: %d, Iterations: %d\n", IBS_WIDTH, IBS_ITERATIONS);
    benchPrintln();

    // Benchmark 0: Pure RenderResponse move cost (no construction)
    {
        uint8_t* dummyBuf = static_cast<uint8_t*>(BENCH_MALLOC(IBS_WIDTH * 4));
        if (dummyBuf) {
            ViewPort srcView(dummyBuf, PixelFormatIDs::RGBA8_Straight, IBS_WIDTH * 4, IBS_WIDTH, 1);
            Point origin{0, 0};
            // Pre-create responses
            ImageBuffer buf(srcView);
            RenderResponse resp1(std::move(buf), origin);

            uint32_t start = benchMicros();
            for (int i = 0; i < IBS_ITERATIONS; i++) {
                RenderResponse resp2(std::move(resp1));
                resp1 = std::move(resp2);  // Move back
            }
            uint32_t elapsed = benchMicros() - start;
            float nsPerOp = static_cast<float>(elapsed) * 1000.0f / (IBS_ITERATIONS * 2);  // 2 moves per iteration
            benchPrintf("RenderResponse pure move:         %7.1f ns/move\n", static_cast<double>(nsPerOp));
            BENCH_FREE(dummyBuf);
        }
    }
    benchPrintln();

    // Allocate test buffer
    uint8_t* testBuf = static_cast<uint8_t*>(BENCH_MALLOC(IBS_WIDTH * 4));
    if (!testBuf) {
        benchPrintln("ERROR: Buffer allocation failed!");
        return;
    }
    // Initialize with test pattern
    for (int i = 0; i < IBS_WIDTH * 4; i++) {
        testBuf[i] = static_cast<uint8_t>(i & 0xFF);
    }

    // Create a pool for pool-based tests
    ImageBufferEntryPool pool;

    // Benchmark 1: ImageBuffer construction (reference mode, no alloc)
    {
        ViewPort srcView(testBuf, PixelFormatIDs::RGBA8_Straight, IBS_WIDTH * 4, IBS_WIDTH, 1);
        uint32_t start = benchMicros();
        for (int i = 0; i < IBS_ITERATIONS; i++) {
            ImageBuffer buf(srcView);
            (void)buf;
        }
        uint32_t elapsed = benchMicros() - start;
        float nsPerOp = static_cast<float>(elapsed) * 1000.0f / IBS_ITERATIONS;
        benchPrintf("ImageBuffer(ViewPort) ref:        %7.1f ns/op\n", static_cast<double>(nsPerOp));
    }

    // Benchmark 2: ImageBufferSet with pool + addBuffer
    {
        ViewPort srcView(testBuf, PixelFormatIDs::RGBA8_Straight, IBS_WIDTH * 4, IBS_WIDTH, 1);
        uint32_t start = benchMicros();
        for (int i = 0; i < IBS_ITERATIONS; i++) {
            ImageBufferSet set(&pool, nullptr);
            ImageBuffer buf(srcView);
            set.addBuffer(std::move(buf), 0);
        }
        uint32_t elapsed = benchMicros() - start;
        float nsPerOp = static_cast<float>(elapsed) * 1000.0f / IBS_ITERATIONS;
        benchPrintf("ImageBufferSet+addBuffer:         %7.1f ns/op\n", static_cast<double>(nsPerOp));
    }

    // Benchmark 3: RenderResponse construction with ImageBuffer
    {
        ViewPort srcView(testBuf, PixelFormatIDs::RGBA8_Straight, IBS_WIDTH * 4, IBS_WIDTH, 1);
        Point origin{0, 0};
        uint32_t start = benchMicros();
        for (int i = 0; i < IBS_ITERATIONS; i++) {
            ImageBuffer buf(srcView);
            RenderResponse resp(std::move(buf), origin);
            (void)resp;
        }
        uint32_t elapsed = benchMicros() - start;
        float nsPerOp = static_cast<float>(elapsed) * 1000.0f / IBS_ITERATIONS;
        benchPrintf("RenderResponse(ImageBuffer):      %7.1f ns/op\n", static_cast<double>(nsPerOp));
    }

    // Benchmark 4: RenderResponse move (simulating return from function)
    {
        ViewPort srcView(testBuf, PixelFormatIDs::RGBA8_Straight, IBS_WIDTH * 4, IBS_WIDTH, 1);
        Point origin{0, 0};
        uint32_t start = benchMicros();
        for (int i = 0; i < IBS_ITERATIONS; i++) {
            ImageBuffer buf(srcView);
            RenderResponse resp1(std::move(buf), origin);
            RenderResponse resp2(std::move(resp1));  // Move
            (void)resp2;
        }
        uint32_t elapsed = benchMicros() - start;
        float nsPerOp = static_cast<float>(elapsed) * 1000.0f / IBS_ITERATIONS;
        benchPrintf("RenderResponse construct+move:    %7.1f ns/op\n", static_cast<double>(nsPerOp));
    }

    // Benchmark 5: ImageBufferSet move (with pool)
    {
        ViewPort srcView(testBuf, PixelFormatIDs::RGBA8_Straight, IBS_WIDTH * 4, IBS_WIDTH, 1);
        uint32_t start = benchMicros();
        for (int i = 0; i < IBS_ITERATIONS; i++) {
            ImageBufferSet set1(&pool, nullptr);
            ImageBuffer buf(srcView);
            set1.addBuffer(std::move(buf), 0);
            ImageBufferSet set2(std::move(set1));  // Move
            (void)set2;
        }
        uint32_t elapsed = benchMicros() - start;
        float nsPerOp = static_cast<float>(elapsed) * 1000.0f / IBS_ITERATIONS;
        benchPrintf("ImageBufferSet move:              %7.1f ns/op\n", static_cast<double>(nsPerOp));
    }

    // Benchmark 6: Full pipeline simulation (construct + move + view)
    {
        ViewPort srcView(testBuf, PixelFormatIDs::RGBA8_Straight, IBS_WIDTH * 4, IBS_WIDTH, 1);
        Point origin{0, 0};
        uint32_t start = benchMicros();
        for (int i = 0; i < IBS_ITERATIONS; i++) {
            // Simulate: SourceNode creates response
            ImageBuffer buf(srcView);
            RenderResponse resp(std::move(buf), origin);
            // Simulate: Response is moved (returned)
            RenderResponse resp2(std::move(resp));
            // Simulate: SinkNode accesses view
            ViewPort v = resp2.view();
            (void)v;
        }
        uint32_t elapsed = benchMicros() - start;
        float nsPerOp = static_cast<float>(elapsed) * 1000.0f / IBS_ITERATIONS;
        benchPrintf("Full path (construct+move+view):  %7.1f ns/op\n", static_cast<double>(nsPerOp));
    }

    benchPrintln();

    BENCH_FREE(testBuf);
}

// =============================================================================
// RenderResponse Move Count in Pipeline Benchmark
// =============================================================================

static void runMoveCountBenchmark() {
    benchPrintln();
    benchPrintln("=== RenderResponse Move Count in Pipeline ===");
    benchPrintln();

    if (!allocateBuffers()) return;
    if (!allocateMaskBuffer()) return;
    size_t outputSize = static_cast<size_t>(MATTE_RENDER_WIDTH) * MATTE_RENDER_HEIGHT * 4;
    if (!allocateOutputBuffer(outputSize)) return;

    // Initialize buffers
    initForegroundBuffer();
    initBackgroundBuffer();
    initMask2DWithPattern(MaskPattern::Gradient, BENCH_WIDTH, BENCH_HEIGHT);

    // Create ViewPorts
    ViewPort fgView(bufRGBA8, PixelFormatIDs::RGBA8_Straight,
                    BENCH_WIDTH * 4, BENCH_WIDTH, BENCH_HEIGHT);
    ViewPort bgView(bufRGBA8_2, PixelFormatIDs::RGBA8_Straight,
                    BENCH_WIDTH * 4, BENCH_WIDTH, BENCH_HEIGHT);
    ViewPort maskView(bufMask, PixelFormatIDs::Alpha8,
                      BENCH_WIDTH, BENCH_WIDTH, BENCH_HEIGHT);
    ViewPort outView(bufOutput, PixelFormatIDs::RGBA8_Straight,
                     MATTE_RENDER_WIDTH * 4, MATTE_RENDER_WIDTH, MATTE_RENDER_HEIGHT);

    float scaleX = static_cast<float>(MATTE_RENDER_WIDTH) / BENCH_WIDTH;
    float scaleY = static_cast<float>(MATTE_RENDER_HEIGHT) / BENCH_HEIGHT;

    // Build pipeline
    SourceNode fgSrc(fgView, float_to_fixed(BENCH_WIDTH / 2.0f),
                     float_to_fixed(BENCH_HEIGHT / 2.0f));
    fgSrc.setScale(scaleX, scaleY);

    SourceNode bgSrc(bgView, float_to_fixed(BENCH_WIDTH / 2.0f),
                     float_to_fixed(BENCH_HEIGHT / 2.0f));
    bgSrc.setScale(scaleX, scaleY);

    SourceNode maskSrc(maskView, float_to_fixed(BENCH_WIDTH / 2.0f),
                       float_to_fixed(BENCH_HEIGHT / 2.0f));
    maskSrc.setScale(scaleX, scaleY);

    MatteNode matte;
    RendererNode renderer;
    SinkNode sink(outView, float_to_fixed(MATTE_RENDER_WIDTH / 2.0f),
                  float_to_fixed(MATTE_RENDER_HEIGHT / 2.0f));

    fgSrc >> matte;
    bgSrc.connectTo(matte, 1);
    maskSrc.connectTo(matte, 2);
    matte >> renderer >> sink;

    renderer.setVirtualScreen(MATTE_RENDER_WIDTH, MATTE_RENDER_HEIGHT);

    // Warm up
    renderer.exec();

    // Reset counter and run
    RenderResponse::resetMoveCount();
    uint32_t start = benchMicros();
    renderer.exec();
    uint32_t elapsed = benchMicros() - start;
    int moveCount = RenderResponse::getMoveCount();

    int totalScanlines = MATTE_RENDER_HEIGHT;
    float movesPerScanline = static_cast<float>(moveCount) / totalScanlines;

    benchPrintf("Pipeline: 3x SourceNode -> MatteNode -> RendererNode -> SinkNode\n");
    benchPrintf("Output: %dx%d (%.1fx scale)\n", MATTE_RENDER_WIDTH, MATTE_RENDER_HEIGHT,
                static_cast<double>(scaleX));
    benchPrintln();
    benchPrintf("exec() time:        %u us\n", elapsed);
    benchPrintf("Total moves:        %d\n", moveCount);
    benchPrintf("Moves per scanline: %.1f\n", static_cast<double>(movesPerScanline));
    benchPrintln();

    // Estimate move overhead using pure move cost from 's' benchmark
    // Assume ~50-200ns per move (typical range)
    float estimatedMoveOverheadUs = static_cast<float>(moveCount) * 0.1f;  // 100ns per move
    float overheadPercent = (estimatedMoveOverheadUs / static_cast<float>(elapsed)) * 100.0f;
    benchPrintf("Estimated move overhead: ~%.0f us (%.1f%% of exec time, assuming 100ns/move)\n",
                static_cast<double>(estimatedMoveOverheadUs),
                static_cast<double>(overheadPercent));
    benchPrintln();
    benchPrintln("Note: Use 's' command to measure actual ns/move on this platform.");
    benchPrintln();
}

static void processCommand(const char* cmd) {
    // Skip empty commands
    if (cmd[0] == '\0') return;

    // Parse command and argument
    char cmdChar = cmd[0];
    const char* arg = "all";

    // Find argument (skip command char and spaces)
    const char* p = cmd + 1;
    while (*p == ' ') p++;
    if (*p != '\0') arg = p;

    switch (cmdChar) {
        case 'c':
        case 'C':
            runConvertBenchmark(arg);
            break;
        case 'b':
        case 'B':
            runBlendBenchmark(arg);
            break;
        case 'u':
        case 'U':
            runBlendUnderStraightBenchmarks(arg);
            break;
        case 't':
        case 'T':
            runDDABenchmark(arg);
            break;
        case 'm':
        case 'M':
            runMatteCompositeBenchmarks(arg);
            break;
        case 'p':
        case 'P':
            runMattePipelineBenchmarks(arg);
            break;
        case 'd':
        case 'D':
            runAlphaDistributionAnalysis();
            break;
        case 'a':
        case 'A':
            runConvertBenchmark("all");
            runBlendBenchmark("all");
            runBlendUnderStraightBenchmarks("all");
            runDDABenchmark("all");
            runMatteCompositeBenchmarks("all");
            runMattePipelineBenchmarks("all");
            break;
        case 'l':
        case 'L':
            listFormats();
            break;
        case 'k':
        case 'K':
            printCalibrationInfo();
            break;
        case 's':
        case 'S':
            runImageBufferSetBenchmark();
            break;
        case 'r':
        case 'R':
            runMoveCountBenchmark();
            break;
        case 'h':
        case 'H':
        case '?':
            printHelp();
            break;
        default:
            benchPrintf("Unknown command: %c (type 'h' for help)\n", cmdChar);
            break;
    }
}

// =============================================================================
// Main Entry Points
// =============================================================================

#ifdef BENCH_M5STACK

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    BENCH_SERIAL.begin(115200);
    delay(1000);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(1);
    M5.Display.println("fleximg Bench");

    benchPrintln();
    benchPrintln("fleximg Unified Benchmark");
    benchPrintf("Free heap: %d bytes\n", ESP.getFreeHeap());

    // sizeof情報を表示
    benchPrintln("\n=== sizeof info ===");
    benchPrintf("ImageBuffer:         %u bytes\n", sizeof(ImageBuffer));
    benchPrintf("  ViewPort:          %u bytes\n", sizeof(ViewPort));
    benchPrintf("  PixelAuxInfo:      %u bytes\n", sizeof(PixelAuxInfo));
    benchPrintf("  IAllocator*:       %u bytes\n", sizeof(core::memory::IAllocator*));
    benchPrintf("ImageBufferSet:      %u bytes\n", sizeof(ImageBufferSet));
    benchPrintf("Entry:               %u bytes\n", sizeof(ImageBufferEntryPool::Entry));
    benchPrintf("ImageBufferEntryPool:%u bytes\n", sizeof(ImageBufferEntryPool));
    benchPrintln();

    if (!allocateBuffers()) {
        M5.Display.println("Alloc failed!");
        return;
    }

    benchPrintf("Free heap after alloc: %d bytes\n", ESP.getFreeHeap());

    initTestData();
    initFormatBuffers();

    printHelp();

    M5.Display.println("Ready. See Serial.");
}

void loop() {
    M5.update();

    // Serial command
    if (BENCH_SERIAL.available()) {
        char cmd[64];
        int len = benchRead(cmd, sizeof(cmd));
        if (len > 0) {
            benchPrintf(">> %s\n", cmd);
            processCommand(cmd);
        }
    }

    // Button shortcuts
    if (M5.BtnA.wasPressed()) {
        processCommand("c all");
    }
    if (M5.BtnB.wasPressed()) {
        processCommand("b all");
    }
    if (M5.BtnC.wasPressed()) {
        processCommand("a");
    }

    delay(10);
}

#else  // Native PC

int main(int argc, char* argv[]) {
    benchPrintln("fleximg Unified Benchmark (Native)");
    benchPrintln();

    // sizeof情報を表示
    benchPrintln("=== sizeof info ===");
    benchPrintf("ImageBuffer:         %zu bytes\n", sizeof(ImageBuffer));
    benchPrintf("  ViewPort:          %zu bytes\n", sizeof(ViewPort));
    benchPrintf("  PixelAuxInfo:      %zu bytes\n", sizeof(PixelAuxInfo));
    benchPrintf("  IAllocator*:       %zu bytes\n", sizeof(core::memory::IAllocator*));
    benchPrintf("ImageBufferSet:      %zu bytes\n", sizeof(ImageBufferSet));
    benchPrintf("Entry:               %zu bytes\n", sizeof(ImageBufferEntryPool::Entry));
    benchPrintf("ImageBufferEntryPool:%zu bytes\n", sizeof(ImageBufferEntryPool));
    benchPrintln();

    if (!allocateBuffers()) {
        return 1;
    }

    initTestData();
    initFormatBuffers();

    // If command-line arguments provided, run those
    if (argc > 1) {
        // Reconstruct command from arguments
        char cmd[256] = "";
        for (int i = 1; i < argc; i++) {
            if (i > 1) strcat(cmd, " ");
            strcat(cmd, argv[i]);
        }
        processCommand(cmd);
        return 0;
    }

    // Interactive mode
    printHelp();
    benchPrint("> ");

    char cmd[64];
    while (benchRead(cmd, sizeof(cmd)) >= 0) {
        if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            break;
        }
        processCommand(cmd);
        benchPrint("> ");
    }

    return 0;
}

#endif
