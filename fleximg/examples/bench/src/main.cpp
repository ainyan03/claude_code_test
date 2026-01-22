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
 *   a        : All benchmarks
 *   l        : List available formats
 *   h        : Help
 *
 *   [fmt] = all | rgb332 | rgb565le | rgb565be | rgb888 | bgr888 | rgba8 | rgba16p
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
#endif

// fleximg
#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/core/common.h"
#include "fleximg/image/pixel_format.h"

// fleximg implementation (header-only style for this benchmark)
#include "fleximg/image/pixel_format.cpp"

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
static uint8_t* bufRGB888 = nullptr;     // RGB888/BGR888 buffer
static uint8_t* bufRGB565 = nullptr;     // RGB565 buffer
static uint8_t* bufRGB332 = nullptr;     // RGB332 buffer
static uint16_t* bufRGBA16 = nullptr;    // RGBA16_Premultiplied buffer
static uint16_t* bufRGBA16_2 = nullptr;  // Second RGBA16 buffer for blending

static bool allocateBuffers() {
    bufRGBA8 = static_cast<uint8_t*>(malloc(BENCH_PIXELS * 4));
    bufRGB888 = static_cast<uint8_t*>(malloc(BENCH_PIXELS * 3));
    bufRGB565 = static_cast<uint8_t*>(malloc(BENCH_PIXELS * 2));
    bufRGB332 = static_cast<uint8_t*>(malloc(BENCH_PIXELS * 1));
    bufRGBA16 = static_cast<uint16_t*>(malloc(BENCH_PIXELS * 8));
    bufRGBA16_2 = static_cast<uint16_t*>(malloc(BENCH_PIXELS * 8));

    if (!bufRGBA8 || !bufRGB888 || !bufRGB565 || !bufRGB332 || !bufRGBA16 || !bufRGBA16_2) {
        benchPrintln("ERROR: Buffer allocation failed!");
        return false;
    }
    return true;
}

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
    {"RGBA16_Premul",   "rgba16p",  &BuiltinFormats::RGBA16_Premultiplied, nullptr, 8},
};
static constexpr int NUM_FORMATS = sizeof(formats) / sizeof(formats[0]);

static void initFormatBuffers() {
    formats[0].srcBuffer = bufRGB332;   // RGB332
    formats[1].srcBuffer = bufRGB565;   // RGB565_LE
    formats[2].srcBuffer = bufRGB565;   // RGB565_BE
    formats[3].srcBuffer = bufRGB888;   // RGB888
    formats[4].srcBuffer = bufRGB888;   // BGR888
    formats[5].srcBuffer = bufRGBA8;    // RGBA8_Straight
    formats[6].srcBuffer = reinterpret_cast<uint8_t*>(bufRGBA16_2);  // RGBA16_Premul
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
// BlendUnder Benchmark
// =============================================================================

static void benchBlendFormat(int idx) {
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

static void runBlendBenchmark(const char* fmtName) {
    benchPrintln();
    benchPrintln("=== BlendUnder Benchmark (Direct vs Indirect) ===");
    benchPrintf("Pixels: %d, Iterations: %d\n", BENCH_PIXELS, ITERATIONS);
    benchPrintln();
    benchPrintln("Format           Direct Indir  Ratio");
    benchPrintln("---------------- ------ ------ ------");

    if (strcmp(fmtName, "all") == 0) {
        for (int i = 0; i < NUM_FORMATS; i++) {
            benchBlendFormat(i);
        }
    } else {
        int idx = findFormat(fmtName);
        if (idx >= 0) {
            benchBlendFormat(idx);
        } else {
            benchPrintf("Unknown format: %s\n", fmtName);
        }
    }
    benchPrintln("(Ratio > 1 means Direct is faster)");
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
    benchPrintln("  b [fmt]  : BlendUnder benchmark");
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
    benchPrintln("Examples:");
    benchPrintln("  c all     - All conversion benchmarks");
    benchPrintln("  c rgb332  - RGB332 conversion only");
    benchPrintln("  b rgba8   - RGBA8 blend benchmark");
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
        case 'a':
        case 'A':
            runConvertBenchmark("all");
            runBlendBenchmark("all");
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

    // Prepare RGBA16_2 buffer with premul data for RGBA16_Premul tests
    BuiltinFormats::RGBA8_Straight.toPremul(bufRGBA16_2, bufRGBA8, BENCH_PIXELS, nullptr);

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

    // Prepare RGBA16_2 buffer
    BuiltinFormats::RGBA8_Straight.toPremul(bufRGBA16_2, bufRGBA8, BENCH_PIXELS, nullptr);

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
