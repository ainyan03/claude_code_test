// affine_margin_test.cpp
// computeInputRegion のマージン検証テスト
//
// 目的: AABB が実際の DDA アクセス範囲を正確にカバーしていることを確認
//

#define FLEXIMG_NAMESPACE fleximg
#include "../src/fleximg/types.h"
#include "../src/fleximg/common.h"
#include "../src/fleximg/render_types.h"
#include "../src/fleximg/operations/transform.h"
#include "../src/fleximg/nodes/affine_node.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

using namespace fleximg;

// テスト結果
struct TestResult {
    int passed = 0;
    int failed = 0;
    int totalMarginSaved = 0;  // 削減できたマージンの合計
};

// DDA シミュレーションで実際にアクセスされる範囲を計算
struct ActualAccessRange {
    int minX = INT32_MAX;
    int maxX = INT32_MIN;
    int minY = INT32_MAX;
    int maxY = INT32_MIN;
    bool hasAccess = false;

    void update(int x, int y) {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
        hasAccess = true;
    }
};

// DDA シミュレーション（applyAffine と同じロジック）
ActualAccessRange simulateDDA(
    const RenderRequest& request,
    const Matrix2x2_fixed16& invMatrix,
    int_fixed8 txFixed8,
    int_fixed8 tyFixed8,
    int srcWidth,
    int srcHeight,
    int_fixed8 srcOriginX,  // 入力バッファの origin
    int_fixed8 srcOriginY
) {
    ActualAccessRange range;

    const int outW = request.width;
    const int outH = request.height;

    const int32_t fixedInvA = invMatrix.a;
    const int32_t fixedInvB = invMatrix.b;
    const int32_t fixedInvC = invMatrix.c;
    const int32_t fixedInvD = invMatrix.d;

    // 原点座標を整数化
    const int32_t dstOriginXInt = from_fixed8(request.origin.x);
    const int32_t dstOriginYInt = from_fixed8(request.origin.y);
    const int32_t srcOriginXInt = from_fixed8(srcOriginX);
    const int32_t srcOriginYInt = from_fixed8(srcOriginY);

    // 逆変換オフセット
    int64_t invTx64 = -(static_cast<int64_t>(txFixed8) * fixedInvA
                      + static_cast<int64_t>(tyFixed8) * fixedInvB);
    int64_t invTy64 = -(static_cast<int64_t>(txFixed8) * fixedInvC
                      + static_cast<int64_t>(tyFixed8) * fixedInvD);
    int32_t invTxFixed = static_cast<int32_t>(invTx64 >> INT_FIXED8_SHIFT);
    int32_t invTyFixed = static_cast<int32_t>(invTy64 >> INT_FIXED8_SHIFT);

    const int32_t fixedInvTx = invTxFixed
                        - (dstOriginXInt * fixedInvA)
                        - (dstOriginYInt * fixedInvB)
                        + (srcOriginXInt << INT_FIXED16_SHIFT);
    const int32_t fixedInvTy = invTyFixed
                        - (dstOriginXInt * fixedInvC)
                        - (dstOriginYInt * fixedInvD)
                        + (srcOriginYInt << INT_FIXED16_SHIFT);

    // ピクセル中心補正
    const int32_t rowOffsetX = fixedInvB >> 1;
    const int32_t rowOffsetY = fixedInvD >> 1;
    const int32_t dxOffsetX = fixedInvA >> 1;
    const int32_t dxOffsetY = fixedInvC >> 1;

    for (int dy = 0; dy < outH; dy++) {
        int32_t rowBaseX = fixedInvB * dy + fixedInvTx + rowOffsetX;
        int32_t rowBaseY = fixedInvD * dy + fixedInvTy + rowOffsetY;

        auto [xStart, xEnd] = transform::calcValidRange(fixedInvA, rowBaseX, srcWidth, outW);
        auto [yStart, yEnd] = transform::calcValidRange(fixedInvC, rowBaseY, srcHeight, outW);
        int dxStart = std::max({0, xStart, yStart});
        int dxEnd = std::min({outW - 1, xEnd, yEnd});

        if (dxStart > dxEnd) continue;

        for (int dx = dxStart; dx <= dxEnd; dx++) {
            int32_t srcX_fixed = fixedInvA * dx + rowBaseX + dxOffsetX;
            int32_t srcY_fixed = fixedInvC * dx + rowBaseY + dxOffsetY;

            int srcX = static_cast<uint32_t>(srcX_fixed) >> INT_FIXED16_SHIFT;
            int srcY = static_cast<uint32_t>(srcY_fixed) >> INT_FIXED16_SHIFT;

            range.update(srcX, srcY);
        }
    }

    return range;
}

