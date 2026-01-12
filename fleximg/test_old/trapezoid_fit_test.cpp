/**
 * @file trapezoid_fit_test.cpp
 * @brief AABB分割の台形フィットアルゴリズム検証プログラム
 *
 * 台形フィットの効果を定量的に測定し、以下を検証:
 * 1. computeXRangeForYStrip() / computeYRangeForXStrip() の正確性
 * 2. 分割前後での requestedPixels の削減量
 * 3. 各回転角度での効果
 */

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <climits>

// 固定小数点定義（types.h から抜粋）
using int_fixed8 = int32_t;
static constexpr int INT_FIXED8_SHIFT = 8;
static constexpr int INT_FIXED8_ONE = 1 << INT_FIXED8_SHIFT;

inline int_fixed8 to_fixed8(int v) { return v << INT_FIXED8_SHIFT; }
inline int from_fixed8(int_fixed8 v) { return v >> INT_FIXED8_SHIFT; }
inline int from_fixed8_floor(int_fixed8 v) {
    return (v >= 0) ? (v >> INT_FIXED8_SHIFT) : -(((-v - 1) >> INT_FIXED8_SHIFT) + 1);
}
inline int from_fixed8_ceil(int_fixed8 v) {
    return (v >= 0)
        ? ((v + INT_FIXED8_ONE - 1) >> INT_FIXED8_SHIFT)
        : -((-v) >> INT_FIXED8_SHIFT);
}
inline int_fixed8 float_to_fixed8(float f) {
    return static_cast<int_fixed8>(std::lround(f * INT_FIXED8_ONE));
}

// InputRegion 構造体（affine_node.h から抜粋）
struct InputRegion {
    int_fixed8 corners_x[4];
    int_fixed8 corners_y[4];
    int aabbLeft, aabbTop, aabbRight, aabbBottom;
    int64_t aabbPixels;
    int64_t parallelogramPixels;
    int64_t outputPixels;
};

// SplitStrategy 構造体
struct SplitStrategy {
    bool splitInX;
    int splitCount;
};

static constexpr int MIN_SPLIT_SIZE = 32;
static constexpr int MAX_SPLIT_COUNT = 8;

// ========================================================================
// 検証対象の関数（affine_node.h から抜粋）
// ========================================================================

// 統一関数: primary 軸の範囲に対応する secondary 軸の範囲を計算
std::pair<int, int> computeSecondaryRangeForPrimaryStrip(
    int primaryMin, int primaryMax,
    const int_fixed8* primaryCoords,
    const int_fixed8* secondaryCoords
) {
    int p[4], s[4];
    for (int i = 0; i < 4; ++i) {
        p[i] = from_fixed8(primaryCoords[i]);
        s[i] = from_fixed8(secondaryCoords[i]);
    }

    static constexpr int edges[4][2] = {{0,1}, {0,2}, {1,3}, {2,3}};

    int sMin = INT32_MAX;
    int sMax = INT32_MIN;

    for (int e = 0; e < 4; ++e) {
        int i0 = edges[e][0];
        int i1 = edges[e][1];
        int p0 = p[i0], p1 = p[i1];
        int s0 = s[i0], s1 = s[i1];

        int edgePMin = std::min(p0, p1);
        int edgePMax = std::max(p0, p1);
        if (edgePMax < primaryMin || edgePMin > primaryMax) continue;

        for (int pv : {primaryMin, primaryMax}) {
            if (pv < edgePMin || pv > edgePMax) continue;
            if (p0 == p1) {
                sMin = std::min({sMin, s0, s1});
                sMax = std::max({sMax, s0, s1});
            } else {
                int sv = s0 + (s1 - s0) * (pv - p0) / (p1 - p0);
                sMin = std::min(sMin, sv);
                sMax = std::max(sMax, sv);
            }
        }
    }

    for (int i = 0; i < 4; ++i) {
        if (p[i] >= primaryMin && p[i] <= primaryMax) {
            sMin = std::min(sMin, s[i]);
            sMax = std::max(sMax, s[i]);
        }
    }

    return {sMin - 1, sMax + 1};
}

// ラッパー関数
std::pair<int, int> computeXRangeForYStrip(int yMin, int yMax,
                                            const InputRegion& region) {
    return computeSecondaryRangeForPrimaryStrip(
        yMin, yMax, region.corners_y, region.corners_x);
}

