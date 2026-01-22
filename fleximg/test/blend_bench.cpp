// fleximg blendUnder* Benchmark
// ピクセルフォーマット別のblendUnder関数の性能比較ベンチマーク
//
// 比較パターン:
//   直接パス: srcFormat->blendUnderPremul(dst, src, ...)
//   間接パス: srcFormat->toPremul(tmp, src, ...) + RGBA16Premul->blendUnderPremul(dst, tmp, ...)
//
// 対応環境:
//   - PC: std::chrono による計測
//   - FreeRTOS: vTaskDelay + 割り込み禁止による安定計測
//
// 使用方法:
//   ./test_runner --test-case="*benchmark*"

#include "doctest.h"
#include <cstring>
#include <vector>
#include <string>

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/image/pixel_format.h"

// =============================================================================
// Platform Detection and Timer Configuration
// =============================================================================

// FreeRTOS detection
#if defined(INCLUDE_vTaskDelay) || defined(configUSE_PREEMPTION)
    #define BENCH_USE_FREERTOS 1
    #include "FreeRTOS.h"
    #include "task.h"
#else
    #define BENCH_USE_FREERTOS 0
    #include <chrono>
#endif

namespace {

// =============================================================================
// Benchmark Configuration
// =============================================================================

// バッファサイズ（実機SRAMを考慮して小さめに設定）
constexpr int PIXEL_COUNT = 1024;

// 反復回数
constexpr int ITERATIONS = 1000;

// ウォームアップ回数
constexpr int WARMUP_ITERATIONS = 10;

// =============================================================================
// Timer Abstraction
// =============================================================================

#if BENCH_USE_FREERTOS

// FreeRTOS用タイマー（ティック単位）
using TimePoint = TickType_t;
using Duration = TickType_t;

inline void prepareForBenchmark() {
    // タスクスイッチを待ってから計測開始
    vTaskDelay(1);
}

inline void enterCriticalSection() {
    taskENTER_CRITICAL();
}

inline void exitCriticalSection() {
    taskEXIT_CRITICAL();
}

inline TimePoint now() {
    return xTaskGetTickCount();
}

inline Duration elapsed(TimePoint start, TimePoint end) {
    return end - start;
}

inline double toMicroseconds(Duration d) {
    // 1 tick = 1ms (typical), convert to microseconds
    return static_cast<double>(d) * 1000.0;
}

#else

// PC用タイマー（std::chrono）
using TimePoint = std::chrono::high_resolution_clock::time_point;
using Duration = std::chrono::nanoseconds;

inline void prepareForBenchmark() {
    // PC環境では特別な準備は不要
}

inline void enterCriticalSection() {
    // PC環境では割り込み禁止は不要
}

inline void exitCriticalSection() {
    // PC環境では割り込み禁止は不要
}

inline TimePoint now() {
    return std::chrono::high_resolution_clock::now();
}

inline Duration elapsed(TimePoint start, TimePoint end) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
}

inline double toMicroseconds(Duration d) {
    return static_cast<double>(d.count()) / 1000.0;
}

#endif

// =============================================================================
// Benchmark Utilities
// =============================================================================

// volatile sink to prevent optimization
volatile uint64_t g_sink = 0;

// Consume result to prevent dead code elimination
template<typename T>
void consumeResult(const T* data, size_t count) {
    uint64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += static_cast<uint64_t>(data[i]);
    }
    g_sink = sum;
}

// =============================================================================
// Format Test Data Generation
// =============================================================================

struct FormatTestData {
    const char* name;
    fleximg::PixelFormatID format;
    std::vector<uint8_t> srcData;
    size_t bytesPerPixel;
};

// RGB332: 1 byte per pixel
std::vector<uint8_t> generateRGB332Data(int count) {
    std::vector<uint8_t> data(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) {
        // Varying pattern
        data[static_cast<size_t>(i)] = static_cast<uint8_t>((i * 37) & 0xFF);
    }
    return data;
}

// RGB565: 2 bytes per pixel
std::vector<uint8_t> generateRGB565Data(int count) {
    std::vector<uint8_t> data(static_cast<size_t>(count) * 2);
    for (int i = 0; i < count; i++) {
        uint16_t val = static_cast<uint16_t>((i * 37) & 0xFFFF);
        data[static_cast<size_t>(i) * 2] = static_cast<uint8_t>(val & 0xFF);
        data[static_cast<size_t>(i) * 2 + 1] = static_cast<uint8_t>(val >> 8);
    }
    return data;
}