// 単一テストケース
bool runSingleTest(
    const char* name,
    float angleDeg,
    float scale,
    float tx,
    float ty,
    int outWidth,
    int outHeight,
    int srcWidth,
    int srcHeight,
    TestResult& result,
    bool verbose = false
) {
    // AffineNode を設定
    AffineNode node;
    float rad = angleDeg * static_cast<float>(M_PI) / 180.0f;
    float c = std::cos(rad) * scale;
    float s = std::sin(rad) * scale;
    AffineMatrix matrix;
    matrix.a = c;   matrix.b = -s;
    matrix.c = s;   matrix.d = c;
    matrix.tx = tx;
    matrix.ty = ty;
    node.setMatrix(matrix);

    // prepare() で逆行列を計算
    RenderRequest screenInfo;
    screenInfo.width = outWidth;
    screenInfo.height = outHeight;
    screenInfo.origin = Point(to_fixed8(outWidth / 2), to_fixed8(outHeight / 2));
    node.prepare(screenInfo);

    // computeInputRegion を呼び出し
    RenderRequest request;
    request.width = outWidth;
    request.height = outHeight;
    request.origin = Point(to_fixed8(outWidth / 2), to_fixed8(outHeight / 2));

    auto region = node.testComputeInputRegion(request);

    // computeInputRequest と同じ方法で srcOrigin と srcSize を計算
    int inputWidth = region.aabbRight - region.aabbLeft + 1;
    int inputHeight = region.aabbBottom - region.aabbTop + 1;
    int_fixed8 srcOriginX = to_fixed8(-region.aabbLeft);
    int_fixed8 srcOriginY = to_fixed8(-region.aabbTop);

    // DDA シミュレーション（実際の applyAffine と同じ条件で）
    ActualAccessRange actual = simulateDDA(
        request,
        node.getInvMatrix(),
        node.getTxFixed8(),
        node.getTyFixed8(),
        inputWidth,
        inputHeight,
        srcOriginX,
        srcOriginY
    );

    // 検証
    bool passed = true;
    int marginSaved = 0;

    // DDA のアクセス範囲は「バッファ内座標」で返される
    // AABB は「基準点相対座標」なので、変換が必要
    // バッファ内座標 = 基準点相対座標 + srcOrigin（整数部）
    // srcOrigin = -aabbLeft なので、基準点相対座標 = バッファ内座標 - srcOrigin = バッファ内座標 + aabbLeft
    int actualMinX_relative = actual.minX + region.aabbLeft;
    int actualMaxX_relative = actual.maxX + region.aabbLeft;
    int actualMinY_relative = actual.minY + region.aabbTop;
    int actualMaxY_relative = actual.maxY + region.aabbTop;

    if (actual.hasAccess) {
        // AABB が実際のアクセス範囲をカバーしているか
        if (region.aabbLeft > actualMinX_relative ||
            region.aabbRight < actualMaxX_relative ||
            region.aabbTop > actualMinY_relative ||
            region.aabbBottom < actualMaxY_relative) {
            passed = false;
            if (verbose) {
                printf("FAIL: %s\n", name);
                printf("  AABB: [%d, %d] x [%d, %d]\n",
                       region.aabbLeft, region.aabbRight,
                       region.aabbTop, region.aabbBottom);
                printf("  Actual (relative): [%d, %d] x [%d, %d]\n",
                       actualMinX_relative, actualMaxX_relative,
                       actualMinY_relative, actualMaxY_relative);
            }
        } else {
            // 余分なマージンを計算
            marginSaved = (actualMinX_relative - region.aabbLeft) +
                          (region.aabbRight - actualMaxX_relative) +
                          (actualMinY_relative - region.aabbTop) +
                          (region.aabbBottom - actualMaxY_relative);
            if (verbose && marginSaved > 4) {
                printf("PASS: %s (excess margin: %d)\n", name, marginSaved);
            }
        }
    }

    if (passed) {
        result.passed++;
        result.totalMarginSaved += marginSaved;
    } else {
        result.failed++;
    }

    return passed;
}

