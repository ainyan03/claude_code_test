/**
 * @file main.cpp
 * @brief fleximg ピクセルフォーマット変換のベンチマーク（M5Stack用）
 *
 * 用途:
 * - ピクセルフォーマット変換関数（toPremul/fromPremul等）の単体性能測定
 * - fromPremul実装方式（テーブル vs 除算）の比較
 * - blendUnder合成の方式別（Premul vs Straight）性能比較
 * - アルファパターン（透明/半透明/不透明）別の処理速度検証
 *
 * ビルド: PlatformIOでM5Stack向けにビルド
 * 操作:
 *   - ボタンA: 通常ベンチマーク実行
 *   - ボタンB: blendUnderのsrc×dstアルファパターンマトリクス測定
 */

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
static uint8_t* canvasRGBA8 = nullptr;   // blendUnderStraight用キャンバス (4096 * 4 = 16KB)

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
    canvasRGBA8 = static_cast<uint8_t*>(malloc(BENCH_PIXELS * 4));

    if (!srcRGBA8 || !dstRGB565 || !dstRGBA16 || !canvasRGBA16 || !canvasRGBA8) {
        Serial.println("ERROR: Failed to allocate buffers!");
        return false;
    }

    Serial.printf("Buffers allocated: src=%p, canvas8=%p, canvas16=%p\n",
                  srcRGBA8, canvasRGBA8, canvasRGBA16);
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

// キャンバスを指定パターンで初期化（blendUnderPremul用、RGBA16_Premultiplied形式）
void initCanvas(AlphaPattern pattern) {
    if (!canvasRGBA16) return;

    for (int i = 0; i < BENCH_PIXELS; i++) {
        int idx = i * 4;
        uint16_t alpha;

        switch (pattern) {
            case AlphaPattern::AllOpaque:
                alpha = 65535;
                break;
            case AlphaPattern::AllTransparent:
                alpha = 0;
                break;
            case AlphaPattern::AllSemi:
                alpha = 32768;  // 約50%
                break;
            case AlphaPattern::Mixed:
            default:
                // srcと同じ96ピクセル周期パターン
                {
                    int phase = i % 96;
                    if (phase < 32) {
                        alpha = 0;
                    } else if (phase < 48) {
                        alpha = static_cast<uint16_t>(4096 + (phase - 32) * 3855);
                    } else if (phase < 80) {
                        alpha = 65535;
                    } else {
                        alpha = static_cast<uint16_t>(4096 + (95 - phase) * 3855);
                    }
                }
                break;
        }

        // Premultiplied形式: RGB値もαでスケール
        // 緑色ベース
        canvasRGBA16[idx + 0] = 0;      // R
        canvasRGBA16[idx + 1] = alpha;  // G (premul: G * α / 65535 = α when G=65535)
        canvasRGBA16[idx + 2] = 0;      // B
        canvasRGBA16[idx + 3] = alpha;  // A
    }
}

// 後方互換性のためのラッパー
void initCanvasHalfTransparent() {
    initCanvas(AlphaPattern::AllSemi);
}

// RGBA8_Straight用キャンバス初期化
void initCanvas8(AlphaPattern pattern) {
    if (!canvasRGBA8) return;

    for (int i = 0; i < BENCH_PIXELS; i++) {
        int idx = i * 4;
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

        // Straight形式: 緑色ベース
        canvasRGBA8[idx + 0] = 0;      // R
        canvasRGBA8[idx + 1] = 255;    // G (straight: 緑)
        canvasRGBA8[idx + 2] = 0;      // B
        canvasRGBA8[idx + 3] = alpha;  // A
    }
}

// ========================================================================
// blendUnderStraight 実装（ベンチマーク用）
// ========================================================================
// under合成: result = dst + src * (1 - dstA)
// Straight形式なので、最終的にunpremultiplyが必要

