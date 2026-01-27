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
 *   c [fmt]  : Conversion benchmark (toStraight/fromStraight/toPremul/fromPremul)
 *   b [fmt]  : BlendUnder benchmark (direct vs indirect path)
 *   s [fmt]  : Pathway comparison (Premul vs Straight) [FLEXIMG_ENABLE_PREMUL]
 *   u [pat]  : blendUnderStraight benchmark with dst pattern variations
 *   d        : Analyze alpha distribution of test data
 *   a        : All benchmarks
 *   l        : List available formats
 *   h        : Help
 *
 *   [fmt] = all | rgb332 | rgb565le | rgb565be | rgb888 | bgr888 | rgba8 | rgba16p
 *   [pat] = all | trans | opaque | semi | mixed
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
#endif

// fleximg (stb-style: define FLEXIMG_IMPLEMENTATION before including headers)
#define FLEXIMG_NAMESPACE fleximg
#define FLEXIMG_IMPLEMENTATION
#include "fleximg/core/common.h"
#include "fleximg/image/pixel_format.h"

using namespace fleximg;

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

static bool benchAvailable() { return BENCH_SERIAL.available() > 0; }

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

static void benchDelay(int ms) { delay(ms); }

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

#endif

// =============================================================================
// Benchmark Configuration
// =============================================================================

#ifdef BENCH_M5STACK
    static constexpr int BENCH_PIXELS = 4096;
    static constexpr int ITERATIONS = 1000;
    static constexpr int WARMUP = 10;
#else
    // PC is much faster, use larger pixel count for accurate measurement
    static constexpr int BENCH_PIXELS = 65536;
    static constexpr int ITERATIONS = 1000;
    static constexpr int WARMUP = 10;
#endif

// =============================================================================
// Buffer Management
// =============================================================================

static uint8_t* bufRGBA8 = nullptr;      // RGBA8 buffer
static uint8_t* bufRGBA8_2 = nullptr;    // Second RGBA8 buffer (canvas for Straight)
static uint8_t* bufRGB888 = nullptr;     // RGB888/BGR888 buffer
static uint8_t* bufRGB565 = nullptr;     // RGB565 buffer
static uint8_t* bufRGB332 = nullptr;     // RGB332 buffer
static uint16_t* bufRGBA16 = nullptr;    // RGBA16_Premultiplied buffer
static uint16_t* bufRGBA16_2 = nullptr;  // Second RGBA16 buffer for blending

// Platform-specific memory allocation
// ESP32: Use internal SRAM (not PSRAM) for accurate benchmarking
// PC: Use standard malloc
#ifdef BENCH_M5STACK
    #define BENCH_MALLOC(size) heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#else
    #define BENCH_MALLOC(size) malloc(size)
#endif

static bool allocateBuffers() {
    bufRGBA8 = static_cast<uint8_t*>(BENCH_MALLOC(BENCH_PIXELS * 4));
    bufRGBA8_2 = static_cast<uint8_t*>(BENCH_MALLOC(BENCH_PIXELS * 4));
    bufRGB888 = static_cast<uint8_t*>(BENCH_MALLOC(BENCH_PIXELS * 3));
    bufRGB565 = static_cast<uint8_t*>(BENCH_MALLOC(BENCH_PIXELS * 2));
    bufRGB332 = static_cast<uint8_t*>(BENCH_MALLOC(BENCH_PIXELS * 1));
    bufRGBA16 = static_cast<uint16_t*>(BENCH_MALLOC(BENCH_PIXELS * 8));
    bufRGBA16_2 = static_cast<uint16_t*>(BENCH_MALLOC(BENCH_PIXELS * 8));

    if (!bufRGBA8 || !bufRGBA8_2 || !bufRGB888 || !bufRGB565 || !bufRGB332 || !bufRGBA16 || !bufRGBA16_2) {
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
    int transparent;   // alpha == 0
    int opaque;        // alpha == 255
    int semiLow;       // alpha 1-127
    int semiHigh;      // alpha 128-254
    int total;

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
        benchPrintf("  %-12s: trans=%5.1f%% opaque=%5.1f%% semi=%5.1f%% (low=%5.1f%% high=%5.1f%%)\n",
            label,
            static_cast<double>(100.0f * transparent / total),
            static_cast<double>(100.0f * opaque / total),
            static_cast<double>(100.0f * (semiLow + semiHigh) / total),
            static_cast<double>(100.0f * semiLow / total),
            static_cast<double>(100.0f * semiHigh / total));
    }
};