// RGB888/BGR888: 3 bytes per pixel
std::vector<uint8_t> generateRGB888Data(int count) {
    std::vector<uint8_t> data(static_cast<size_t>(count) * 3);
    for (int i = 0; i < count; i++) {
        data[static_cast<size_t>(i) * 3 + 0] = static_cast<uint8_t>((i * 37) & 0xFF);
        data[static_cast<size_t>(i) * 3 + 1] = static_cast<uint8_t>((i * 73) & 0xFF);
        data[static_cast<size_t>(i) * 3 + 2] = static_cast<uint8_t>((i * 111) & 0xFF);
    }
    return data;
}

// RGBA8_Straight: 4 bytes per pixel
std::vector<uint8_t> generateRGBA8Data(int count) {
    std::vector<uint8_t> data(static_cast<size_t>(count) * 4);
    for (int i = 0; i < count; i++) {
        data[static_cast<size_t>(i) * 4 + 0] = static_cast<uint8_t>((i * 37) & 0xFF);
        data[static_cast<size_t>(i) * 4 + 1] = static_cast<uint8_t>((i * 73) & 0xFF);
        data[static_cast<size_t>(i) * 4 + 2] = static_cast<uint8_t>((i * 111) & 0xFF);
        data[static_cast<size_t>(i) * 4 + 3] = static_cast<uint8_t>((i * 17) & 0xFF);  // varying alpha
    }
    return data;
}

// Initialize RGBA16_Premultiplied destination buffer
void initDstPremul(uint16_t* dst, int count) {
    for (int i = 0; i < count; i++) {
        // Semi-transparent gray pattern
        uint8_t v = static_cast<uint8_t>((i * 23) & 0xFF);
        uint8_t a = static_cast<uint8_t>(128 + ((i * 7) & 0x7F));  // 128-255
        uint16_t a_tmp = static_cast<uint16_t>(a) + 1;
        dst[i * 4 + 0] = static_cast<uint16_t>(v * a_tmp);
        dst[i * 4 + 1] = static_cast<uint16_t>(v * a_tmp);
        dst[i * 4 + 2] = static_cast<uint16_t>(v * a_tmp);
        dst[i * 4 + 3] = static_cast<uint16_t>(255 * a_tmp);
    }
}

// Initialize RGBA8_Straight destination buffer
void initDstStraight(uint8_t* dst, int count) {
    for (int i = 0; i < count; i++) {
        // Semi-transparent gray pattern
        uint8_t v = static_cast<uint8_t>((i * 23) & 0xFF);
        uint8_t a = static_cast<uint8_t>(128 + ((i * 7) & 0x7F));  // 128-255
        dst[i * 4 + 0] = v;
        dst[i * 4 + 1] = v;
        dst[i * 4 + 2] = v;
        dst[i * 4 + 3] = a;
    }
}

// =============================================================================
// Benchmark Result
// =============================================================================

struct BenchResult {
    const char* formatName;
    double directUs;    // Direct path time (microseconds)
    double indirectUs;  // Indirect path time (microseconds)
    double ratio;       // indirectUs / directUs
    bool correctnessOk; // Direct and indirect paths produce same result
    int mismatchCount;  // Number of mismatched pixels (0 if correctnessOk)
};

// =============================================================================
// Correctness Verification
// =============================================================================

// Compare two RGBA16 buffers, return number of mismatched pixels
int compareRGBA16Buffers(const uint16_t* a, const uint16_t* b, int pixelCount, uint16_t tolerance = 0) {
    int mismatches = 0;
    for (int i = 0; i < pixelCount; i++) {
        for (int c = 0; c < 4; c++) {
            int idx = i * 4 + c;
            int diff = static_cast<int>(a[idx]) - static_cast<int>(b[idx]);
            if (diff < 0) diff = -diff;
            if (diff > tolerance) {
                mismatches++;
                break;  // Count pixel, not channel
            }
        }
    }
    return mismatches;
}

// Compare two RGBA8 buffers, return number of mismatched pixels
int compareRGBA8Buffers(const uint8_t* a, const uint8_t* b, int pixelCount, uint8_t tolerance = 0) {
    int mismatches = 0;
    for (int i = 0; i < pixelCount; i++) {
        for (int c = 0; c < 4; c++) {
            int idx = i * 4 + c;
            int diff = static_cast<int>(a[idx]) - static_cast<int>(b[idx]);
            if (diff < 0) diff = -diff;
            if (diff > tolerance) {
                mismatches++;
                break;  // Count pixel, not channel
            }
        }
    }
    return mismatches;
}