static void blendUnderStraight_impl(
    uint8_t* __restrict__ dst,
    const uint8_t* __restrict__ src,
    int pixelCount
) {
    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;
        uint_fast16_t srcR = src[idx + 0];
        uint_fast16_t srcG = src[idx + 1];
        uint_fast16_t srcB = src[idx + 2];
        uint_fast16_t srcA = src[idx + 3];
        uint_fast16_t dstR = dst[idx + 0];
        uint_fast16_t dstG = dst[idx + 1];
        uint_fast16_t dstB = dst[idx + 2];
        uint_fast16_t dstA = dst[idx + 3];

        // dstが不透明ならスキップ
        if (dstA == 255) continue;
        // srcが透明ならスキップ
        if (srcA == 0) continue;

        // dstが透明なら単純コピー
        if (dstA == 0) {
            dst[idx + 0] = static_cast<uint8_t>(srcR);
            dst[idx + 1] = static_cast<uint8_t>(srcG);
            dst[idx + 2] = static_cast<uint8_t>(srcB);
            dst[idx + 3] = static_cast<uint8_t>(srcA);
            continue;
        }

        // under合成（Straight形式）
        // invDstA = 255 - dstA (0-255スケール)
        uint_fast16_t invDstA = 255 - dstA;

        // resultA = dstA + srcA * invDstA / 255
        uint_fast16_t resultA = dstA + (srcA * invDstA + 127) / 255;

        // Premultiplied計算:
        // resultR_premul = dstR * dstA + srcR * srcA * invDstA / 255
        // resultR = resultR_premul / resultA
        uint_fast32_t resultR_premul = dstR * dstA + (srcR * srcA * invDstA + 127) / 255;
        uint_fast32_t resultG_premul = dstG * dstA + (srcG * srcA * invDstA + 127) / 255;
        uint_fast32_t resultB_premul = dstB * dstA + (srcB * srcA * invDstA + 127) / 255;

        // Unpremultiply (除算)
        dst[idx + 0] = static_cast<uint8_t>(resultR_premul / resultA);
        dst[idx + 1] = static_cast<uint8_t>(resultG_premul / resultA);
        dst[idx + 2] = static_cast<uint8_t>(resultB_premul / resultA);
        dst[idx + 3] = static_cast<uint8_t>(resultA);
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
// fromPremul実装比較ベンチマーク
// テーブル(現在) vs テーブル+四捨五入 vs 除算
// ========================================================================

// 逆数テーブル（現在の実装: 切り捨て）
static constexpr uint16_t calcInvFloor(int a) {
    return (a == 0) ? 0 : static_cast<uint16_t>(65536u / static_cast<uint32_t>(a + 1));
}

// 逆数テーブル（改良案: 四捨五入）
static constexpr uint16_t calcInvRound(int a) {
    if (a == 0) return 0;
    uint32_t divisor = static_cast<uint32_t>(a + 1);
    return static_cast<uint16_t>((65536u + divisor / 2) / divisor);
}

// テーブル生成用ヘルパーマクロ
#define INV_TABLE_ROW(base) \
    calcInvFloor(base+0), calcInvFloor(base+1), calcInvFloor(base+2), calcInvFloor(base+3), \
    calcInvFloor(base+4), calcInvFloor(base+5), calcInvFloor(base+6), calcInvFloor(base+7), \
    calcInvFloor(base+8), calcInvFloor(base+9), calcInvFloor(base+10), calcInvFloor(base+11), \
    calcInvFloor(base+12), calcInvFloor(base+13), calcInvFloor(base+14), calcInvFloor(base+15)

#define INV_TABLE_ROW_R(base) \
    calcInvRound(base+0), calcInvRound(base+1), calcInvRound(base+2), calcInvRound(base+3), \
    calcInvRound(base+4), calcInvRound(base+5), calcInvRound(base+6), calcInvRound(base+7), \
    calcInvRound(base+8), calcInvRound(base+9), calcInvRound(base+10), calcInvRound(base+11), \
    calcInvRound(base+12), calcInvRound(base+13), calcInvRound(base+14), calcInvRound(base+15)

alignas(64) static constexpr uint16_t invTableFloor[256] = {
    INV_TABLE_ROW(0), INV_TABLE_ROW(16), INV_TABLE_ROW(32), INV_TABLE_ROW(48),
    INV_TABLE_ROW(64), INV_TABLE_ROW(80), INV_TABLE_ROW(96), INV_TABLE_ROW(112),
    INV_TABLE_ROW(128), INV_TABLE_ROW(144), INV_TABLE_ROW(160), INV_TABLE_ROW(176),
    INV_TABLE_ROW(192), INV_TABLE_ROW(208), INV_TABLE_ROW(224), INV_TABLE_ROW(240)
};

alignas(64) static constexpr uint16_t invTableRound[256] = {
    INV_TABLE_ROW_R(0), INV_TABLE_ROW_R(16), INV_TABLE_ROW_R(32), INV_TABLE_ROW_R(48),
    INV_TABLE_ROW_R(64), INV_TABLE_ROW_R(80), INV_TABLE_ROW_R(96), INV_TABLE_ROW_R(112),
    INV_TABLE_ROW_R(128), INV_TABLE_ROW_R(144), INV_TABLE_ROW_R(160), INV_TABLE_ROW_R(176),
    INV_TABLE_ROW_R(192), INV_TABLE_ROW_R(208), INV_TABLE_ROW_R(224), INV_TABLE_ROW_R(240)
};

#undef INV_TABLE_ROW
#undef INV_TABLE_ROW_R

// 方式A: 切り捨てテーブル + 切り捨て演算（現在の実装）
static void fromPremul_tableFloor(uint8_t* __restrict__ dst, const uint16_t* __restrict__ src, int pixelCount) {
    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;
        uint8_t a8 = static_cast<uint8_t>(src[idx + 3] >> 8);
        uint32_t inv = invTableFloor[a8];
        dst[idx + 0] = static_cast<uint8_t>((src[idx + 0] * inv) >> 16);
        dst[idx + 1] = static_cast<uint8_t>((src[idx + 1] * inv) >> 16);
        dst[idx + 2] = static_cast<uint8_t>((src[idx + 2] * inv) >> 16);
        dst[idx + 3] = a8;
    }
}

