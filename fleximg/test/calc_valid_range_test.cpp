// fleximg calcValidRange Unit Tests
// コンパイル: g++ -std=c++17 -I../src calc_valid_range_test.cpp -o calc_valid_range_test

#include <iostream>
#include <cstdint>
#include <utility>
#include "fleximg/operations/transform.h"

using namespace fleximg;
using namespace fleximg::transform;

int testsPassed = 0;
int testsFailed = 0;

void check(const char* name, bool condition) {
    if (condition) {
        std::cout << "[PASS] " << name << std::endl;
        testsPassed++;
    } else {
        std::cout << "[FAIL] " << name << std::endl;
        testsFailed++;
    }
}

// ==============================================================================
// DDAシミュレーション: 実際のDDAループで有効な dx を全探索して取得
// ==============================================================================
//
// 実際のDDAループ:
//   for (int dx = dxStart; dx <= dxEnd; dx++) {
//       int32_t srcX_fixed = coeff * dx + base + (coeff >> 1);
//       uint32_t sx = static_cast<uint32_t>(srcX_fixed) >> FIXED_POINT_BITS;
//       if (sx < static_cast<uint32_t>(srcSize)) { ... }
//   }
//
std::pair<int, int> simulateDDA(int32_t coeff, int32_t base, int srcSize, int canvasSize) {
    int firstValid = -1;
    int lastValid = -1;

    int32_t baseWithHalf = base + (coeff >> 1);

    for (int dx = 0; dx < canvasSize; dx++) {
        int32_t srcX_fixed = coeff * dx + baseWithHalf;
        // 符号付き右シフトでインデックスを計算
        // 注: 負の値の右シフトは実装依存だが、実際のDDAでは uint32_t キャストで処理
        int srcIdx;
        if (srcX_fixed >= 0) {
            srcIdx = srcX_fixed >> FIXED_POINT_BITS;
        } else {
            // 負の場合は範囲外
            srcIdx = -1;
        }

        bool valid = (srcIdx >= 0 && srcIdx < srcSize);

        if (valid) {
            if (firstValid == -1) firstValid = dx;
            lastValid = dx;
        }
    }

    if (firstValid == -1) {
        // 有効なピクセルなし
        return {1, 0};
    }
    return {firstValid, lastValid};
}

// ==============================================================================
// 比較テスト
// ==============================================================================

void testCase(const char* name, int32_t coeff, int32_t base, int srcSize, int canvasSize) {
    auto [calcStart, calcEnd] = calcValidRange(coeff, base, srcSize, canvasSize);
    auto [simStart, simEnd] = simulateDDA(coeff, base, srcSize, canvasSize);

    // 「有効範囲なし」の場合は両方とも同じ表記に正規化
    bool calcNoValid = (calcStart > calcEnd);
    bool simNoValid = (simStart > simEnd);

    // calcValidRange の結果を canvasSize 内にクランプ（有効範囲ありの場合のみ）
    if (!calcNoValid) {
        if (calcStart < 0) calcStart = 0;
        if (calcEnd >= canvasSize) calcEnd = canvasSize - 1;
        // クランプ後に無効になる可能性
        if (calcStart > calcEnd) calcNoValid = true;
    }

    bool match;
    if (calcNoValid && simNoValid) {
        // 両方とも「有効範囲なし」
        match = true;
    } else {
        match = (calcStart == simStart && calcEnd == simEnd);
    }

    if (!match) {
        std::cout << "[FAIL] " << name << std::endl;
        std::cout << "  coeff=" << coeff << ", base=" << base
                  << ", srcSize=" << srcSize << ", canvasSize=" << canvasSize << std::endl;
        std::cout << "  calcValidRange: [" << calcStart << ", " << calcEnd << "]" << std::endl;
        std::cout << "  simulateDDA:    [" << simStart << ", " << simEnd << "]" << std::endl;
        testsFailed++;
    } else {
        std::cout << "[PASS] " << name << std::endl;
        testsPassed++;
    }
}

// ==============================================================================
// 基本テスト
// ==============================================================================