// =============================================================================
// blendUnderPremul Benchmark
// =============================================================================

BenchResult benchBlendUnderPremul(const FormatTestData& testData) {
    using namespace fleximg;

    // Allocate buffers
    std::vector<uint16_t> dstDirect(static_cast<size_t>(PIXEL_COUNT) * 4);
    std::vector<uint16_t> dstIndirect(static_cast<size_t>(PIXEL_COUNT) * 4);
    std::vector<uint16_t> tempPremul(static_cast<size_t>(PIXEL_COUNT) * 4);

    // Warmup
    for (int w = 0; w < WARMUP_ITERATIONS; w++) {
        initDstPremul(dstDirect.data(), PIXEL_COUNT);
        if (testData.format->blendUnderPremul) {
            testData.format->blendUnderPremul(
                dstDirect.data(), testData.srcData.data(), PIXEL_COUNT, nullptr);
        }
    }

    // Direct path benchmark
    prepareForBenchmark();
    enterCriticalSection();
    TimePoint startDirect = now();

    for (int iter = 0; iter < ITERATIONS; iter++) {
        initDstPremul(dstDirect.data(), PIXEL_COUNT);
        if (testData.format->blendUnderPremul) {
            testData.format->blendUnderPremul(
                dstDirect.data(), testData.srcData.data(), PIXEL_COUNT, nullptr);
        }
    }

    TimePoint endDirect = now();
    exitCriticalSection();
    consumeResult(dstDirect.data(), dstDirect.size());

    // Indirect path benchmark
    prepareForBenchmark();
    enterCriticalSection();
    TimePoint startIndirect = now();

    for (int iter = 0; iter < ITERATIONS; iter++) {
        initDstPremul(dstIndirect.data(), PIXEL_COUNT);
        // Step 1: Convert src to Premul
        if (testData.format->toPremul) {
            testData.format->toPremul(
                tempPremul.data(), testData.srcData.data(), PIXEL_COUNT, nullptr);
        }
        // Step 2: Blend using RGBA16_Premultiplied
        if (PixelFormatIDs::RGBA16_Premultiplied->blendUnderPremul) {
            PixelFormatIDs::RGBA16_Premultiplied->blendUnderPremul(
                dstIndirect.data(), tempPremul.data(), PIXEL_COUNT, nullptr);
        }
    }

    TimePoint endIndirect = now();
    exitCriticalSection();
    consumeResult(dstIndirect.data(), dstIndirect.size());

    // Calculate results
    double directUs = toMicroseconds(elapsed(startDirect, endDirect)) / ITERATIONS;
    double indirectUs = toMicroseconds(elapsed(startIndirect, endIndirect)) / ITERATIONS;
    double ratio = (directUs > 0) ? (indirectUs / directUs) : 0;

    // Correctness verification: run both paths once and compare results
    initDstPremul(dstDirect.data(), PIXEL_COUNT);
    initDstPremul(dstIndirect.data(), PIXEL_COUNT);

    // Direct path
    testData.format->blendUnderPremul(
        dstDirect.data(), testData.srcData.data(), PIXEL_COUNT, nullptr);

    // Indirect path
    testData.format->toPremul(
        tempPremul.data(), testData.srcData.data(), PIXEL_COUNT, nullptr);
    PixelFormatIDs::RGBA16_Premultiplied->blendUnderPremul(
        dstIndirect.data(), tempPremul.data(), PIXEL_COUNT, nullptr);

    // Compare results
    int mismatches = compareRGBA16Buffers(dstDirect.data(), dstIndirect.data(), PIXEL_COUNT);
    bool correctnessOk = (mismatches == 0);

    return {testData.name, directUs, indirectUs, ratio, correctnessOk, mismatches};
}

// =============================================================================
// blendUnderStraight Benchmark
// =============================================================================