// 方式D: 四捨五入テーブル + 四捨五入演算（改良案）
static void fromPremul_tableRound(uint8_t* __restrict__ dst, const uint16_t* __restrict__ src, int pixelCount) {
    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;
        uint8_t a8 = static_cast<uint8_t>(src[idx + 3] >> 8);
        uint32_t inv = invTableRound[a8];
        dst[idx + 0] = static_cast<uint8_t>((src[idx + 0] * inv + 32768) >> 16);
        dst[idx + 1] = static_cast<uint8_t>((src[idx + 1] * inv + 32768) >> 16);
        dst[idx + 2] = static_cast<uint8_t>((src[idx + 2] * inv + 32768) >> 16);
        dst[idx + 3] = a8;
    }
}

// 方式E: 純粋な除算（精度100%だが遅い）
static void fromPremul_division(uint8_t* __restrict__ dst, const uint16_t* __restrict__ src, int pixelCount) {
    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;
        uint8_t a8 = static_cast<uint8_t>(src[idx + 3] >> 8);
        uint16_t a_tmp = a8 + 1;
        dst[idx + 0] = static_cast<uint8_t>(src[idx + 0] / a_tmp);
        dst[idx + 1] = static_cast<uint8_t>(src[idx + 1] / a_tmp);
        dst[idx + 2] = static_cast<uint8_t>(src[idx + 2] / a_tmp);
        dst[idx + 3] = a8;
    }
}