void testCoeffZero() {
    // 係数がゼロの場合、全dxで同じsrcIdxになる

    // srcIdx が有効範囲内
    testCase("CoeffZero_InRange", 0, 32 << FIXED_POINT_BITS, 100, 200);

    // srcIdx が有効範囲外（負）
    testCase("CoeffZero_Negative", 0, -(1 << FIXED_POINT_BITS), 100, 200);

    // srcIdx が有効範囲外（大きすぎ）
    testCase("CoeffZero_TooLarge", 0, 100 << FIXED_POINT_BITS, 100, 200);

    // 境界値: srcIdx = 0
    testCase("CoeffZero_AtZero", 0, 0, 100, 200);

    // 境界値: srcIdx = srcSize - 1
    testCase("CoeffZero_AtMax", 0, 99 << FIXED_POINT_BITS, 100, 200);
}

void testPositiveCoeff() {
    // 正の係数: dxが増えるとsrcIdxも増える

    // 等倍スケール
    int32_t scale1 = FIXED_POINT_SCALE;
    testCase("PositiveCoeff_Scale1_Origin0", scale1, 0, 100, 100);
    testCase("PositiveCoeff_Scale1_OriginNeg", scale1, -(50 << FIXED_POINT_BITS), 100, 150);
    testCase("PositiveCoeff_Scale1_OriginPos", scale1, 50 << FIXED_POINT_BITS, 100, 150);

    // 2倍スケール（縮小表示）
    int32_t scale2 = FIXED_POINT_SCALE * 2;
    testCase("PositiveCoeff_Scale2_Origin0", scale2, 0, 100, 50);
    testCase("PositiveCoeff_Scale2_OriginNeg", scale2, -(10 << FIXED_POINT_BITS), 100, 60);

    // 0.5倍スケール（拡大表示）
    int32_t scaleHalf = FIXED_POINT_SCALE / 2;
    testCase("PositiveCoeff_ScaleHalf_Origin0", scaleHalf, 0, 100, 200);
    testCase("PositiveCoeff_ScaleHalf_OriginNeg", scaleHalf, -(50 << FIXED_POINT_BITS), 100, 300);
}

void testNegativeCoeff() {
    // 負の係数: dxが増えるとsrcIdxが減る（反転）

    int32_t scaleNeg1 = -FIXED_POINT_SCALE;
    testCase("NegativeCoeff_Scale-1_HighBase", scaleNeg1, 99 << FIXED_POINT_BITS, 100, 100);
    testCase("NegativeCoeff_Scale-1_MidBase", scaleNeg1, 150 << FIXED_POINT_BITS, 100, 200);

    int32_t scaleNeg2 = -FIXED_POINT_SCALE * 2;
    testCase("NegativeCoeff_Scale-2", scaleNeg2, 198 << FIXED_POINT_BITS, 100, 100);

    int32_t scaleNegHalf = -FIXED_POINT_SCALE / 2;
    testCase("NegativeCoeff_Scale-Half", scaleNegHalf, 99 << FIXED_POINT_BITS, 100, 200);
}

void testFractionalBase() {
    // 小数部を持つbase

    int32_t scale1 = FIXED_POINT_SCALE;

    // base = 0.5 (半ピクセルオフセット)
    int32_t baseHalf = FIXED_POINT_SCALE / 2;
    testCase("FractionalBase_0.5", scale1, baseHalf, 100, 100);

    // base = -0.25
    int32_t baseNegQuarter = -FIXED_POINT_SCALE / 4;
    testCase("FractionalBase_-0.25", scale1, baseNegQuarter, 100, 100);

    // base = 0.99999...
    int32_t baseAlmostOne = FIXED_POINT_SCALE - 1;
    testCase("FractionalBase_AlmostOne", scale1, baseAlmostOne, 100, 100);
}

