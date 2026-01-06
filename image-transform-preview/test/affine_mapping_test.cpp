// アフィン変換マッピングテスト
// 出力座標から入力座標への逆変換の正確性を検証
//
// テスト条件:
// - 元画像: 4x6 ピクセル
// - 出力先: 20x20 ピクセル、基準座標 (10, 10)
// - 代表的な3原点(左上・中央・右下) × 4回転 = 12パターン

#include <iostream>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <iomanip>

// テスト設定
constexpr int INPUT_WIDTH = 4;
constexpr int INPUT_HEIGHT = 6;
constexpr int OUTPUT_SIZE = 20;
constexpr int DST_ORIGIN_X = 10;
constexpr int DST_ORIGIN_Y = 10;

// 固定小数点の設定（image_processor.cppと同じ）
constexpr int FIXED_POINT_BITS = 16;
constexpr int32_t FIXED_POINT_SCALE = 1 << FIXED_POINT_BITS;
constexpr int32_t HALF = FIXED_POINT_SCALE / 2;

// 出力範囲の期待値
struct ExpectedRange {
    const char* originName;
    double originX, originY;
    const char* rotationName;
    double degrees;
    int minX, minY, maxX, maxY;  // 期待される出力範囲
};

// ============================================================================
// 期待値の手計算根拠:
//
// 元画像: 4x6, dstOrigin: (10,10)
// 出力ピクセル(dx,dy)の中心(dx+0.5, dy+0.5)から入力座標を逆変換で求める
//
// 0度: sx = dx + 0.5 - 10 + originX, sy = dy + 0.5 - 10 + originY
// 90度CW: sx = dy + 0.5 - 10 + originX, sy = 10 - (dx + 0.5) + originY
// 180度: sx = 10 - (dx + 0.5) + originX, sy = 10 - (dy + 0.5) + originY
// 270度CW: sx = 10 - (dy + 0.5) + originX, sy = dx + 0.5 - 10 + originY
//
// 入力範囲 0 <= sx < 4, 0 <= sy < 6 を満たす出力範囲を求める
// ============================================================================

const ExpectedRange EXPECTED[] = {
    // 左上原点 (0, 0)
    {"左上", 0, 0, "0度",   0,   10, 10, 13, 15},  // dx:[10,13], dy:[10,15]
    {"左上", 0, 0, "90度",  90,   4, 10,  9, 13},  // dx:[4,9],   dy:[10,13]
    {"左上", 0, 0, "180度", 180,  6,  4,  9,  9},  // dx:[6,9],   dy:[4,9]
    {"左上", 0, 0, "270度", 270, 10,  6, 15,  9},  // dx:[10,15], dy:[6,9]

    // 中央原点 (2, 3)
    {"中央", 2, 3, "0度",   0,    8,  7, 11, 12},  // dx:[8,11],  dy:[7,12]
    {"中央", 2, 3, "90度",  90,   7,  8, 12, 11},  // dx:[7,12],  dy:[8,11]
    {"中央", 2, 3, "180度", 180,  8,  7, 11, 12},  // dx:[8,11],  dy:[7,12]
    {"中央", 2, 3, "270度", 270,  7,  8, 12, 11},  // dx:[7,12],  dy:[8,11]

    // 右下原点 (4, 6)
    {"右下", 4, 6, "0度",   0,    6,  4,  9,  9},  // dx:[6,9],   dy:[4,9]
    {"右下", 4, 6, "90度",  90,  10,  6, 15,  9},  // dx:[10,15], dy:[6,9]
    {"右下", 4, 6, "180度", 180, 10, 10, 13, 15},  // dx:[10,13], dy:[10,15]
    {"右下", 4, 6, "270度", 270,  4, 10,  9, 13},  // dx:[4,9],   dy:[10,13]
};

// アフィン行列
struct AffineMatrix {
    double a, b, c, d;
};

// 角度から回転行列を生成
AffineMatrix createRotationMatrix(double degrees) {
    double rad = degrees * M_PI / 180.0;
    double cosA = std::cos(rad);
    double sinA = std::sin(rad);
    return {cosA, -sinA, sinA, cosA};
}

// 出力配列の型（-1: 範囲外、0以上: 入力ピクセルインデックス sy * INPUT_WIDTH + sx）
using OutputMap = std::vector<std::vector<int>>;