void benchFromPremulVariants() {
    Serial.println("\n--- fromPremul Implementation Comparison ---");
    M5.Display.println("\n-- fromPremul cmp --");

    // 先にPremulデータを準備
    BuiltinFormats::RGBA8_Straight.toPremul(
        dstRGBA16, srcRGBA8, BENCH_PIXELS, nullptr);

    // 方式A: テーブル(floor)+演算(floor) - 現在の実装
    auto resultA = runBench("tbl_floor+floor", []() {
        fromPremul_tableFloor(srcRGBA8, dstRGBA16, BENCH_PIXELS);
    });
    results[resultCount++] = resultA;
    printResult(resultA);

    // 方式D: テーブル(round)+演算(round) - 改良案
    auto resultD = runBench("tbl_round+round", []() {
        fromPremul_tableRound(srcRGBA8, dstRGBA16, BENCH_PIXELS);
    });
    results[resultCount++] = resultD;
    printResult(resultD);

    // 方式E: 除算
    auto resultE = runBench("division", []() {
        fromPremul_division(srcRGBA8, dstRGBA16, BENCH_PIXELS);
    });
    results[resultCount++] = resultE;
    printResult(resultE);

    // 比較結果
    Serial.printf("\nComparison: round/floor=%.2fx, div/floor=%.2fx\n",
                  static_cast<float>(resultD.perFrameUs) / resultA.perFrameUs,
                  static_cast<float>(resultE.perFrameUs) / resultA.perFrameUs);
}

// ========================================================================
// blendUnder比較ベンチマーク
// Premul方式 vs Straight方式
// ========================================================================

static AlphaPattern g_canvas8Pattern = AlphaPattern::AllSemi;

void benchBlendUnderComparison() {
    Serial.println("\n--- blendUnder: Premul vs Straight ---");
    M5.Display.println("\n-- blendUnder cmp --");

    // 方式1: RGBA8 → blendUnderPremul → RGBA16 (現在の方式)
    // toPremul変換 + blendUnder + fromPremul変換 の合計時間
    auto resultPremul = runBench("via_Premul(16bit)", []() {
        initCanvas(AlphaPattern::AllSemi);
        BuiltinFormats::RGBA8_Straight.blendUnderPremul(
            canvasRGBA16, srcRGBA8, BENCH_PIXELS, nullptr);
    });
    results[resultCount++] = resultPremul;
    printResult(resultPremul);

    // 方式2: RGBA8 → blendUnderStraight → RGBA8 (新方式)
    auto resultStraight = runBench("Straight(8bit)", []() {
        initCanvas8(AlphaPattern::AllSemi);
        blendUnderStraight_impl(canvasRGBA8, srcRGBA8, BENCH_PIXELS);
    });
    results[resultCount++] = resultStraight;
    printResult(resultStraight);

    // 比較結果
    Serial.printf("\nComparison: Straight/Premul=%.2fx\n",
                  static_cast<float>(resultStraight.perFrameUs) / resultPremul.perFrameUs);

    if (resultStraight.perFrameUs < resultPremul.perFrameUs) {
        Serial.println("=> Straight is FASTER");
    } else {
        Serial.println("=> Premul is FASTER");
    }
}

// ========================================================================
// 直接パス vs 間接パス比較ベンチマーク
// Direct: srcFormat->blendUnderPremul(dst, src, ...)
// Indirect: srcFormat->toPremul(tmp, src, ...) + RGBA16Premul->blendUnderPremul(dst, tmp, ...)
// ========================================================================

// 間接パス用の一時バッファ
static uint16_t* tempPremul = nullptr;
static uint8_t* tempStraight = nullptr;

bool allocatePathCompareBuffers() {
    tempPremul = static_cast<uint16_t*>(malloc(BENCH_PIXELS * 8));
    tempStraight = static_cast<uint8_t*>(malloc(BENCH_PIXELS * 4));
    return (tempPremul != nullptr && tempStraight != nullptr);
}

struct PathCompareResult {
    const char* formatName;
    uint32_t directUs;
    uint32_t indirectUs;
    float ratio;
    bool correctnessOk;
    int mismatchCount;
};

// RGBA16バッファ比較（ピクセル単位でミスマッチ数をカウント）
int compareRGBA16Buffers(const uint16_t* a, const uint16_t* b, int pixelCount) {
    int mismatches = 0;
    for (int i = 0; i < pixelCount; i++) {
        for (int c = 0; c < 4; c++) {
            int idx = i * 4 + c;
            if (a[idx] != b[idx]) {
                mismatches++;
                break;  // ピクセル単位でカウント
            }
        }
    }
    return mismatches;
}