void testEdgeCases() {
    // エッジケース

    // 非常に小さいcanvasSize
    testCase("EdgeCase_TinyCanvas", FIXED_POINT_SCALE, 0, 100, 1);

    // 非常に小さいsrcSize
    testCase("EdgeCase_TinySrc", FIXED_POINT_SCALE, 0, 1, 100);

    // 有効ピクセルなし
    testCase("EdgeCase_NoValidPixels", FIXED_POINT_SCALE, 200 << FIXED_POINT_BITS, 100, 50);

    // 全ピクセル有効
    testCase("EdgeCase_AllValid", FIXED_POINT_SCALE / 2, 0, 100, 50);
}

void testRotationScenarios() {
    // 回転時の典型的なシナリオ

    // 45度回転相当（cos=sin≈0.707）
    int32_t cos45 = static_cast<int32_t>(0.707 * FIXED_POINT_SCALE);
    testCase("Rotation45_Cos", cos45, 50 << FIXED_POINT_BITS, 100, 150);
    testCase("Rotation45_NegSin", -cos45, 100 << FIXED_POINT_BITS, 100, 150);

    // 30度回転相当（cos≈0.866, sin≈0.5）
    int32_t cos30 = static_cast<int32_t>(0.866 * FIXED_POINT_SCALE);
    int32_t sin30 = static_cast<int32_t>(0.5 * FIXED_POINT_SCALE);
    testCase("Rotation30_Cos", cos30, 0, 100, 120);
    testCase("Rotation30_Sin", sin30, -(50 << FIXED_POINT_BITS), 100, 120);
}

// ==============================================================================
// ランダムテスト
// ==============================================================================

void testRandomCases() {
    // 様々な組み合わせで網羅テスト

    int32_t coeffs[] = {
        FIXED_POINT_SCALE,           // 1.0
        FIXED_POINT_SCALE * 2,       // 2.0
        FIXED_POINT_SCALE / 2,       // 0.5
        -FIXED_POINT_SCALE,          // -1.0
        -FIXED_POINT_SCALE * 2,      // -2.0
        -FIXED_POINT_SCALE / 2,      // -0.5
        FIXED_POINT_SCALE * 3 / 4,   // 0.75
        -FIXED_POINT_SCALE * 3 / 4,  // -0.75
    };

    int32_t bases[] = {
        0,
        50 << FIXED_POINT_BITS,
        -(50 << FIXED_POINT_BITS),
        25 << FIXED_POINT_BITS,
        -(25 << FIXED_POINT_BITS),
        (FIXED_POINT_SCALE / 2),      // 0.5
        -(FIXED_POINT_SCALE / 2),     // -0.5
    };

    int srcSizes[] = {64, 100, 128, 256};
    int canvasSizes[] = {64, 100, 150, 200};

    int caseNum = 0;
    for (int32_t coeff : coeffs) {
        for (int32_t base : bases) {
            for (int srcSize : srcSizes) {
                for (int canvasSize : canvasSizes) {
                    char name[64];
                    snprintf(name, sizeof(name), "Random_%03d", caseNum++);
                    testCase(name, coeff, base, srcSize, canvasSize);
                }
            }
        }
    }
}

// ==============================================================================
// メイン
// ==============================================================================

int main() {
    std::cout << "=== calcValidRange Unit Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "--- Coefficient Zero Tests ---" << std::endl;
    testCoeffZero();
    std::cout << std::endl;

    std::cout << "--- Positive Coefficient Tests ---" << std::endl;
    testPositiveCoeff();
    std::cout << std::endl;

    std::cout << "--- Negative Coefficient Tests ---" << std::endl;
    testNegativeCoeff();
    std::cout << std::endl;

    std::cout << "--- Fractional Base Tests ---" << std::endl;
    testFractionalBase();
    std::cout << std::endl;

    std::cout << "--- Edge Case Tests ---" << std::endl;
    testEdgeCases();
    std::cout << std::endl;

    std::cout << "--- Rotation Scenario Tests ---" << std::endl;
    testRotationScenarios();
    std::cout << std::endl;

    std::cout << "--- Random Combination Tests ---" << std::endl;
    testRandomCases();
    std::cout << std::endl;

    std::cout << "=== Summary ===" << std::endl;
    std::cout << "Passed: " << testsPassed << std::endl;
    std::cout << "Failed: " << testsFailed << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