// 網羅的テスト
void runComprehensiveTests(TestResult& result) {
    printf("=== Comprehensive Margin Test ===\n\n");

    // 回転角度: 0-360度を5度刻み
    std::vector<float> angles;
    for (float a = 0; a < 360; a += 5) angles.push_back(a);

    // スケール: 0.5, 1.0, 1.5, 2.0
    std::vector<float> scales = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};

    // 平行移動: -10 ~ 10
    std::vector<float> translations = {-10.0f, -5.5f, 0.0f, 5.5f, 10.0f};

    // 出力サイズ
    std::vector<std::pair<int, int>> outputSizes = {
        {32, 32}, {63, 63}, {64, 64}, {100, 50}
    };

    // ソースサイズ（十分大きく）
    int srcWidth = 256;
    int srcHeight = 256;

    int testCount = 0;
    char name[256];

    for (auto [outW, outH] : outputSizes) {
        for (float angle : angles) {
            for (float scale : scales) {
                for (float tx : translations) {
                    for (float ty : translations) {
                        snprintf(name, sizeof(name),
                                 "out=%dx%d angle=%.0f scale=%.2f tx=%.1f ty=%.1f",
                                 outW, outH, angle, scale, tx, ty);
                        runSingleTest(name, angle, scale, tx, ty,
                                      outW, outH, srcWidth, srcHeight,
                                      result, false);
                        testCount++;
                    }
                }
            }
        }
    }

    printf("Total tests: %d\n", testCount);
    printf("Passed: %d, Failed: %d\n", result.passed, result.failed);
    printf("Average excess margin: %.2f pixels\n",
           result.passed > 0 ? (float)result.totalMarginSaved / result.passed : 0.0f);
}

// 境界ケーステスト（失敗しやすいケース）
void runEdgeCaseTests(TestResult& result) {
    printf("\n=== Edge Case Tests ===\n");

    // 45度回転（最も AABB が大きくなる）
    runSingleTest("45deg rotation", 45.0f, 1.0f, 0, 0, 64, 64, 256, 256, result, true);

    // 小数オフセットでの回転
    runSingleTest("30deg + offset 0.5", 30.0f, 1.0f, 0.5f, 0.5f, 32, 32, 256, 256, result, true);
    runSingleTest("30deg + offset 0.25", 30.0f, 1.0f, 0.25f, 0.75f, 32, 32, 256, 256, result, true);

    // 奇数サイズ
    runSingleTest("odd size 31x31", 45.0f, 1.0f, 0, 0, 31, 31, 256, 256, result, true);
    runSingleTest("odd size 63x63", 22.5f, 1.0f, 0, 0, 63, 63, 256, 256, result, true);

    // スケール変換
    runSingleTest("scale 0.5 + rotate", 30.0f, 0.5f, 0, 0, 64, 64, 256, 256, result, true);
    runSingleTest("scale 2.0 + rotate", 60.0f, 2.0f, 0, 0, 64, 64, 256, 256, result, true);

    // 回転なし（最小マージンが期待されるケース）
    runSingleTest("no rotation 64x64", 0.0f, 1.0f, 0, 0, 64, 64, 256, 256, result, true);
    runSingleTest("no rotation 32x32 tx=0.5", 0.0f, 1.0f, 0.5f, 0.0f, 32, 32, 256, 256, result, true);

    // 90度回転（整数のみのケース）
    runSingleTest("90deg rotation", 90.0f, 1.0f, 0, 0, 64, 64, 256, 256, result, true);

    // 180度回転
    runSingleTest("180deg rotation", 180.0f, 1.0f, 0, 0, 64, 64, 256, 256, result, true);

    // 149.8度回転 + 3倍スケール
    {
        printf("\n--- Special Test: 149.8deg scale3x ---\n");
        bool ok = runSingleTest("149.8deg scale3x", 149.8f, 3.0f, 0, 0, 64, 64, 256, 256, result, true);
        printf("Result: %s\n", ok ? "PASS" : "FAIL");
    }
}

int main() {
    TestResult result;

    runEdgeCaseTests(result);
    runComprehensiveTests(result);

    printf("\n=== Final Result ===\n");
    printf("Passed: %d, Failed: %d\n", result.passed, result.failed);

    if (result.failed > 0) {
        printf("\n*** SOME TESTS FAILED ***\n");
        return 1;
    }

    printf("\n*** ALL TESTS PASSED ***\n");
    return 0;
}