// 出力マップを生成（固定小数点演算、image_processor.cppのロジックを再現）
OutputMap generateActualMap(double originX, double originY, const AffineMatrix& matrix) {
    OutputMap output(OUTPUT_SIZE, std::vector<int>(OUTPUT_SIZE, -1));

    // 逆行列を計算
    double det = matrix.a * matrix.d - matrix.b * matrix.c;
    if (std::abs(det) < 1e-10) return output;

    double invDet = 1.0 / det;
    double invA = matrix.d * invDet;
    double invB = -matrix.b * invDet;
    double invC = -matrix.c * invDet;
    double invD = matrix.a * invDet;

    // 固定小数点に変換
    int32_t fixedInvA = std::lround(invA * FIXED_POINT_SCALE);
    int32_t fixedInvB = std::lround(invB * FIXED_POINT_SCALE);
    int32_t fixedInvC = std::lround(invC * FIXED_POINT_SCALE);
    int32_t fixedInvD = std::lround(invD * FIXED_POINT_SCALE);
    int32_t fixedOriginX = std::lround(originX * FIXED_POINT_SCALE);
    int32_t fixedOriginY = std::lround(originY * FIXED_POINT_SCALE);

    // 出力ピクセル中心オフセット（0.5を固定小数点で表現）
    const int32_t pixelCenterOffset = HALF;  // 0.5 * FIXED_POINT_SCALE

    for (int dy = 0; dy < OUTPUT_SIZE; dy++) {
        for (int dx = 0; dx < OUTPUT_SIZE; dx++) {
            // dstOriginからの相対位置（出力ピクセルの中心座標を使用）
            // (dx + 0.5) - DST_ORIGIN_X, (dy + 0.5) - DST_ORIGIN_Y
            int32_t relX_fixed = ((dx - DST_ORIGIN_X) << FIXED_POINT_BITS) + pixelCenterOffset;
            int32_t relY_fixed = ((dy - DST_ORIGIN_Y) << FIXED_POINT_BITS) + pixelCenterOffset;

            // 逆変換（固定小数点）
            int64_t srcX_fixed = ((int64_t)fixedInvA * relX_fixed + (int64_t)fixedInvB * relY_fixed) >> FIXED_POINT_BITS;
            int64_t srcY_fixed = ((int64_t)fixedInvC * relX_fixed + (int64_t)fixedInvD * relY_fixed) >> FIXED_POINT_BITS;

            // 原点オフセットを加算
            srcX_fixed += fixedOriginX;
            srcY_fixed += fixedOriginY;

            // floor で整数座標に変換（負の数も正しく処理）
            int sx = (int)(srcX_fixed >> FIXED_POINT_BITS);
            int sy = (int)(srcY_fixed >> FIXED_POINT_BITS);
            if (srcX_fixed < 0 && (srcX_fixed & ((1 << FIXED_POINT_BITS) - 1)) != 0) sx--;
            if (srcY_fixed < 0 && (srcY_fixed & ((1 << FIXED_POINT_BITS) - 1)) != 0) sy--;

            if (sx >= 0 && sx < INPUT_WIDTH && sy >= 0 && sy < INPUT_HEIGHT) {
                output[dy][dx] = sy * INPUT_WIDTH + sx;
            }
        }
    }
    return output;
}