std::pair<int, int> computeYRangeForXStrip(int xMin, int xMax,
                                            const InputRegion& region) {
    return computeSecondaryRangeForPrimaryStrip(
        xMin, xMax, region.corners_x, region.corners_y);
}

SplitStrategy computeSplitStrategy(const InputRegion& region) {
    int width = region.aabbRight - region.aabbLeft + 1;
    int height = region.aabbBottom - region.aabbTop + 1;

    bool splitInX = (width > height);
    int dim = splitInX ? width : height;

    int count = dim / MIN_SPLIT_SIZE;
    if (count < 1) count = 1;
    if (count > MAX_SPLIT_COUNT) count = MAX_SPLIT_COUNT;

    return {splitInX, count};
}

// ========================================================================
// テスト用ヘルパー関数
// ========================================================================

// 出力矩形を逆変換して InputRegion を生成
InputRegion createInputRegion(int outWidth, int outHeight,
                               float rotation, float scaleX, float scaleY) {
    InputRegion region;
    region.outputPixels = static_cast<int64_t>(outWidth) * outHeight;

    // 変換行列
    float cosR = std::cos(rotation);
    float sinR = std::sin(rotation);
    float a = cosR * scaleX;
    float b = -sinR * scaleY;
    float c = sinR * scaleX;
    float d = cosR * scaleY;

    // 逆行列
    float det = a * d - b * c;
    float invA = d / det;
    float invB = -b / det;
    float invC = -c / det;
    float invD = a / det;

    // 出力矩形の4頂点（原点中心）
    float out_x[4] = {0, (float)outWidth, 0, (float)outWidth};
    float out_y[4] = {0, 0, (float)outHeight, (float)outHeight};

    // 逆変換して入力座標を計算
    int_fixed8 minX_f8 = INT32_MAX, minY_f8 = INT32_MAX;
    int_fixed8 maxX_f8 = INT32_MIN, maxY_f8 = INT32_MIN;

    for (int i = 0; i < 4; i++) {
        float sx = invA * out_x[i] + invB * out_y[i];
        float sy = invC * out_x[i] + invD * out_y[i];
        region.corners_x[i] = float_to_fixed8(sx);
        region.corners_y[i] = float_to_fixed8(sy);
        minX_f8 = std::min(minX_f8, region.corners_x[i]);
        minY_f8 = std::min(minY_f8, region.corners_y[i]);
        maxX_f8 = std::max(maxX_f8, region.corners_x[i]);
        maxY_f8 = std::max(maxY_f8, region.corners_y[i]);
    }

    // AABB計算
    int minX = from_fixed8_floor(minX_f8);
    int minY = from_fixed8_floor(minY_f8);
    int maxX = from_fixed8_ceil(maxX_f8);
    int maxY = from_fixed8_ceil(maxY_f8);

    region.aabbLeft = minX - 1;
    region.aabbTop = minY - 1;
    region.aabbRight = maxX + 1;
    region.aabbBottom = maxY + 1;
    region.aabbPixels = static_cast<int64_t>(region.aabbRight - region.aabbLeft + 1)
                      * (region.aabbBottom - region.aabbTop + 1);

    // 平行四辺形面積
    int64_t dx1 = region.corners_x[1] - region.corners_x[0];
    int64_t dy1 = region.corners_y[1] - region.corners_y[0];
    int64_t dx2 = region.corners_x[2] - region.corners_x[0];
    int64_t dy2 = region.corners_y[2] - region.corners_y[0];
    int64_t cross = dx1 * dy2 - dy1 * dx2;
    region.parallelogramPixels = std::abs(cross) >> INT_FIXED8_SHIFT >> INT_FIXED8_SHIFT;

    return region;
}

// 分割なしの総要求ピクセル数
int64_t calcRequestedPixelsNoSplit(const InputRegion& region) {
    return region.aabbPixels;
}