// Analyze alpha distribution of a buffer
static void analyzeAlphaDistribution(const uint8_t* buf, int pixelCount, AlphaDistribution& dist) {
    dist.reset();
    for (int i = 0; i < pixelCount; i++) {
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

#ifdef FLEXIMG_ENABLE_PREMUL
// Initialize RGBA16 canvas with semi-transparent green
static void initCanvasRGBA16() {
    for (int i = 0; i < BENCH_PIXELS; i++) {
        uint16_t alpha = 32768;  // ~50%
        bufRGBA16[i * 4 + 0] = 0;
        bufRGBA16[i * 4 + 1] = alpha;  // Green (premultiplied)
        bufRGBA16[i * 4 + 2] = 0;
        bufRGBA16[i * 4 + 3] = alpha;
    }
}

// Initialize RGBA8 canvas with semi-transparent green (Straight alpha)
static void initCanvasRGBA8() {
    initCanvasRGBA8WithPattern(DstPattern::SemiTransparent);
}
#endif // FLEXIMG_ENABLE_PREMUL

// =============================================================================
// Benchmark Runner
// =============================================================================

template<typename Func>
static uint32_t runBenchmark(Func func) {
    // Warmup
    for (int i = 0; i < WARMUP; i++) {
        func();
    }

    // Measure
    uint32_t start = benchMicros();
    for (int i = 0; i < ITERATIONS; i++) {
        func();
    }
    uint32_t elapsed = benchMicros() - start;

    return elapsed / ITERATIONS;
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
#ifdef FLEXIMG_ENABLE_PREMUL
    {"RGBA16_Premul",   "rgba16p",  &BuiltinFormats::RGBA16_Premultiplied, nullptr, 8},
#endif
};
static constexpr int NUM_FORMATS = sizeof(formats) / sizeof(formats[0]);

static void initFormatBuffers() {
    formats[0].srcBuffer = bufRGB332;   // RGB332
    formats[1].srcBuffer = bufRGB565;   // RGB565_LE
    formats[2].srcBuffer = bufRGB565;   // RGB565_BE
    formats[3].srcBuffer = bufRGB888;   // RGB888
    formats[4].srcBuffer = bufRGB888;   // BGR888
    formats[5].srcBuffer = bufRGBA8;    // RGBA8_Straight
#ifdef FLEXIMG_ENABLE_PREMUL
    formats[6].srcBuffer = reinterpret_cast<uint8_t*>(bufRGBA16_2);  // RGBA16_Premul
#endif
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

    // toPremul
    if (fmt.format->toPremul) {
        uint32_t us = runBenchmark([&]() {
            fmt.format->toPremul(bufRGBA16, fmt.srcBuffer, BENCH_PIXELS, nullptr);
        });
        benchPrintf(" %6u", us);
    } else {
        benchPrint("      -");
    }

    // fromPremul
    if (fmt.format->fromPremul) {
        uint32_t us = runBenchmark([&]() {
            fmt.format->fromPremul(fmt.srcBuffer, bufRGBA16, BENCH_PIXELS, nullptr);
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
    benchPrintln("Format           toStr  frStr toPre frPre (us/frame)");
    benchPrintln("---------------- ------ ------ ------ ------");

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

#ifdef FLEXIMG_ENABLE_PREMUL
// Premul mode: compare direct blendUnderPremul vs indirect (toPremul + blend)
static void benchBlendFormatPremul(int idx) {
    const auto& fmt = formats[idx];

    // Skip RGBA16_Premul (it's the destination format)
    if (idx == 6) {
        benchPrintf("%-16s   (dst format, skip)\n", fmt.name);
        return;
    }

    if (!fmt.format->blendUnderPremul || !fmt.format->toPremul) {
        benchPrintf("%-16s   (no blend/toPremul)\n", fmt.name);
        return;
    }

    benchPrintf("%-16s", fmt.name);

    // Direct path
    uint32_t directUs = runBenchmark([&]() {
        initCanvasRGBA16();
        fmt.format->blendUnderPremul(bufRGBA16, fmt.srcBuffer, BENCH_PIXELS, nullptr);
    });
    benchPrintf(" %6u", directUs);

    // Indirect path (toPremul + RGBA16_Premul blend)
    uint32_t indirectUs = runBenchmark([&]() {
        initCanvasRGBA16();
        fmt.format->toPremul(bufRGBA16_2, fmt.srcBuffer, BENCH_PIXELS, nullptr);
        BuiltinFormats::RGBA16_Premultiplied.blendUnderPremul(
            bufRGBA16, bufRGBA16_2, BENCH_PIXELS, nullptr);
    });
    benchPrintf(" %6u", indirectUs);

    // Ratio
    float ratio = (directUs > 0) ? static_cast<float>(indirectUs) / static_cast<float>(directUs) : 0;
    benchPrintf("  %5.2fx\n", ratio);
}
#endif // FLEXIMG_ENABLE_PREMUL

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
#ifdef FLEXIMG_ENABLE_PREMUL
    benchPrintln();
    benchPrintln("=== BlendUnder Benchmark [Premul] (Direct vs Indirect) ===");
    benchPrintf("Pixels: %d, Iterations: %d\n", BENCH_PIXELS, ITERATIONS);
    benchPrintln();
    benchPrintln("Format           Direct Indir  Ratio");
    benchPrintln("---------------- ------ ------ ------");

    if (strcmp(fmtName, "all") == 0) {
        for (int i = 0; i < NUM_FORMATS; i++) {
            benchBlendFormatPremul(i);
        }
    } else {
        int idx = findFormat(fmtName);
        if (idx >= 0) {
            benchBlendFormatPremul(idx);
        } else {
            benchPrintf("Unknown format: %s\n", fmtName);
        }
    }
    benchPrintln("(Ratio > 1 means Direct is faster)");
#endif // FLEXIMG_ENABLE_PREMUL

    benchPrintln();
    benchPrintln("=== BlendUnder Benchmark [Straight] (Direct vs Indirect) ===");
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

#ifdef FLEXIMG_ENABLE_PREMUL
// =============================================================================
// Premul vs Straight Pathway Comparison (Premul mode only)
// =============================================================================
// Compare full blending pipeline:
//   Premul:   toPremul → blendUnderPremul → fromPremul
//   Straight: toStraight → blendUnderStraight → fromStraight

// Number of blend operations to simulate multi-layer compositing
static constexpr int BLEND_LAYERS = 10;

static void benchPathwayFormat(int idx) {
    const auto& fmt = formats[idx];

    // Skip formats without necessary functions
    if (!fmt.format->toPremul || !fmt.format->fromPremul ||
        !fmt.format->toStraight || !fmt.format->fromStraight) {
        benchPrintf("%-16s   (missing conversion)\n", fmt.name);
        return;
    }

    // Skip RGBA16_Premul (it's the Premul canvas format)
    if (idx == 6) {
        benchPrintf("%-16s   (canvas format)\n", fmt.name);
        return;
    }

    benchPrintf("%-16s", fmt.name);

    // Premul pathway: toPremul → (blendUnderPremul × N) → fromPremul
    uint32_t premulUs = runBenchmark([&]() {
        // Convert src to temp Premul (1回)
        fmt.format->toPremul(bufRGBA16_2, fmt.srcBuffer, BENCH_PIXELS, nullptr);
        // Initialize canvas
        initCanvasRGBA16();
        // Blend N times (複数レイヤー合成をシミュレート)
        for (int layer = 0; layer < BLEND_LAYERS; layer++) {
            BuiltinFormats::RGBA16_Premultiplied.blendUnderPremul(
                bufRGBA16, bufRGBA16_2, BENCH_PIXELS, nullptr);
        }
        // Convert back to dst format (1回)
        fmt.format->fromPremul(fmt.srcBuffer, bufRGBA16, BENCH_PIXELS, nullptr);
    });
    benchPrintf(" %6u", premulUs);

    // Straight pathway: toStraight → (blendUnderStraight × N) → fromStraight
    uint32_t straightUs = runBenchmark([&]() {
        // Convert src to temp Straight (1回)
        fmt.format->toStraight(bufRGBA8, fmt.srcBuffer, BENCH_PIXELS, nullptr);
        // Initialize canvas
        initCanvasRGBA8();
        // Blend N times (複数レイヤー合成をシミュレート)
        for (int layer = 0; layer < BLEND_LAYERS; layer++) {
            BuiltinFormats::RGBA8_Straight.blendUnderStraight(
                bufRGBA8_2, bufRGBA8, BENCH_PIXELS, nullptr);
        }
        // Convert back to dst format (1回)
        fmt.format->fromStraight(fmt.srcBuffer, bufRGBA8_2, BENCH_PIXELS, nullptr);
    });
    benchPrintf(" %6u", straightUs);

    // Ratio
    float ratio = (premulUs > 0) ? static_cast<float>(straightUs) / static_cast<float>(premulUs) : 0;
    benchPrintf("  %5.2fx\n", ratio);
}

static void runPathwayBenchmark(const char* fmtName) {
    benchPrintln();
    benchPrintln("=== Pathway Comparison (Premul vs Straight) ===");
    benchPrintf("Pixels: %d, Iterations: %d, Layers: %d\n", BENCH_PIXELS, ITERATIONS, BLEND_LAYERS);
    benchPrintln();
    benchPrintln("Pipeline: convert → blend x N → convert back");
    benchPrintln();
    benchPrintln("Format           Premul Straig  Ratio");
    benchPrintln("---------------- ------ ------ ------");

    if (strcmp(fmtName, "all") == 0) {
        for (int i = 0; i < NUM_FORMATS; i++) {
            benchPathwayFormat(i);
        }
    } else {
        int idx = findFormat(fmtName);
        if (idx >= 0) {
            benchPathwayFormat(idx);
        } else {
            benchPrintf("Unknown format: %s\n", fmtName);
        }
    }
    benchPrintln("(Ratio > 1 means Premul is faster)");
    benchPrintln();

    // Pure blend comparison
    benchPrintln("=== Pure Blend Comparison ===");
    benchPrintf("Pixels: %d, Iterations: %d\n", BENCH_PIXELS, ITERATIONS);
    benchPrintln();

    // RGBA16_Premul blendUnderPremul
    uint32_t premulBlendUs = runBenchmark([&]() {
        initCanvasRGBA16();
        // Use bufRGBA16_2 as src (initialize with semi-transparent data)
        BuiltinFormats::RGBA16_Premultiplied.blendUnderPremul(
            bufRGBA16, bufRGBA16_2, BENCH_PIXELS, nullptr);
    });

    // RGBA8_Straight blendUnderStraight
    uint32_t straightBlendUs = runBenchmark([&]() {
        initCanvasRGBA8();
        BuiltinFormats::RGBA8_Straight.blendUnderStraight(
            bufRGBA8_2, bufRGBA8, BENCH_PIXELS, nullptr);
    });

    benchPrintf("RGBA16_Premul.blendUnderPremul:     %6u us\n", premulBlendUs);
    benchPrintf("RGBA8_Straight.blendUnderStraight:  %6u us\n", straightBlendUs);
    float blendRatio = (premulBlendUs > 0) ? static_cast<float>(straightBlendUs) / static_cast<float>(premulBlendUs) : 0;
    benchPrintf("Ratio (Straight/Premul):            %5.2fx\n", blendRatio);
    benchPrintln();
}
#endif // FLEXIMG_ENABLE_PREMUL

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
// Command Interface
// =============================================================================

static void printHelp() {
    benchPrintln();
    benchPrintln("=== fleximg Unified Benchmark ===");
    benchPrintln();
    benchPrintln("Commands:");
    benchPrintln("  c [fmt]  : Conversion benchmark");
    benchPrintln("  b [fmt]  : BlendUnder benchmark (Direct vs Indirect)");
    benchPrintln("  s [fmt]  : Pathway comparison (Premul vs Straight)");
    benchPrintln("  u [pat]  : blendUnderStraight with dst pattern variations");
    benchPrintln("  d        : Analyze alpha distribution of test data");
    benchPrintln("  a        : All benchmarks");
    benchPrintln("  l        : List formats");
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
    benchPrintln("Examples:");
    benchPrintln("  c all     - All conversion benchmarks");
    benchPrintln("  c rgb332  - RGB332 conversion only");
    benchPrintln("  b rgba8   - RGBA8 blend benchmark");
    benchPrintln("  s rgb565le - RGB565_LE pathway comparison");
    benchPrintln("  u all     - blendUnderStraight with all dst patterns");
    benchPrintln("  u trans   - blendUnderStraight with transparent dst");
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
#ifdef FLEXIMG_ENABLE_PREMUL
        case 's':
        case 'S':
            runPathwayBenchmark(arg);
            break;
#endif
        case 'u':
        case 'U':
            runBlendUnderStraightBenchmarks(arg);
            break;
        case 'd':
        case 'D':
            runAlphaDistributionAnalysis();
            break;
        case 'a':
        case 'A':
            runConvertBenchmark("all");
            runBlendBenchmark("all");
#ifdef FLEXIMG_ENABLE_PREMUL
            runPathwayBenchmark("all");
#endif
            runBlendUnderStraightBenchmarks("all");
            break;
        case 'l':
        case 'L':
            listFormats();
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

    if (!allocateBuffers()) {
        M5.Display.println("Alloc failed!");
        return;
    }

    benchPrintf("Free heap after alloc: %d bytes\n", ESP.getFreeHeap());

    initTestData();
    initFormatBuffers();

#ifdef FLEXIMG_ENABLE_PREMUL
    // Prepare RGBA16_2 buffer with premul data for RGBA16_Premul tests
    BuiltinFormats::RGBA8_Straight.toPremul(bufRGBA16_2, bufRGBA8, BENCH_PIXELS, nullptr);
#endif

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

    if (!allocateBuffers()) {
        return 1;
    }

    initTestData();
    initFormatBuffers();

#ifdef FLEXIMG_ENABLE_PREMUL
    // Prepare RGBA16_2 buffer for Premul benchmarks
    BuiltinFormats::RGBA8_Straight.toPremul(bufRGBA16_2, bufRGBA8, BENCH_PIXELS, nullptr);
#endif

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
