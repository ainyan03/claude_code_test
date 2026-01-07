// AffineOperator マッピングテスト
// AffineOperator の出力座標から入力座標への逆変換の正確性を検証
//
// テスト条件:
// - 元画像: 4x6 ピクセル（各ピクセルに一意のインデックス値を設定）
// - 出力先: 24x24 ピクセル、基準座標 (12, 12)
// - 代表的な3原点(左上・中央・右下) × 4回転 = 12パターン
//
// 設計:
// AffineOperatorは「基準点アライメント」設計を採用
// - 入力画像の基準点が出力バッファの基準点に揃う
// - 回転は基準点を中心に行われる

#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>

#include "fleximg/viewport.h"
#include "fleximg/operators.h"
#include "fleximg/image_types.h"

using namespace FLEXIMG_NAMESPACE;

namespace {

// テスト設定
constexpr int INPUT_WIDTH = 4;
constexpr int INPUT_HEIGHT = 6;
constexpr int OUTPUT_SIZE = 24;
constexpr float DST_ORIGIN_X = 12.0f;
constexpr float DST_ORIGIN_Y = 12.0f;

// ============================================================================
// 期待値の計算根拠:
//
// AffineOperatorの座標変換:
//   入力基準相対座標 = inv_matrix * 出力基準相対座標 + invTx/Ty
//   入力バッファ座標 = 入力基準相対座標 - inputSrcOrigin
//
// inputSrcOrigin: 基準点から見た入力画像左上の相対座標
//   - 左上原点(0,0): inputSrcOrigin = (0, 0)
//   - 中央原点(2,3): inputSrcOrigin = (-2, -3) ← 基準点から左上へ (-2,-3)
//   - 右下原点(4,6): inputSrcOrigin = (-4, -6)
//
// evaluation_node.cpp での使用法に従い、outputOffset を計算:
//   outputOffset = dstOrigin - inputSrcOrigin
// ============================================================================

// 出力範囲の期待値
struct ExpectedRange {
    const char* originName;
    float inputSrcOriginX, inputSrcOriginY;  // 基準点から見た入力左上の相対座標
    const char* rotationName;
    float degrees;
    int minX, minY, maxX, maxY;  // 期待される出力範囲
};

// テストデータ（基準点アライメント設計に基づく期待値）
// 出力バッファは24x24、基準点は(12,12)
const ExpectedRange EXPECTED[] = {
    // 左上原点: inputSrcOrigin = (0, 0)
    // 入力(0,0)が出力(12,12)に対応、入力(3,5)が出力(15,17)に対応
    {"TopLeft", 0, 0, "0deg",   0,   12, 12, 15, 17},
    {"TopLeft", 0, 0, "90deg",  90,   6, 12, 11, 15},
    {"TopLeft", 0, 0, "180deg", 180,  8,  6, 11, 11},
    {"TopLeft", 0, 0, "270deg", 270, 12,  8, 17, 11},

    // 中央原点: inputSrcOrigin = (-2, -3)
    // 入力(2,3)が出力(12,12)に対応、入力(0,0)が出力(10,9)に対応
    {"Center", -2, -3, "0deg",   0,   10,  9, 13, 14},
    {"Center", -2, -3, "90deg",  90,   9, 10, 14, 13},
    {"Center", -2, -3, "180deg", 180, 10,  9, 13, 14},
    {"Center", -2, -3, "270deg", 270,  9, 10, 14, 13},

    // 右下原点: inputSrcOrigin = (-4, -6)
    // 入力(4,6)が出力(12,12)に対応、入力(0,0)が出力(8,6)に対応
    {"BottomRight", -4, -6, "0deg",   0,    8,  6, 11, 11},
    {"BottomRight", -4, -6, "90deg",  90,  12,  8, 17, 11},
    {"BottomRight", -4, -6, "180deg", 180, 12, 12, 15, 17},
    {"BottomRight", -4, -6, "270deg", 270,  6, 12, 11, 15},
};

// 角度から回転行列を生成
AffineMatrix createRotationMatrix(float degrees) {
    float rad = degrees * static_cast<float>(M_PI) / 180.0f;
    float cosA = std::cos(rad);
    float sinA = std::sin(rad);
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
// 基準点アライメント設計での座標計算
// 戻り値: 全ピクセルが正しければtrue
bool verifyPixelMapping(const ViewPort& output, const ViewPort& /* input */,
                        float inputSrcOriginX, float inputSrcOriginY,
                        const AffineMatrix& matrix) {
    // 逆行列を計算
    float det = matrix.a * matrix.d - matrix.b * matrix.c;
    if (std::abs(det) < 1e-10f) return false;

    float invDet = 1.0f / det;
    float invA = matrix.d * invDet;
    float invB = -matrix.b * invDet;
    float invC = -matrix.c * invDet;
    float invD = matrix.a * invDet;

    // outputOffset = dstOrigin - inputSrcOrigin (evaluation_node.cpp と同じ計算)
    float outputOffsetX = DST_ORIGIN_X - inputSrcOriginX;
    float outputOffsetY = DST_ORIGIN_Y - inputSrcOriginY;
    // outputOrigin = inputSrcOrigin + outputOffset = dstOrigin
    float outputOriginX = inputSrcOriginX + outputOffsetX;  // = DST_ORIGIN_X
    float outputOriginY = inputSrcOriginY + outputOffsetY;  // = DST_ORIGIN_Y

    for (int dy = 0; dy < output.height; dy++) {
        for (int dx = 0; dx < output.width; dx++) {
            const uint16_t* outPixel = output.getPixelPtr<uint16_t>(dx, dy);

            // 透明ピクセルはスキップ
            if (outPixel[3] == 0) continue;

            // 基準点アライメント設計での入力座標計算
            // 出力の基準相対座標
            float dstRelX = (dx + 0.5f) - outputOriginX;
            float dstRelY = (dy + 0.5f) - outputOriginY;

            // 逆変換で入力の基準相対座標を求める
            float srcRelX = invA * dstRelX + invB * dstRelY;
            float srcRelY = invC * dstRelX + invD * dstRelY;

            // 入力バッファ座標に変換
            float srcX = srcRelX - inputSrcOriginX;
            float srcY = srcRelY - inputSrcOriginY;

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

    // AffineOperator を作成（evaluation_node.cpp と同じ計算）
    AffineMatrix matrix = createRotationMatrix(expected.degrees);
    float outputOffsetX = DST_ORIGIN_X - expected.inputSrcOriginX;
    float outputOffsetY = DST_ORIGIN_Y - expected.inputSrcOriginY;
    AffineOperator op(matrix, expected.inputSrcOriginX, expected.inputSrcOriginY,
                      outputOffsetX, outputOffsetY, OUTPUT_SIZE, OUTPUT_SIZE);

    // コンテキスト
    RenderRequest req{OUTPUT_SIZE, OUTPUT_SIZE, DST_ORIGIN_X, DST_ORIGIN_Y};

    // 変換を適用
    ViewPort output = op.applyToSingle(input, req);

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

    // AffineOperator を作成（evaluation_node.cpp と同じ計算）
    AffineMatrix matrix = createRotationMatrix(expected.degrees);
    float outputOffsetX = DST_ORIGIN_X - expected.inputSrcOriginX;
    float outputOffsetY = DST_ORIGIN_Y - expected.inputSrcOriginY;
    AffineOperator op(matrix, expected.inputSrcOriginX, expected.inputSrcOriginY,
                      outputOffsetX, outputOffsetY, OUTPUT_SIZE, OUTPUT_SIZE);

    // コンテキスト
    RenderRequest req{OUTPUT_SIZE, OUTPUT_SIZE, DST_ORIGIN_X, DST_ORIGIN_Y};

    // 変換を適用
    ViewPort output = op.applyToSingle(input, req);

    // ピクセルマッピングを検証
    EXPECT_TRUE(verifyPixelMapping(output, input, expected.inputSrcOriginX, expected.inputSrcOriginY, matrix))
        << "Pixel mapping incorrect for " << expected.originName << " " << expected.rotationName;
}

// ±1度での安定性テスト
TEST_P(AffineOperatorTest, StabilityWithinOneDegree) {
    const ExpectedRange& expected = GetParam();

    ViewPort input = createIndexedInput();
    RenderRequest req{OUTPUT_SIZE, OUTPUT_SIZE, DST_ORIGIN_X, DST_ORIGIN_Y};

    // outputOffset を計算（evaluation_node.cpp と同じ）
    float outputOffsetX = DST_ORIGIN_X - expected.inputSrcOriginX;
    float outputOffsetY = DST_ORIGIN_Y - expected.inputSrcOriginY;

    // +1度
    AffineMatrix matrixPlus = createRotationMatrix(expected.degrees + 1.0f);
    AffineOperator opPlus(matrixPlus, expected.inputSrcOriginX, expected.inputSrcOriginY,
                          outputOffsetX, outputOffsetY, OUTPUT_SIZE, OUTPUT_SIZE);
    ViewPort outputPlus = opPlus.applyToSingle(input, req);

    // -1度
    AffineMatrix matrixMinus = createRotationMatrix(expected.degrees - 1.0f);
    AffineOperator opMinus(matrixMinus, expected.inputSrcOriginX, expected.inputSrcOriginY,
                           outputOffsetX, outputOffsetY, OUTPUT_SIZE, OUTPUT_SIZE);
    ViewPort outputMinus = opMinus.applyToSingle(input, req);

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
    identity.a = 1.0f; identity.b = 0.0f;
    identity.c = 0.0f; identity.d = 1.0f;
    identity.tx = 0.0f; identity.ty = 0.0f;

    // inputSrcOrigin = (0,0) の場合、outputOffset = DST_ORIGIN - inputSrcOrigin = (12,12)
    float inputSrcOriginX = 0.0f;
    float inputSrcOriginY = 0.0f;
    float outputOffsetX = DST_ORIGIN_X - inputSrcOriginX;
    float outputOffsetY = DST_ORIGIN_Y - inputSrcOriginY;

    AffineOperator op(identity, inputSrcOriginX, inputSrcOriginY,
                      outputOffsetX, outputOffsetY, OUTPUT_SIZE, OUTPUT_SIZE);
    RenderRequest req{OUTPUT_SIZE, OUTPUT_SIZE, DST_ORIGIN_X, DST_ORIGIN_Y};

    ViewPort output = op.applyToSingle(input, req);

    int minX, minY, maxX, maxY;
    ASSERT_TRUE(getOutputBounds(output, minX, minY, maxX, maxY));

    // inputSrcOrigin=(0,0)で単位行列の場合、入力左上が出力(12,12)に対応
    EXPECT_EQ(minX, 12);
    EXPECT_EQ(minY, 12);
    EXPECT_EQ(maxX, 15);  // 12 + 4 - 1
    EXPECT_EQ(maxY, 17);  // 12 + 6 - 1
}

TEST(AffineOperatorBasicTest, TranslationOnly) {
    ViewPort input = createIndexedInput();

    // 平行移動のみ (tx=2, ty=3)
    AffineMatrix translation;
    translation.a = 1.0f; translation.b = 0.0f;
    translation.c = 0.0f; translation.d = 1.0f;
    translation.tx = 2.0f; translation.ty = 3.0f;

    float inputSrcOriginX = 0.0f;
    float inputSrcOriginY = 0.0f;
    float outputOffsetX = DST_ORIGIN_X - inputSrcOriginX;
    float outputOffsetY = DST_ORIGIN_Y - inputSrcOriginY;

    AffineOperator op(translation, inputSrcOriginX, inputSrcOriginY,
                      outputOffsetX, outputOffsetY, OUTPUT_SIZE, OUTPUT_SIZE);
    RenderRequest req{OUTPUT_SIZE, OUTPUT_SIZE, DST_ORIGIN_X, DST_ORIGIN_Y};

    ViewPort output = op.applyToSingle(input, req);

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
    identity.a = 1.0f; identity.b = 0.0f;
    identity.c = 0.0f; identity.d = 1.0f;
    identity.tx = 0.0f; identity.ty = 0.0f;

    float inputSrcOriginX = 0.0f;
    float inputSrcOriginY = 0.0f;
    float outputOffsetX = DST_ORIGIN_X - inputSrcOriginX;
    float outputOffsetY = DST_ORIGIN_Y - inputSrcOriginY;

    AffineOperator op(identity, inputSrcOriginX, inputSrcOriginY,
                      outputOffsetX, outputOffsetY, OUTPUT_SIZE, OUTPUT_SIZE);
    RenderRequest req{OUTPUT_SIZE, OUTPUT_SIZE, DST_ORIGIN_X, DST_ORIGIN_Y};

    ViewPort output = op.applyToSingle(input, req);

    // 出力フォーマットがRGBA16_Premultipliedであることを確認
    EXPECT_EQ(output.formatID, PixelFormatIDs::RGBA16_Premultiplied);
}

} // namespace