// 分割あり（台形フィットなし）の総要求ピクセル数
int64_t calcRequestedPixelsSplitNoFit(const InputRegion& region) {
    SplitStrategy strategy = computeSplitStrategy(region);

    int aabbWidth = region.aabbRight - region.aabbLeft + 1;
    int aabbHeight = region.aabbBottom - region.aabbTop + 1;

    int splitDim = strategy.splitInX ? aabbWidth : aabbHeight;
    int splitSize = (splitDim + strategy.splitCount - 1) / strategy.splitCount;

    int64_t total = 0;

    for (int i = 0; i < strategy.splitCount; ++i) {
        int stripPixels;
        if (strategy.splitInX) {
            int splitLeft = region.aabbLeft + i * splitSize;
            int splitRight = std::min(splitLeft + splitSize - 1, region.aabbRight);
            if (splitLeft > splitRight) break;
            stripPixels = (splitRight - splitLeft + 1) * aabbHeight;
        } else {
            int splitTop = region.aabbTop + i * splitSize;
            int splitBottom = std::min(splitTop + splitSize - 1, region.aabbBottom);
            if (splitTop > splitBottom) break;
            stripPixels = aabbWidth * (splitBottom - splitTop + 1);
        }
        total += stripPixels;
    }

    return total;
}

// 分割あり（台形フィットあり）の総要求ピクセル数
int64_t calcRequestedPixelsSplitWithFit(const InputRegion& region) {
    SplitStrategy strategy = computeSplitStrategy(region);

    int aabbWidth = region.aabbRight - region.aabbLeft + 1;
    int aabbHeight = region.aabbBottom - region.aabbTop + 1;

    int splitDim = strategy.splitInX ? aabbWidth : aabbHeight;
    int splitSize = (splitDim + strategy.splitCount - 1) / strategy.splitCount;

    int64_t total = 0;

    for (int i = 0; i < strategy.splitCount; ++i) {
        int stripPixels;
        if (strategy.splitInX) {
            int splitLeft = region.aabbLeft + i * splitSize;
            int splitRight = std::min(splitLeft + splitSize - 1, region.aabbRight);
            if (splitLeft > splitRight) break;

            // 台形フィット
            auto [yFitMin, yFitMax] = computeYRangeForXStrip(splitLeft, splitRight, region);
            yFitMin = std::max(yFitMin, region.aabbTop);
            yFitMax = std::min(yFitMax, region.aabbBottom);
            if (yFitMin > yFitMax) continue;

            stripPixels = (splitRight - splitLeft + 1) * (yFitMax - yFitMin + 1);
        } else {
            int splitTop = region.aabbTop + i * splitSize;
            int splitBottom = std::min(splitTop + splitSize - 1, region.aabbBottom);
            if (splitTop > splitBottom) break;

            // 台形フィット
            auto [xFitMin, xFitMax] = computeXRangeForYStrip(splitTop, splitBottom, region);
            xFitMin = std::max(xFitMin, region.aabbLeft);
            xFitMax = std::min(xFitMax, region.aabbRight);
            if (xFitMin > xFitMax) continue;

            stripPixels = (xFitMax - xFitMin + 1) * (splitBottom - splitTop + 1);
        }
        total += stripPixels;
    }

    return total;
}

// ========================================================================
// テストケース
// ========================================================================

void printHeader() {
    printf("=== AABB分割 台形フィット検証 ===\n\n");
    printf("| 回転角度 | 出力サイズ | AABB面積 | 分割なし | 分割のみ | 台形フィット | 削減率 |\n");
    printf("|----------|------------|----------|----------|----------|--------------|--------|\n");
}

void testCase(const char* label, int outW, int outH, float rotation, float scaleX = 1.0f, float scaleY = 1.0f) {
    InputRegion region = createInputRegion(outW, outH, rotation, scaleX, scaleY);

    int64_t noSplit = calcRequestedPixelsNoSplit(region);
    int64_t splitNoFit = calcRequestedPixelsSplitNoFit(region);
    int64_t splitWithFit = calcRequestedPixelsSplitWithFit(region);

    float reductionVsNoSplit = 100.0f * (1.0f - (float)splitWithFit / noSplit);
    float reductionVsSplitOnly = 100.0f * (1.0f - (float)splitWithFit / splitNoFit);

    printf("| %-8s | %4dx%-5d | %8lld | %8lld | %8lld | %12lld | %5.1f%% |\n",
           label, outW, outH,
           (long long)region.aabbPixels,
           (long long)noSplit,
           (long long)splitNoFit,
           (long long)splitWithFit,
           reductionVsSplitOnly);
}