BenchResult benchBlendUnderStraight(const FormatTestData& testData) {
    using namespace fleximg;

    // Allocate buffers
    std::vector<uint8_t> dstDirect(static_cast<size_t>(PIXEL_COUNT) * 4);
    std::vector<uint8_t> dstIndirect(static_cast<size_t>(PIXEL_COUNT) * 4);
    std::vector<uint8_t> tempStraight(static_cast<size_t>(PIXEL_COUNT) * 4);

    // Warmup
    for (int w = 0; w < WARMUP_ITERATIONS; w++) {
        initDstStraight(dstDirect.data(), PIXEL_COUNT);
        if (testData.format->blendUnderStraight) {
            testData.format->blendUnderStraight(
                dstDirect.data(), testData.srcData.data(), PIXEL_COUNT, nullptr);
        }
    }

    // Direct path benchmark
    prepareForBenchmark();
    enterCriticalSection();
    TimePoint startDirect = now();

    for (int iter = 0; iter < ITERATIONS; iter++) {
        initDstStraight(dstDirect.data(), PIXEL_COUNT);
        if (testData.format->blendUnderStraight) {
            testData.format->blendUnderStraight(
                dstDirect.data(), testData.srcData.data(), PIXEL_COUNT, nullptr);
        }
    }

    TimePoint endDirect = now();
    exitCriticalSection();
    consumeResult(dstDirect.data(), dstDirect.size());

    // Indirect path benchmark
    prepareForBenchmark();
    enterCriticalSection();
    TimePoint startIndirect = now();

    for (int iter = 0; iter < ITERATIONS; iter++) {
        initDstStraight(dstIndirect.data(), PIXEL_COUNT);
        // Step 1: Convert src to Straight
        if (testData.format->toStraight) {
            testData.format->toStraight(
                tempStraight.data(), testData.srcData.data(), PIXEL_COUNT, nullptr);
        }
        // Step 2: Blend using RGBA8_Straight
        if (PixelFormatIDs::RGBA8_Straight->blendUnderStraight) {
            PixelFormatIDs::RGBA8_Straight->blendUnderStraight(
                dstIndirect.data(), tempStraight.data(), PIXEL_COUNT, nullptr);
        }
    }

    TimePoint endIndirect = now();
    exitCriticalSection();
    consumeResult(dstIndirect.data(), dstIndirect.size());

    // Calculate results
    double directUs = toMicroseconds(elapsed(startDirect, endDirect)) / ITERATIONS;
    double indirectUs = toMicroseconds(elapsed(startIndirect, endIndirect)) / ITERATIONS;
    double ratio = (directUs > 0) ? (indirectUs / directUs) : 0;

    // Correctness verification: run both paths once and compare results
    initDstStraight(dstDirect.data(), PIXEL_COUNT);
    initDstStraight(dstIndirect.data(), PIXEL_COUNT);

    // Direct path
    testData.format->blendUnderStraight(
        dstDirect.data(), testData.srcData.data(), PIXEL_COUNT, nullptr);

    // Indirect path
    testData.format->toStraight(
        tempStraight.data(), testData.srcData.data(), PIXEL_COUNT, nullptr);
    PixelFormatIDs::RGBA8_Straight->blendUnderStraight(
        dstIndirect.data(), tempStraight.data(), PIXEL_COUNT, nullptr);

    // Compare results
    int mismatches = compareRGBA8Buffers(dstDirect.data(), dstIndirect.data(), PIXEL_COUNT);
    bool correctnessOk = (mismatches == 0);

    return {testData.name, directUs, indirectUs, ratio, correctnessOk, mismatches};
}

// =============================================================================
// Result Output
// =============================================================================

void printResults(const char* title, const std::vector<BenchResult>& results) {
    MESSAGE(std::string(title));
    MESSAGE(std::string("Format          Direct(us)  Indirect(us)  Ratio  Correctness"));
    MESSAGE(std::string("------          ----------  ------------  -----  -----------"));

    for (const auto& r : results) {
        char buf[128];
        const char* correctStr = r.correctnessOk ? "OK" : "FAIL";
        if (r.correctnessOk) {
            std::snprintf(buf, sizeof(buf), "%-15s %10.2f  %12.2f  %.2fx  %s",
                          r.formatName, r.directUs, r.indirectUs, r.ratio, correctStr);
        } else {
            std::snprintf(buf, sizeof(buf), "%-15s %10.2f  %12.2f  %.2fx  %s (%d px)",
                          r.formatName, r.directUs, r.indirectUs, r.ratio, correctStr, r.mismatchCount);
        }
        MESSAGE(std::string(buf));
    }
    MESSAGE(std::string(""));
}

} // anonymous namespace

// =============================================================================
// Benchmark Test Cases
// =============================================================================

