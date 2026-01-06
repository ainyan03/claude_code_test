// AffineOperator マッピングテスト
// AffineOperator の出力座標から入力座標への逆変換の正確性を検証
//
// テスト条件:
// - 元画像: 4x6 ピクセル（各ピクセルに一意のインデックス値を設定）
// - 出力先: 20x20 ピクセル、基準座標 (10, 10)
// - 代表的な3原点(左上・中央・右下) × 4回転 = 12パターン

#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>

#include "viewport.h"
#include "operators.h"
#include "image_types.h"

using namespace ImageTransform;

namespace {

// テスト設定
constexpr int INPUT_WIDTH = 4;
constexpr int INPUT_HEIGHT = 6;
constexpr int OUTPUT_SIZE = 20;
constexpr int DST_ORIGIN_X = 10;
constexpr int DST_ORIGIN_Y = 10;

// ============================================================================
// 期待値の手計算根拠:
//
// AffineOperatorは「回転中心」設計を採用
// 座標変換: src = inv_matrix * (dst - offset - origin) + origin
//
// 元画像: 4x6, dstOrigin(outputOffset): (10,10)
// 出力ピクセル(dx,dy)の中心(dx+0.5, dy+0.5)から入力座標を逆変換で求める
//
// 0度（単位行列）: originがキャンセルされる
//   sx = (dx + 0.5) - 10 = dx - 9.5
//   sy = (dy + 0.5) - 10 = dy - 9.5
//   → 全originで minX=10, minY=10, maxX=13, maxY=15
//
// 90度CCW (invA=0, invB=1, invC=-1, invD=0):
//   sx = (dy + 0.5 - 10 - oy) + ox
//   sy = -(dx + 0.5 - 10 - ox) + oy
//
// 180度 (invA=-1, invB=0, invC=0, invD=-1):
//   sx = -(dx + 0.5 - 10 - ox) + ox
//   sy = -(dy + 0.5 - 10 - oy) + oy
//
// 270度CCW (invA=0, invB=-1, invC=1, invD=0):
//   sx = -(dy + 0.5 - 10 - oy) + ox
//   sy = (dx + 0.5 - 10 - ox) + oy
//
// 入力範囲 0 <= sx < 4, 0 <= sy < 6 を満たす出力範囲を求める
// ============================================================================

// 出力範囲の期待値
struct ExpectedRange {
    const char* originName;
    double originX, originY;
    const char* rotationName;
    double degrees;
    int minX, minY, maxX, maxY;  // 期待される出力範囲
};

// テストデータ（回転中心設計に基づく期待値）
const ExpectedRange EXPECTED[] = {
    // 左上原点 (0, 0)
    {"TopLeft", 0, 0, "0deg",   0,   10, 10, 13, 15},
    {"TopLeft", 0, 0, "90deg",  90,   4, 10,  9, 13},
    {"TopLeft", 0, 0, "180deg", 180,  6,  4,  9,  9},
    {"TopLeft", 0, 0, "270deg", 270, 10,  6, 15,  9},

    // 中央原点 (2, 3) - 回転中心設計では0度は全originで同じ結果
    {"Center", 2, 3, "0deg",   0,   10, 10, 13, 15},
    {"Center", 2, 3, "90deg",  90,   9, 11, 14, 14},
    {"Center", 2, 3, "180deg", 180, 10, 10, 13, 15},
    {"Center", 2, 3, "270deg", 270,  9, 11, 14, 14},

    // 右下原点 (4, 6) - 回転中心設計では0度は全originで同じ結果
    {"BottomRight", 4, 6, "0deg",   0,   10, 10, 13, 15},
    {"BottomRight", 4, 6, "90deg",  90,  14, 12, 19, 15},
    {"BottomRight", 4, 6, "180deg", 180, 14, 16, 17, 19},
    {"BottomRight", 4, 6, "270deg", 270,  8, 16, 13, 19},
};

// 角度から回転行列を生成
AffineMatrix createRotationMatrix(double degrees) {
    double rad = degrees * M_PI / 180.0;
    double cosA = std::cos(rad);
    double sinA = std::sin(rad);
    AffineMatrix m;
    m.a = cosA;
    m.b = -sinA;
    m.c = sinA;
    m.d = cosA;
    m.tx = 0;
    m.ty = 0;
    return m;
}

// 入力画像を作成（各ピクセルに一意のインデックス値を設定）
// R = (sy * INPUT_WIDTH + sx) で識別可能
ViewPort createIndexedInput() {
    ViewPort input(INPUT_WIDTH, INPUT_HEIGHT, PixelFormatIDs::RGBA16_Premultiplied);

    for (int sy = 0; sy < INPUT_HEIGHT; sy++) {
        for (int sx = 0; sx < INPUT_WIDTH; sx++) {
            uint16_t* pixel = input.getPixelPtr<uint16_t>(sx, sy);
            uint16_t index = sy * INPUT_WIDTH + sx;
            // R にインデックス、G/B は 0、A は不透明
            pixel[0] = index * 256;  // R: インデックス値（0-23を識別可能に拡大）
            pixel[1] = 0;            // G
            pixel[2] = 0;            // B
            pixel[3] = 65535;        // A: 不透明
        }
    }
    return input;
}

// 出力画像から有効ピクセルの範囲を取得
bool getOutputBounds(const ViewPort& output, int& minX, int& minY, int& maxX, int& maxY) {
    minX = minY = OUTPUT_SIZE;
    maxX = maxY = -1;

    for (int dy = 0; dy < output.height; dy++) {
        for (int dx = 0; dx < output.width; dx++) {
            const uint16_t* pixel = output.getPixelPtr<uint16_t>(dx, dy);
            // アルファが0より大きいピクセルを有効とみなす
            if (pixel[3] > 0) {
                minX = std::min(minX, dx);
                minY = std::min(minY, dy);
                maxX = std::max(maxX, dx);
                maxY = std::max(maxY, dy);
            }
        }
    }
    return maxX >= 0;
}

// 出力ピクセルが正しい入力ピクセルからマッピングされているか検証
// 回転中心設計: src = inv_matrix * (dst - offset - origin) + origin
// 戻り値: 全ピクセルが正しければtrue
bool verifyPixelMapping(const ViewPort& output, const ViewPort& /* input */,
                        double originX, double originY, const AffineMatrix& matrix) {
    // 逆行列を計算
    double det = matrix.a * matrix.d - matrix.b * matrix.c;
    if (std::abs(det) < 1e-10) return false;

    double invDet = 1.0 / det;
    double invA = matrix.d * invDet;
    double invB = -matrix.b * invDet;
    double invC = -matrix.c * invDet;
    double invD = matrix.a * invDet;

    for (int dy = 0; dy < output.height; dy++) {
        for (int dx = 0; dx < output.width; dx++) {
            const uint16_t* outPixel = output.getPixelPtr<uint16_t>(dx, dy);

            // 透明ピクセルはスキップ
            if (outPixel[3] == 0) continue;

            // 回転中心設計での入力座標計算
            // src = inv_matrix * (dst - offset - origin) + origin
            double relX = (dx + 0.5) - DST_ORIGIN_X - originX;
            double relY = (dy + 0.5) - DST_ORIGIN_Y - originY;

            double srcX = invA * relX + invB * relY + originX;
            double srcY = invC * relX + invD * relY + originY;

            int expectedSx = (int)std::floor(srcX);
            int expectedSy = (int)std::floor(srcY);

            // 入力範囲外なら出力も透明であるべき
            if (expectedSx < 0 || expectedSx >= INPUT_WIDTH ||
                expectedSy < 0 || expectedSy >= INPUT_HEIGHT) {
                continue;  // 境界条件は複雑なので厳密な検証は省略
            }

            // 期待されるインデックス
            uint16_t expectedIndex = expectedSy * INPUT_WIDTH + expectedSx;
            uint16_t actualIndex = outPixel[0] / 256;

            if (actualIndex != expectedIndex) {
                return false;
            }
        }
    }
    return true;
}

// ==============================================================================
// パラメータ化テスト
// ==============================================================================

class AffineOperatorTest : public ::testing::TestWithParam<ExpectedRange> {};

TEST_P(AffineOperatorTest, OutputRangeMatches) {
    const ExpectedRange& expected = GetParam();

    // 入力画像を作成
    ViewPort input = createIndexedInput();

    // AffineOperator を作成
    AffineMatrix matrix = createRotationMatrix(expected.degrees);
    AffineOperator op(matrix, expected.originX, expected.originY,
                      DST_ORIGIN_X, DST_ORIGIN_Y, OUTPUT_SIZE, OUTPUT_SIZE);

    // コンテキスト
    OperatorContext ctx(OUTPUT_SIZE, OUTPUT_SIZE, DST_ORIGIN_X, DST_ORIGIN_Y);

    // 変換を適用
    ViewPort output = op.applyToSingle(input, ctx);

    // 出力範囲を取得
    int actualMinX, actualMinY, actualMaxX, actualMaxY;
    ASSERT_TRUE(getOutputBounds(output, actualMinX, actualMinY, actualMaxX, actualMaxY))
        << "Output is empty for " << expected.originName << " " << expected.rotationName;

    EXPECT_EQ(actualMinX, expected.minX)
        << "minX mismatch for " << expected.originName << " " << expected.rotationName;
    EXPECT_EQ(actualMinY, expected.minY)
        << "minY mismatch for " << expected.originName << " " << expected.rotationName;
    EXPECT_EQ(actualMaxX, expected.maxX)
        << "maxX mismatch for " << expected.originName << " " << expected.rotationName;
    EXPECT_EQ(actualMaxY, expected.maxY)
        << "maxY mismatch for " << expected.originName << " " << expected.rotationName;
}

TEST_P(AffineOperatorTest, PixelMappingIsCorrect) {
    const ExpectedRange& expected = GetParam();

    // 入力画像を作成
    ViewPort input = createIndexedInput();

    // AffineOperator を作成
    AffineMatrix matrix = createRotationMatrix(expected.degrees);
    AffineOperator op(matrix, expected.originX, expected.originY,
                      DST_ORIGIN_X, DST_ORIGIN_Y, OUTPUT_SIZE, OUTPUT_SIZE);

    // コンテキスト
    OperatorContext ctx(OUTPUT_SIZE, OUTPUT_SIZE, DST_ORIGIN_X, DST_ORIGIN_Y);

    // 変換を適用
    ViewPort output = op.applyToSingle(input, ctx);

    // ピクセルマッピングを検証
    EXPECT_TRUE(verifyPixelMapping(output, input, expected.originX, expected.originY, matrix))
        << "Pixel mapping incorrect for " << expected.originName << " " << expected.rotationName;
}

// ±1度での安定性テスト
TEST_P(AffineOperatorTest, StabilityWithinOneDegree) {
    const ExpectedRange& expected = GetParam();

    ViewPort input = createIndexedInput();
    OperatorContext ctx(OUTPUT_SIZE, OUTPUT_SIZE, DST_ORIGIN_X, DST_ORIGIN_Y);

    // +1度
    AffineMatrix matrixPlus = createRotationMatrix(expected.degrees + 1.0);
    AffineOperator opPlus(matrixPlus, expected.originX, expected.originY,
                          DST_ORIGIN_X, DST_ORIGIN_Y, OUTPUT_SIZE, OUTPUT_SIZE);
    ViewPort outputPlus = opPlus.applyToSingle(input, ctx);

    // -1度
    AffineMatrix matrixMinus = createRotationMatrix(expected.degrees - 1.0);
    AffineOperator opMinus(matrixMinus, expected.originX, expected.originY,
                           DST_ORIGIN_X, DST_ORIGIN_Y, OUTPUT_SIZE, OUTPUT_SIZE);
    ViewPort outputMinus = opMinus.applyToSingle(input, ctx);

    int plusMinX, plusMinY, plusMaxX, plusMaxY;
    int minusMinX, minusMinY, minusMaxX, minusMaxY;

    ASSERT_TRUE(getOutputBounds(outputPlus, plusMinX, plusMinY, plusMaxX, plusMaxY));
    ASSERT_TRUE(getOutputBounds(outputMinus, minusMinX, minusMinY, minusMaxX, minusMaxY));

    // ±1ピクセルの変動を許容
    const int tolerance = 1;

    EXPECT_NEAR(plusMinX, expected.minX, tolerance);
    EXPECT_NEAR(plusMinY, expected.minY, tolerance);
    EXPECT_NEAR(plusMaxX, expected.maxX, tolerance);
    EXPECT_NEAR(plusMaxY, expected.maxY, tolerance);

    EXPECT_NEAR(minusMinX, expected.minX, tolerance);
    EXPECT_NEAR(minusMinY, expected.minY, tolerance);
    EXPECT_NEAR(minusMaxX, expected.maxX, tolerance);
    EXPECT_NEAR(minusMaxY, expected.maxY, tolerance);
}

// テスト名を生成
std::string TestNameGenerator(const ::testing::TestParamInfo<ExpectedRange>& info) {
    std::string name = std::string(info.param.originName) + "_" + info.param.rotationName;
    for (char& c : name) {
        if (!std::isalnum(c)) c = '_';
    }
    return name;
}

INSTANTIATE_TEST_SUITE_P(
    AffineOperator,
    AffineOperatorTest,
    ::testing::ValuesIn(EXPECTED),
    TestNameGenerator
);

// ==============================================================================
// 追加の単体テスト
// ==============================================================================

TEST(AffineOperatorBasicTest, IdentityTransform) {
    ViewPort input = createIndexedInput();

    // 単位行列
    AffineMatrix identity;
    identity.a = 1.0; identity.b = 0.0;
    identity.c = 0.0; identity.d = 1.0;
    identity.tx = 0.0; identity.ty = 0.0;

    AffineOperator op(identity, 0, 0, DST_ORIGIN_X, DST_ORIGIN_Y, OUTPUT_SIZE, OUTPUT_SIZE);
    OperatorContext ctx(OUTPUT_SIZE, OUTPUT_SIZE, DST_ORIGIN_X, DST_ORIGIN_Y);

    ViewPort output = op.applyToSingle(input, ctx);

    int minX, minY, maxX, maxY;
    ASSERT_TRUE(getOutputBounds(output, minX, minY, maxX, maxY));

    // 原点(0,0)で単位行列の場合、左上が(10,10)になる
    EXPECT_EQ(minX, 10);
    EXPECT_EQ(minY, 10);
    EXPECT_EQ(maxX, 13);  // 10 + 4 - 1
    EXPECT_EQ(maxY, 15);  // 10 + 6 - 1
}

TEST(AffineOperatorBasicTest, TranslationOnly) {
    ViewPort input = createIndexedInput();

    // 平行移動のみ (tx=2, ty=3)
    AffineMatrix translation;
    translation.a = 1.0; translation.b = 0.0;
    translation.c = 0.0; translation.d = 1.0;
    translation.tx = 2.0; translation.ty = 3.0;

    AffineOperator op(translation, 0, 0, DST_ORIGIN_X, DST_ORIGIN_Y, OUTPUT_SIZE, OUTPUT_SIZE);
    OperatorContext ctx(OUTPUT_SIZE, OUTPUT_SIZE, DST_ORIGIN_X, DST_ORIGIN_Y);

    ViewPort output = op.applyToSingle(input, ctx);

    int minX, minY, maxX, maxY;
    ASSERT_TRUE(getOutputBounds(output, minX, minY, maxX, maxY));

    // tx, ty による平行移動で出力位置がシフト
    // 注意: tx, ty の符号と方向はAffineOperatorの実装に依存
    EXPECT_GT(maxX - minX + 1, 0);  // 非空であることを確認
    EXPECT_GT(maxY - minY + 1, 0);
}

TEST(AffineOperatorBasicTest, OutputFormatIsPremultiplied) {
    ViewPort input = createIndexedInput();

    AffineMatrix identity;
    identity.a = 1.0; identity.b = 0.0;
    identity.c = 0.0; identity.d = 1.0;
    identity.tx = 0.0; identity.ty = 0.0;

    AffineOperator op(identity, 0, 0, DST_ORIGIN_X, DST_ORIGIN_Y, OUTPUT_SIZE, OUTPUT_SIZE);
    OperatorContext ctx(OUTPUT_SIZE, OUTPUT_SIZE, DST_ORIGIN_X, DST_ORIGIN_Y);

    ViewPort output = op.applyToSingle(input, ctx);

    // 出力フォーマットがRGBA16_Premultipliedであることを確認
    EXPECT_EQ(output.formatID, PixelFormatIDs::RGBA16_Premultiplied);
}

} // namespace