void testDetailedCase(const char* label, int outW, int outH, float rotation) {
    InputRegion region = createInputRegion(outW, outH, rotation, 1.0f, 1.0f);
    SplitStrategy strategy = computeSplitStrategy(region);

    printf("\n--- %s (%.1f°, %dx%d) ---\n", label, rotation * 180 / M_PI, outW, outH);
    printf("AABB: [%d,%d]-[%d,%d] (%dx%d)\n",
           region.aabbLeft, region.aabbTop, region.aabbRight, region.aabbBottom,
           region.aabbRight - region.aabbLeft + 1,
           region.aabbBottom - region.aabbTop + 1);
    printf("分割戦略: %s方向, %d分割\n",
           strategy.splitInX ? "X" : "Y", strategy.splitCount);

    printf("\n頂点座標:\n");
    for (int i = 0; i < 4; i++) {
        printf("  [%d]: (%d, %d)\n", i,
               from_fixed8(region.corners_x[i]),
               from_fixed8(region.corners_y[i]));
    }

    int aabbWidth = region.aabbRight - region.aabbLeft + 1;
    int aabbHeight = region.aabbBottom - region.aabbTop + 1;
    int splitDim = strategy.splitInX ? aabbWidth : aabbHeight;
    int splitSize = (splitDim + strategy.splitCount - 1) / strategy.splitCount;

    printf("\n各strip の詳細:\n");
    printf("| strip | 分割範囲     | フィット前   | フィット後   | 削減 |\n");
    printf("|-------|--------------|--------------|--------------|------|\n");

    for (int i = 0; i < strategy.splitCount; ++i) {
        if (strategy.splitInX) {
            int splitLeft = region.aabbLeft + i * splitSize;
            int splitRight = std::min(splitLeft + splitSize - 1, region.aabbRight);
            if (splitLeft > splitRight) break;

            int beforePixels = (splitRight - splitLeft + 1) * aabbHeight;

            auto [yFitMin, yFitMax] = computeYRangeForXStrip(splitLeft, splitRight, region);
            int yBefore = aabbHeight;
            yFitMin = std::max(yFitMin, region.aabbTop);
            yFitMax = std::min(yFitMax, region.aabbBottom);
            int yAfter = (yFitMin <= yFitMax) ? (yFitMax - yFitMin + 1) : 0;

            int afterPixels = (splitRight - splitLeft + 1) * yAfter;
            float reduction = 100.0f * (1.0f - (float)afterPixels / beforePixels);

            printf("| %5d | X[%3d-%3d]   | Y[%3d-%3d]=%d | Y[%3d-%3d]=%d | %4.0f%% |\n",
                   i, splitLeft, splitRight,
                   region.aabbTop, region.aabbBottom, yBefore,
                   yFitMin, yFitMax, yAfter,
                   reduction);
        } else {
            int splitTop = region.aabbTop + i * splitSize;
            int splitBottom = std::min(splitTop + splitSize - 1, region.aabbBottom);
            if (splitTop > splitBottom) break;

            int beforePixels = aabbWidth * (splitBottom - splitTop + 1);

            auto [xFitMin, xFitMax] = computeXRangeForYStrip(splitTop, splitBottom, region);
            int xBefore = aabbWidth;
            xFitMin = std::max(xFitMin, region.aabbLeft);
            xFitMax = std::min(xFitMax, region.aabbRight);
            int xAfter = (xFitMin <= xFitMax) ? (xFitMax - xFitMin + 1) : 0;

            int afterPixels = xAfter * (splitBottom - splitTop + 1);
            float reduction = 100.0f * (1.0f - (float)afterPixels / beforePixels);

            printf("| %5d | Y[%3d-%3d]   | X[%3d-%3d]=%d | X[%3d-%3d]=%d | %4.0f%% |\n",
                   i, splitTop, splitBottom,
                   region.aabbLeft, region.aabbRight, xBefore,
                   xFitMin, xFitMax, xAfter,
                   reduction);
        }
    }

    int64_t noSplit = calcRequestedPixelsNoSplit(region);
    int64_t splitNoFit = calcRequestedPixelsSplitNoFit(region);
    int64_t splitWithFit = calcRequestedPixelsSplitWithFit(region);

    printf("\n合計:\n");
    printf("  分割なし:     %lld px\n", (long long)noSplit);
    printf("  分割のみ:     %lld px (分割なしの %.1f%%)\n",
           (long long)splitNoFit, 100.0f * splitNoFit / noSplit);
    printf("  台形フィット: %lld px (分割のみの %.1f%%, 削減 %.1f%%)\n",
           (long long)splitWithFit,
           100.0f * splitWithFit / splitNoFit,
           100.0f * (1.0f - (float)splitWithFit / splitNoFit));
}