TEST_CASE("blendUnderPremul benchmark" * doctest::skip(false) * doctest::timeout(60)) {
    using namespace fleximg;

    // Prepare test data for each format
    std::vector<FormatTestData> testFormats = {
        {"RGB332", PixelFormatIDs::RGB332, generateRGB332Data(PIXEL_COUNT), 1},
        {"RGB565_LE", PixelFormatIDs::RGB565_LE, generateRGB565Data(PIXEL_COUNT), 2},
        {"RGB565_BE", PixelFormatIDs::RGB565_BE, generateRGB565Data(PIXEL_COUNT), 2},
        {"RGB888", PixelFormatIDs::RGB888, generateRGB888Data(PIXEL_COUNT), 3},
        {"BGR888", PixelFormatIDs::BGR888, generateRGB888Data(PIXEL_COUNT), 3},
        {"RGBA8_Straight", PixelFormatIDs::RGBA8_Straight, generateRGBA8Data(PIXEL_COUNT), 4},
    };

    std::vector<BenchResult> results;

    for (const auto& testData : testFormats) {
        // Skip if no blendUnderPremul or toPremul
        if (!testData.format->blendUnderPremul) {
            std::string msg = std::string("Skipping ") + testData.name + " (no blendUnderPremul)";
            MESSAGE(msg);
            continue;
        }
        if (!testData.format->toPremul) {
            std::string msg = std::string("Skipping ") + testData.name + " (no toPremul)";
            MESSAGE(msg);
            continue;
        }

        BenchResult result = benchBlendUnderPremul(testData);
        results.push_back(result);

        // Basic sanity check
        CHECK(result.directUs > 0);
        CHECK(result.indirectUs > 0);

        // Correctness check
        CHECK_MESSAGE(result.correctnessOk,
            "Direct/Indirect mismatch for " << testData.name << ": " << result.mismatchCount << " pixels differ");
    }

    printResults("[blendUnderPremul]", results);
}

TEST_CASE("blendUnderStraight benchmark" * doctest::skip(false) * doctest::timeout(60)) {
    using namespace fleximg;

    // Prepare test data for each format
    std::vector<FormatTestData> testFormats = {
        {"RGB332", PixelFormatIDs::RGB332, generateRGB332Data(PIXEL_COUNT), 1},
        {"RGB565_LE", PixelFormatIDs::RGB565_LE, generateRGB565Data(PIXEL_COUNT), 2},
        {"RGB565_BE", PixelFormatIDs::RGB565_BE, generateRGB565Data(PIXEL_COUNT), 2},
        {"RGB888", PixelFormatIDs::RGB888, generateRGB888Data(PIXEL_COUNT), 3},
        {"BGR888", PixelFormatIDs::BGR888, generateRGB888Data(PIXEL_COUNT), 3},
        {"RGBA8_Straight", PixelFormatIDs::RGBA8_Straight, generateRGBA8Data(PIXEL_COUNT), 4},
    };

    std::vector<BenchResult> results;

    for (const auto& testData : testFormats) {
        // Skip if no blendUnderStraight or toStraight
        if (!testData.format->blendUnderStraight) {
            std::string msg = std::string("Skipping ") + testData.name + " (no blendUnderStraight)";
            MESSAGE(msg);
            continue;
        }
        if (!testData.format->toStraight) {
            std::string msg = std::string("Skipping ") + testData.name + " (no toStraight)";
            MESSAGE(msg);
            continue;
        }

        BenchResult result = benchBlendUnderStraight(testData);
        results.push_back(result);

        // Basic sanity check
        CHECK(result.directUs > 0);
        CHECK(result.indirectUs > 0);

        // Correctness check
        CHECK_MESSAGE(result.correctnessOk,
            "Direct/Indirect mismatch for " << testData.name << ": " << result.mismatchCount << " pixels differ");
    }

    printResults("[blendUnderStraight]", results);
}

TEST_CASE("benchmark configuration info" * doctest::skip(false)) {
    MESSAGE("Benchmark Configuration:");
    MESSAGE("  PIXEL_COUNT: " << PIXEL_COUNT);
    MESSAGE("  ITERATIONS: " << ITERATIONS);
    MESSAGE("  WARMUP_ITERATIONS: " << WARMUP_ITERATIONS);
#if BENCH_USE_FREERTOS
    MESSAGE("  Timer: FreeRTOS tick");
    MESSAGE("  Critical section: taskENTER_CRITICAL/taskEXIT_CRITICAL");
#else
    MESSAGE("  Timer: std::chrono::high_resolution_clock");
    MESSAGE("  Critical section: none (PC environment)");
#endif
    CHECK(true);  // Always pass
}