// 出力マップの範囲を取得
bool getMapBounds(const OutputMap& map, int& minX, int& minY, int& maxX, int& maxY) {
    minX = minY = OUTPUT_SIZE;
    maxX = maxY = -1;
    for (int y = 0; y < OUTPUT_SIZE; y++) {
        for (int x = 0; x < OUTPUT_SIZE; x++) {
            if (map[y][x] >= 0) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }
    return maxX >= 0;
}

// 出力マップを表示
void printMap(const OutputMap& map) {
    int minX, minY, maxX, maxY;
    if (!getMapBounds(map, minX, minY, maxX, maxY)) {
        std::cout << "    (empty)" << std::endl;
        return;
    }

    int dispMinX = std::max(0, minX - 1);
    int dispMaxX = std::min(OUTPUT_SIZE - 1, maxX + 1);
    int dispMinY = std::max(0, minY - 1);
    int dispMaxY = std::min(OUTPUT_SIZE - 1, maxY + 1);

    std::cout << "      ";
    for (int x = dispMinX; x <= dispMaxX; x++) {
        std::cout << std::setw(3) << x;
    }
    std::cout << std::endl;

    for (int y = dispMinY; y <= dispMaxY; y++) {
        std::cout << "    " << std::setw(2) << y << " ";
        for (int x = dispMinX; x <= dispMaxX; x++) {
            if (map[y][x] >= 0) {
                std::cout << std::setw(3) << map[y][x];
            } else {
                std::cout << "  .";
            }
        }
        std::cout << std::endl;
    }
}

int main() {
    std::cout << "=== Affine Mapping Test ===" << std::endl;
    std::cout << "Input: " << INPUT_WIDTH << "x" << INPUT_HEIGHT << " pixels" << std::endl;
    std::cout << "Output: " << OUTPUT_SIZE << "x" << OUTPUT_SIZE << " pixels" << std::endl;
    std::cout << "Dst Origin: (" << DST_ORIGIN_X << ", " << DST_ORIGIN_Y << ")" << std::endl;
    std::cout << std::endl;

    int passCount = 0;
    int failCount = 0;

    for (const auto& expected : EXPECTED) {
        std::cout << "--- " << expected.originName << "原点("
                  << expected.originX << "," << expected.originY << "), "
                  << expected.rotationName << " ---" << std::endl;
        std::cout << "  Expected range: (" << expected.minX << "," << expected.minY
                  << ")-(" << expected.maxX << "," << expected.maxY << ")" << std::endl;

        AffineMatrix matrix = createRotationMatrix(expected.degrees);
        OutputMap actual = generateActualMap(expected.originX, expected.originY, matrix);

        int actualMinX, actualMinY, actualMaxX, actualMaxY;
        getMapBounds(actual, actualMinX, actualMinY, actualMaxX, actualMaxY);

        std::cout << "  Actual range:   (" << actualMinX << "," << actualMinY
                  << ")-(" << actualMaxX << "," << actualMaxY << ")" << std::endl;

        bool rangeMatch = (actualMinX == expected.minX && actualMinY == expected.minY &&
                          actualMaxX == expected.maxX && actualMaxY == expected.maxY);

        if (rangeMatch) {
            std::cout << "  [PASS]" << std::endl;
            passCount++;
        } else {
            std::cout << "  [FAIL] Range mismatch!" << std::endl;
            std::cout << "  Actual map:" << std::endl;
            printMap(actual);
            failCount++;
        }

        // ±1度での安定性テスト
        AffineMatrix matrixPlus = createRotationMatrix(expected.degrees + 1.0);
        AffineMatrix matrixMinus = createRotationMatrix(expected.degrees - 1.0);
        OutputMap actualPlus = generateActualMap(expected.originX, expected.originY, matrixPlus);
        OutputMap actualMinus = generateActualMap(expected.originX, expected.originY, matrixMinus);

        int plusMinX, plusMinY, plusMaxX, plusMaxY;
        int minusMinX, minusMinY, minusMaxX, minusMaxY;
        getMapBounds(actualPlus, plusMinX, plusMinY, plusMaxX, plusMaxY);
        getMapBounds(actualMinus, minusMinX, minusMinY, minusMaxX, minusMaxY);

        // ±1度では範囲が最大1ピクセル程度の変動は許容
        // ここでは基準角度と同じ範囲になることを確認（厳密テスト）
        bool stablePlus = (plusMinX == expected.minX && plusMinY == expected.minY &&
                          plusMaxX == expected.maxX && plusMaxY == expected.maxY);
        bool stableMinus = (minusMinX == expected.minX && minusMinY == expected.minY &&
                           minusMaxX == expected.maxX && minusMaxY == expected.maxY);

        if (stablePlus && stableMinus) {
            std::cout << "  [STABLE] ±1 degree" << std::endl;
        } else {
            std::cout << "  [UNSTABLE] ";
            if (!stablePlus) std::cout << "+1deg:(" << plusMinX << "," << plusMinY
                                       << ")-(" << plusMaxX << "," << plusMaxY << ") ";
            if (!stableMinus) std::cout << "-1deg:(" << minusMinX << "," << minusMinY
                                        << ")-(" << minusMaxX << "," << minusMaxY << ")";
            std::cout << std::endl;
        }
        std::cout << std::endl;
    }

    std::cout << "=== Summary ===" << std::endl;
    std::cout << "Range test: " << passCount << " passed, " << failCount << " failed" << std::endl;

    return (failCount == 0) ? 0 : 1;
}