// AABB分割の発動条件をチェック
void checkSplitCondition(const char* label, int outW, int outH, float rotation) {
    InputRegion region = createInputRegion(outW, outH, rotation, 1.0f, 1.0f);

    // 改善倍率 = aabbPixels / (parallelogramPixels * 2)
    float improvementFactor = (region.parallelogramPixels > 0)
        ? static_cast<float>(region.aabbPixels) / (region.parallelogramPixels * 2)
        : 1.0f;

    bool wouldSplit = improvementFactor >= 10.0f;  // AABB_SPLIT_THRESHOLD

    printf("| %-12s | %4dx%-4d | %8lld | %8lld | %6.2fx | %-3s |\n",
           label, outW, outH,
           (long long)region.aabbPixels,
           (long long)region.parallelogramPixels,
           improvementFactor,
           wouldSplit ? "Yes" : "No");
}

int main() {
    // AABB分割発動条件のチェック
    printf("=== AABB分割 発動条件チェック (閾値: 10x) ===\n\n");
    printf("| 条件         | 出力サイズ | AABB面積 | 平行四辺形 | 倍率   | 発動 |\n");
    printf("|--------------|------------|----------|------------|--------|------|\n");

    // 正方形出力
    checkSplitCondition("0°", 256, 256, 0);
    checkSplitCondition("45°", 256, 256, 45 * M_PI / 180);
    checkSplitCondition("30°", 256, 256, 30 * M_PI / 180);

    // 細長い出力（タイル分割時のスキャンラインを模擬）
    checkSplitCondition("45° 512x1", 512, 1, 45 * M_PI / 180);
    checkSplitCondition("45° 256x1", 256, 1, 45 * M_PI / 180);
    checkSplitCondition("45° 128x1", 128, 1, 45 * M_PI / 180);
    checkSplitCondition("45° 64x1", 64, 1, 45 * M_PI / 180);
    checkSplitCondition("45° 32x1", 32, 1, 45 * M_PI / 180);

    // 32x32タイル（一般的なタイルサイズ）
    checkSplitCondition("45° 32x32", 32, 32, 45 * M_PI / 180);
    checkSplitCondition("30° 32x32", 32, 32, 30 * M_PI / 180);

    // 64x64タイル
    checkSplitCondition("45° 64x64", 64, 64, 45 * M_PI / 180);
    checkSplitCondition("30° 64x64", 64, 64, 30 * M_PI / 180);

    printf("\n");

    // サマリーテーブル
    printHeader();

    // 各回転角度でテスト（256x256 出力）
    testCase("0°", 256, 256, 0);
    testCase("15°", 256, 256, 15 * M_PI / 180);
    testCase("30°", 256, 256, 30 * M_PI / 180);
    testCase("45°", 256, 256, 45 * M_PI / 180);
    testCase("60°", 256, 256, 60 * M_PI / 180);
    testCase("90°", 256, 256, 90 * M_PI / 180);

    printf("\n");

    // 細長い出力でテスト（45度回転）
    testCase("45° 256x1", 256, 1, 45 * M_PI / 180);
    testCase("45° 256x16", 256, 16, 45 * M_PI / 180);
    testCase("45° 256x64", 256, 64, 45 * M_PI / 180);

    // 詳細出力
    testDetailedCase("45度回転", 256, 64, 45 * M_PI / 180);
    testDetailedCase("30度回転", 256, 256, 30 * M_PI / 180);

    // 128x1 スキャンラインの詳細分析
    printf("\n=== 128x1 スキャンライン詳細分析 ===\n");
    testDetailedCase("128x1 45°", 128, 1, 45 * M_PI / 180);

    // MIN_SPLIT_SIZE の影響を検証
    printf("\n=== MIN_SPLIT_SIZE の影響 ===\n");
    printf("| MIN_SPLIT | 分割数 | 台形フィット | 削減率 |\n");
    printf("|-----------|--------|--------------|--------|\n");

    for (int minSize : {64, 32, 16, 8, 4, 2, 1}) {
        InputRegion region = createInputRegion(128, 1, 45 * M_PI / 180, 1.0f, 1.0f);

        int aabbWidth = region.aabbRight - region.aabbLeft + 1;
        int aabbHeight = region.aabbBottom - region.aabbTop + 1;

        // Y方向分割（高さが大きいので）
        bool splitInX = (aabbWidth > aabbHeight);
        int dim = splitInX ? aabbWidth : aabbHeight;
        int count = dim / minSize;
        if (count < 1) count = 1;
        if (count > 32) count = 32;  // テスト用に上限を緩和

        int splitSize = (dim + count - 1) / count;
        int64_t total = 0;

        for (int i = 0; i < count; ++i) {
            if (splitInX) {
                int splitLeft = region.aabbLeft + i * splitSize;
                int splitRight = std::min(splitLeft + splitSize - 1, region.aabbRight);
                if (splitLeft > splitRight) break;

                auto [yFitMin, yFitMax] = computeYRangeForXStrip(splitLeft, splitRight, region);
                yFitMin = std::max(yFitMin, region.aabbTop);
                yFitMax = std::min(yFitMax, region.aabbBottom);
                if (yFitMin > yFitMax) continue;

                total += (splitRight - splitLeft + 1) * (yFitMax - yFitMin + 1);
            } else {
                int splitTop = region.aabbTop + i * splitSize;
                int splitBottom = std::min(splitTop + splitSize - 1, region.aabbBottom);
                if (splitTop > splitBottom) break;

                auto [xFitMin, xFitMax] = computeXRangeForYStrip(splitTop, splitBottom, region);
                xFitMin = std::max(xFitMin, region.aabbLeft);
                xFitMax = std::min(xFitMax, region.aabbRight);
                if (xFitMin > xFitMax) continue;

                total += (xFitMax - xFitMin + 1) * (splitBottom - splitTop + 1);
            }
        }

        float reduction = 100.0f * (1.0f - (float)total / region.aabbPixels);
        float efficiency = 100.0f * 128.0f / total;  // 128 = 出力ピクセル数
        printf("| %9d | %6d | %12lld | %5.1f%% (eff: %.1f%%) |\n",
               minSize, count, (long long)total, reduction, efficiency);
    }

    // マージンの影響を検証
    printf("\n=== マージン ±1 の影響 ===\n");
    {
        InputRegion region = createInputRegion(128, 1, 45 * M_PI / 180, 1.0f, 1.0f);
        int count = 8;  // 固定
        int aabbHeight = region.aabbBottom - region.aabbTop + 1;
        int splitSize = (aabbHeight + count - 1) / count;

        printf("頂点座標:\n");
        for (int i = 0; i < 4; i++) {
            printf("  [%d]: (%d, %d)\n", i,
                   from_fixed8(region.corners_x[i]),
                   from_fixed8(region.corners_y[i]));
        }

        printf("\n各strip のX範囲計算:\n");
        printf("| strip | Y範囲      | X範囲(計算)  | 幅(±1込) | 幅(理論) |\n");
        printf("|-------|------------|--------------|----------|----------|\n");

        for (int i = 0; i < count; ++i) {
            int splitTop = region.aabbTop + i * splitSize;
            int splitBottom = std::min(splitTop + splitSize - 1, region.aabbBottom);
            if (splitTop > splitBottom) break;

            auto [xFitMin, xFitMax] = computeXRangeForYStrip(splitTop, splitBottom, region);
            xFitMin = std::max(xFitMin, region.aabbLeft);
            xFitMax = std::min(xFitMax, region.aabbRight);

            int width = (xFitMin <= xFitMax) ? (xFitMax - xFitMin + 1) : 0;
            // 理論上の幅 = strip高さ（45度回転なので1:1対応）+ 入力の幅(≈1)
            int stripH = splitBottom - splitTop + 1;
            int theoreticalWidth = stripH + 2;  // 45度なので高さ≒幅、+入力幅

            printf("| %5d | [%3d-%3d] | [%3d-%3d] | %8d | %8d |\n",
                   i, splitTop, splitBottom, xFitMin, xFitMax, width, theoreticalWidth);
        }
    }

    printf("\n=== 検証完了 ===\n");
    return 0;
}