// 直接パス vs 間接パス比較（blendUnderPremul）
void runBlendUnderPremulPathCompare() {
    Serial.println("\n========================================");
    Serial.println("blendUnderPremul: Direct vs Indirect");
    Serial.println("========================================\n");

    // tempPremulが確保されていない場合はエラー
    if (!tempPremul) {
        Serial.println("ERROR: tempPremul buffer not allocated!");
        Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
        return;
    }

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("Direct vs Indirect\n");

    // テストフォーマット
    struct FormatInfo {
        const char* name;
        const PixelFormatDescriptor* format;
        uint8_t* srcData;
        size_t bytesPerPixel;
    };

    // RGB332データ生成
    static uint8_t srcRGB332[BENCH_PIXELS];
    for (int i = 0; i < BENCH_PIXELS; i++) {
        srcRGB332[i] = static_cast<uint8_t>((i * 37) & 0xFF);
    }

    // RGB565データ生成
    static uint8_t srcRGB565[BENCH_PIXELS * 2];
    for (int i = 0; i < BENCH_PIXELS; i++) {
        uint16_t val = static_cast<uint16_t>((i * 37) & 0xFFFF);
        srcRGB565[i * 2] = static_cast<uint8_t>(val & 0xFF);
        srcRGB565[i * 2 + 1] = static_cast<uint8_t>(val >> 8);
    }

    // RGB888データ生成
    static uint8_t srcRGB888[BENCH_PIXELS * 3];
    for (int i = 0; i < BENCH_PIXELS; i++) {
        srcRGB888[i * 3 + 0] = static_cast<uint8_t>((i * 37) & 0xFF);
        srcRGB888[i * 3 + 1] = static_cast<uint8_t>((i * 73) & 0xFF);
        srcRGB888[i * 3 + 2] = static_cast<uint8_t>((i * 111) & 0xFF);
    }

    FormatInfo formats[] = {
        {"RGB332", &BuiltinFormats::RGB332, srcRGB332, 1},
        {"RGB565_LE", &BuiltinFormats::RGB565_LE, srcRGB565, 2},
        {"RGB565_BE", &BuiltinFormats::RGB565_BE, srcRGB565, 2},
        {"RGB888", &BuiltinFormats::RGB888, srcRGB888, 3},
        {"BGR888", &BuiltinFormats::BGR888, srcRGB888, 3},
        {"RGBA8_St", &BuiltinFormats::RGBA8_Straight, srcRGBA8, 4},
    };
    constexpr int numFormats = sizeof(formats) / sizeof(formats[0]);

    // 正確性チェック用の追加バッファ（遅延確保）
    // メモリ節約のため、canvasRGBA16を一時的に正確性チェックに再利用
    // verifyDirectにはcanvasRGBA16をコピーして使い、verifyIndirectは新規確保
    static uint16_t* verifyIndirect = nullptr;
    bool canVerify = true;
    if (!verifyIndirect) {
        verifyIndirect = static_cast<uint16_t*>(malloc(BENCH_PIXELS * 8));
        if (!verifyIndirect) {
            Serial.println("Warning: Could not allocate verify buffer, skipping correctness check");
            canVerify = false;
        }
    }

    Serial.printf("%-12s %8s %8s %6s %s\n", "Format", "Direct", "Indirect", "Ratio", "Check");
    Serial.println("------------------------------------------------------");
    M5.Display.printf("%-8s %4s %4s %4s %s\n", "Format", "Dir", "Ind", "Rat", "Chk");

    bool allCorrect = true;

    for (int f = 0; f < numFormats; f++) {
        const auto& fmt = formats[f];

        // 関数が存在するか確認
        if (!fmt.format->blendUnderPremul || !fmt.format->toPremul) {
            Serial.printf("%-12s SKIP (no function)\n", fmt.name);
            continue;
        }

        // 直接パス計測
        auto directResult = runBench("direct", [&]() {
            initCanvas(AlphaPattern::AllSemi);
            fmt.format->blendUnderPremul(canvasRGBA16, fmt.srcData, BENCH_PIXELS, nullptr);
        });

        // 間接パス計測
        auto indirectResult = runBench("indirect", [&]() {
            initCanvas(AlphaPattern::AllSemi);
            fmt.format->toPremul(tempPremul, fmt.srcData, BENCH_PIXELS, nullptr);
            BuiltinFormats::RGBA16_Premultiplied.blendUnderPremul(
                canvasRGBA16, tempPremul, BENCH_PIXELS, nullptr);
        });

        // 正確性チェック
        bool correctnessOk = true;
        int mismatches = 0;
        const char* checkStr = "-";

        if (canVerify && verifyIndirect) {
            // canvasRGBA16を直接パス結果用に使用
            initCanvas(AlphaPattern::AllSemi);
            // 直接パス: canvasRGBA16に結果を格納
            fmt.format->blendUnderPremul(canvasRGBA16, fmt.srcData, BENCH_PIXELS, nullptr);

            // 間接パス: verifyIndirectに結果を格納
            initCanvas(AlphaPattern::AllSemi);
            memcpy(verifyIndirect, canvasRGBA16, BENCH_PIXELS * 8);
            fmt.format->toPremul(tempPremul, fmt.srcData, BENCH_PIXELS, nullptr);
            BuiltinFormats::RGBA16_Premultiplied.blendUnderPremul(
                verifyIndirect, tempPremul, BENCH_PIXELS, nullptr);

            // 再度直接パスを実行（canvasRGBA16がinitCanvasで上書きされたため）
            initCanvas(AlphaPattern::AllSemi);
            fmt.format->blendUnderPremul(canvasRGBA16, fmt.srcData, BENCH_PIXELS, nullptr);

            mismatches = compareRGBA16Buffers(canvasRGBA16, verifyIndirect, BENCH_PIXELS);
            correctnessOk = (mismatches == 0);
            if (!correctnessOk) allCorrect = false;
            checkStr = correctnessOk ? "OK" : "FAIL";
        }

        float ratio = (directResult.perFrameUs > 0)
            ? static_cast<float>(indirectResult.perFrameUs) / directResult.perFrameUs
            : 0;

        Serial.printf("%-12s %8u %8u %5.2fx %s",
                      fmt.name, directResult.perFrameUs, indirectResult.perFrameUs, ratio, checkStr);
        if (!correctnessOk && mismatches > 0) {
            Serial.printf(" (%d px)", mismatches);
        }
        Serial.println();

        M5.Display.printf("%-8s %4u %4u %3.1f %s\n",
                          fmt.name, directResult.perFrameUs, indirectResult.perFrameUs, ratio, checkStr);
    }

    Serial.println("------------------------------------------------------");
    if (canVerify) {
        Serial.printf("Correctness: %s\n", allCorrect ? "ALL OK" : "SOME FAILED");
    } else {
        Serial.println("Correctness: SKIPPED (insufficient memory)");
    }
    Serial.println("Ratio = Indirect / Direct (>1 means Direct is faster)");
    M5.Display.printf("\nResult: %s\n", canVerify ? (allCorrect ? "ALL OK" : "FAIL") : "NO CHK");
    M5.Display.println("BtnA:main BtnC:path");
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

    // fromPremul実装比較
    benchFromPremulVariants();

    // blendUnder比較（Premul vs Straight）
    benchBlendUnderComparison();

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

// 現在のベンチマーク用キャンバスパターン（グローバル、ラムダから参照）
static AlphaPattern g_canvasPattern = AlphaPattern::AllSemi;

// アルファパターン別ベンチマーク（blendUnderPremulの条件分岐影響測定）
// src × dst の 4×4 総当たりテスト
void runBlendUnderBenchByPattern() {
    Serial.println("\n========================================");
    Serial.println("blendUnderPremul: src x dst Matrix");
    Serial.println("========================================\n");

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextSize(1);
    M5.Display.println("BlendUnder src x dst\n");

    const char* shortNames[] = {"Mix", "Opq", "Trn", "Sem"};
    const char* patternNames[] = {"Mixed", "Opaque", "Transp", "Semi"};
    AlphaPattern patterns[] = {
        AlphaPattern::Mixed,
        AlphaPattern::AllOpaque,
        AlphaPattern::AllTransparent,
        AlphaPattern::AllSemi
    };

    // ヘッダー出力（シリアル）
    Serial.printf("%-8s", "src\\dst");
    for (int d = 0; d < 4; d++) {
        Serial.printf(" %7s", shortNames[d]);
    }
    Serial.println(" (us/frame)");
    Serial.println("----------------------------------------");

    // ヘッダー出力（ディスプレイ）
    M5.Display.printf("src\\dst");
    for (int d = 0; d < 4; d++) {
        M5.Display.printf(" %4s", shortNames[d]);
    }
    M5.Display.println();

    // 4×4マトリクス測定
    for (int s = 0; s < 4; s++) {
        Serial.printf("%-8s", patternNames[s]);
        M5.Display.printf("%-7s", shortNames[s]);

        initTestData(patterns[s]);

        for (int d = 0; d < 4; d++) {
            g_canvasPattern = patterns[d];

            auto result = runBench("", []() {
                initCanvas(g_canvasPattern);
                BuiltinFormats::RGBA8_Straight.blendUnderPremul(
                    canvasRGBA16, srcRGBA8, BENCH_PIXELS, nullptr);
            });

            Serial.printf(" %7u", result.perFrameUs);
            M5.Display.printf(" %4u", result.perFrameUs);
        }
        Serial.println();
        M5.Display.println();
    }

    Serial.println("----------------------------------------");
    Serial.println("Row=src pattern, Col=dst(canvas) pattern");
    Serial.println("========================================\n");

    M5.Display.println("\nBtnA:main BtnB:matrix");
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

    if (!allocatePathCompareBuffers()) {
        Serial.println("Warning: Path compare buffers allocation failed");
    }

    Serial.printf("Free heap after alloc: %d bytes\n", ESP.getFreeHeap());

    initTestData();
    runAllBenchmarks();
}

void loop() {
    M5.update();

    // シリアル入力による制御
    if (Serial.available()) {
        char cmd = Serial.read();
        switch (cmd) {
            case 'a':
            case 'A':
            case '1':
                Serial.println(">> Running main benchmark...");
                runAllBenchmarks();
                break;
            case 'b':
            case 'B':
            case '2':
                Serial.println(">> Running alpha pattern matrix...");
                runBlendUnderBenchByPattern();
                break;
            case 'c':
            case 'C':
            case '3':
                Serial.println(">> Running direct vs indirect comparison...");
                runBlendUnderPremulPathCompare();
                break;
            case 'h':
            case 'H':
            case '?':
                Serial.println("\n=== Serial Commands ===");
                Serial.println("  a/1: Main benchmark");
                Serial.println("  b/2: Alpha pattern matrix");
                Serial.println("  c/3: Direct vs Indirect comparison");
                Serial.println("  h/?: Show this help");
                Serial.println("=======================\n");
                break;
            default:
                // 改行等は無視
                if (cmd >= ' ') {
                    Serial.printf("Unknown command: '%c'. Type 'h' for help.\n", cmd);
                }
                break;
        }
    }

    // ボタンAで通常ベンチマーク
    if (M5.BtnA.wasPressed()) {
        runAllBenchmarks();
    }

    // ボタンBでアルファパターン別ベンチマーク
    if (M5.BtnB.wasPressed()) {
        runBlendUnderBenchByPattern();
    }

    // ボタンCで直接パス vs 間接パス比較
    if (M5.BtnC.wasPressed()) {
        runBlendUnderPremulPathCompare();
    }

    delay(10);
}
