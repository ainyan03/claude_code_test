// NodeGraphEvaluator 統合テスト
// ノードグラフAPIを使用したパイプライン全体の動作検証
//
// テスト条件:
// - 元画像: 5x10 ピクセル（各ピクセルに一意の色値を設定）
// - 出力先: 32x32 ピクセル、基準座標 (16, 16)
// - 9つの基準点 × 複数の回転角度 = 多数のパターン
//
// 検証内容:
// - ノードグラフAPIの正しい使用方法
// - パイプライン全体を通した座標変換の正確性
// - 出力画像のピクセル値が期待通りか

#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>

#include "fleximg/node_graph.h"

using namespace FLEXIMG_NAMESPACE;

namespace {

// テスト設定
constexpr int INPUT_WIDTH = 5;
constexpr int INPUT_HEIGHT = 10;
constexpr int OUTPUT_SIZE = 32;
constexpr float DST_ORIGIN_X = 16.0f;
constexpr float DST_ORIGIN_Y = 16.0f;

// 基準点定義（ピクセル座標）
// srcOriginX/Y は ImageEvalNode で以下のように解釈される:
//   imgLeft = -srcOriginX
//   imgTop = -srcOriginY
// 例: 中央基準(2.5, 5.0) → 画像の中央が原点に対応
struct SrcOriginPoint {
    const char* name;
    float x, y;  // ピクセル座標
};

const SrcOriginPoint SRC_ORIGINS[] = {
    {"TopLeft",      0.0f, 0.0f},
    {"TopCenter",    INPUT_WIDTH / 2.0f, 0.0f},                      // 2.5
    {"TopRight",     static_cast<float>(INPUT_WIDTH), 0.0f},         // 5.0
    {"MiddleLeft",   0.0f, INPUT_HEIGHT / 2.0f},                     // 5.0
    {"Center",       INPUT_WIDTH / 2.0f, INPUT_HEIGHT / 2.0f},       // 2.5, 5.0
    {"MiddleRight",  static_cast<float>(INPUT_WIDTH), INPUT_HEIGHT / 2.0f},
    {"BottomLeft",   0.0f, static_cast<float>(INPUT_HEIGHT)},        // 10.0
    {"BottomCenter", INPUT_WIDTH / 2.0f, static_cast<float>(INPUT_HEIGHT)},
    {"BottomRight",  static_cast<float>(INPUT_WIDTH), static_cast<float>(INPUT_HEIGHT)},
};

// 回転角度
const float ROTATION_ANGLES[] = {0, 30, 45, 90, 135, 180, 270};

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

// 入力画像を作成（各ピクセルに一意の色値を設定）
// R = x座標 * 50, G = y座標 * 25, B = 128, A = 255
void createIndexedInputImage(std::vector<uint8_t>& data) {
    data.resize(INPUT_WIDTH * INPUT_HEIGHT * 4);
    for (int y = 0; y < INPUT_HEIGHT; y++) {
        for (int x = 0; x < INPUT_WIDTH; x++) {
            int idx = (y * INPUT_WIDTH + x) * 4;
            data[idx + 0] = static_cast<uint8_t>(x * 50);   // R: x座標で変化
            data[idx + 1] = static_cast<uint8_t>(y * 25);   // G: y座標で変化
            data[idx + 2] = 128;                            // B: 固定
            data[idx + 3] = 255;                            // A: 不透明
        }
    }
}

// 出力画像に描画されたピクセルがあるか確認
bool hasVisiblePixels(const std::vector<uint8_t>& outputData, int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 4;
            if (outputData[idx + 3] > 0) {
                return true;
            }
        }
    }
    return false;
}

// 出力画像から有効ピクセルの範囲を取得
bool getOutputBounds(const std::vector<uint8_t>& outputData, int width, int height,
                     int& minX, int& minY, int& maxX, int& maxY) {
    minX = minY = width;
    maxX = maxY = -1;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 4;
            if (outputData[idx + 3] > 0) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }
    return maxX >= 0;
}

// ==============================================================================
// テストフィクスチャ
// ==============================================================================

class NodeGraphIntegrationTest : public ::testing::Test {
protected:
    std::vector<uint8_t> inputData;
    std::vector<uint8_t> outputData;

    void SetUp() override {
        createIndexedInputImage(inputData);
        outputData.resize(OUTPUT_SIZE * OUTPUT_SIZE * 4, 0);
    }

    void clearOutput() {
        std::fill(outputData.begin(), outputData.end(), 0);
    }

    // ノードグラフを設定して評価
    void evaluateImageAffineOutput(float srcOriginX, float srcOriginY,
                                   const AffineMatrix& matrix) {
        NodeGraphEvaluator evaluator(OUTPUT_SIZE, OUTPUT_SIZE);

        // 画像を登録
        evaluator.registerImage(0, inputData.data(), INPUT_WIDTH, INPUT_HEIGHT);
        evaluator.registerImage(1, outputData.data(), OUTPUT_SIZE, OUTPUT_SIZE);

        // 出力の基準点を中央に設定
        evaluator.setDstOrigin(DST_ORIGIN_X, DST_ORIGIN_Y);

        // ノードを設定
        std::vector<GraphNode> nodes(3);

        // 元画像ノード
        nodes[0].type = "image";
        nodes[0].id = "img";
        nodes[0].imageId = 0;
        nodes[0].srcOriginX = srcOriginX;
        nodes[0].srcOriginY = srcOriginY;

        // アフィンノード
        nodes[1].type = "affine";
        nodes[1].id = "affine";
        nodes[1].affineMatrix = matrix;

        // 出力ノード
        nodes[2].type = "output";
        nodes[2].id = "out";
        nodes[2].imageId = 1;

        evaluator.setNodes(nodes);

        // 接続を設定
        // 単一入力ノードの入力ポート名は "in"
        std::vector<GraphConnection> connections = {
            {"img", "output", "affine", "in"},
            {"affine", "output", "out", "in"},
        };
        evaluator.setConnections(connections);

        // 評価実行
        evaluator.evaluateGraph();
    }
};

// ==============================================================================
// 基本動作テスト
// ==============================================================================

TEST_F(NodeGraphIntegrationTest, BasicPipelineWorks) {
    // 単位行列（回転なし）で基本動作を確認
    AffineMatrix identity;
    identity.a = 1.0f; identity.b = 0.0f;
    identity.c = 0.0f; identity.d = 1.0f;
    identity.tx = 0.0f; identity.ty = 0.0f;

    evaluateImageAffineOutput(0.0f, 0.0f, identity);

    // 出力に何か描画されているはず
    EXPECT_TRUE(hasVisiblePixels(outputData, OUTPUT_SIZE, OUTPUT_SIZE));
}

TEST_F(NodeGraphIntegrationTest, IdentityTransformPosition) {
    // 単位行列、左上原点(0,0)の場合
    // srcOrigin=(0,0) → imgLeft = 0, imgTop = 0
    // 入力の左上(0,0)が出力の基準点(16,16)に対応
    AffineMatrix identity;
    identity.a = 1.0f; identity.b = 0.0f;
    identity.c = 0.0f; identity.d = 1.0f;
    identity.tx = 0.0f; identity.ty = 0.0f;

    evaluateImageAffineOutput(0.0f, 0.0f, identity);

    int minX, minY, maxX, maxY;
    ASSERT_TRUE(getOutputBounds(outputData, OUTPUT_SIZE, OUTPUT_SIZE, minX, minY, maxX, maxY));

    // srcOrigin=(0,0)の場合、画像は基準点の右下に配置
    // 出力範囲は (16,16) から (20,25) まで
    EXPECT_EQ(minX, 16);
    EXPECT_EQ(minY, 16);
    EXPECT_EQ(maxX, 20);  // 16 + 5 - 1
    EXPECT_EQ(maxY, 25);  // 16 + 10 - 1
}

TEST_F(NodeGraphIntegrationTest, CenterOriginIdentity) {
    // 単位行列、中央原点(2.5, 5.0)の場合
    // srcOrigin=(2.5, 5.0) → imgLeft = -2.5, imgTop = -5
    // 入力の中央が出力の基準点(16,16)に対応
    AffineMatrix identity;
    identity.a = 1.0f; identity.b = 0.0f;
    identity.c = 0.0f; identity.d = 1.0f;
    identity.tx = 0.0f; identity.ty = 0.0f;

    float centerX = INPUT_WIDTH / 2.0f;   // 2.5
    float centerY = INPUT_HEIGHT / 2.0f;  // 5.0
    evaluateImageAffineOutput(centerX, centerY, identity);

    int minX, minY, maxX, maxY;
    ASSERT_TRUE(getOutputBounds(outputData, OUTPUT_SIZE, OUTPUT_SIZE, minX, minY, maxX, maxY));

    // srcOrigin=(2.5, 5.0) → imgLeft = -2.5, imgTop = -5
    // 画像は基準点を中心に配置
    // 入力左上は出力の (16-2.5, 16-5) = (13.5, 11) 付近
    // 入力右下は出力の (16+2.5, 16+5) = (18.5, 21) 付近
    EXPECT_NEAR(minX, 13, 1);  // floor(13.5)
    EXPECT_NEAR(minY, 11, 1);
    EXPECT_NEAR(maxX, 18, 1);  // floor(18.5)
    EXPECT_NEAR(maxY, 20, 1);
}

// ==============================================================================
// 回転テスト
// ==============================================================================

TEST_F(NodeGraphIntegrationTest, Rotation90Degrees) {
    AffineMatrix rot90 = createRotationMatrix(90.0f);
    evaluateImageAffineOutput(0.0f, 0.0f, rot90);

    int minX, minY, maxX, maxY;
    ASSERT_TRUE(getOutputBounds(outputData, OUTPUT_SIZE, OUTPUT_SIZE, minX, minY, maxX, maxY));

    // 90度回転で5x10が10x5に変わる
    int width = maxX - minX + 1;
    int height = maxY - minY + 1;
    EXPECT_EQ(width, INPUT_HEIGHT);  // 10
    EXPECT_EQ(height, INPUT_WIDTH);  // 5
}

TEST_F(NodeGraphIntegrationTest, Rotation180Degrees) {
    AffineMatrix rot180 = createRotationMatrix(180.0f);
    evaluateImageAffineOutput(0.0f, 0.0f, rot180);

    int minX, minY, maxX, maxY;
    ASSERT_TRUE(getOutputBounds(outputData, OUTPUT_SIZE, OUTPUT_SIZE, minX, minY, maxX, maxY));

    // 180度回転でサイズは変わらない
    int width = maxX - minX + 1;
    int height = maxY - minY + 1;
    EXPECT_EQ(width, INPUT_WIDTH);
    EXPECT_EQ(height, INPUT_HEIGHT);
}

// ==============================================================================
// 全基準点 × 全角度のパラメータ化テスト
// ==============================================================================

struct TestParams {
    SrcOriginPoint origin;
    float degrees;
};

class NodeGraphParameterizedTest : public ::testing::TestWithParam<TestParams> {
protected:
    std::vector<uint8_t> inputData;
    std::vector<uint8_t> outputData;

    void SetUp() override {
        createIndexedInputImage(inputData);
        outputData.resize(OUTPUT_SIZE * OUTPUT_SIZE * 4, 0);
    }
};

TEST_P(NodeGraphParameterizedTest, ProducesVisibleOutput) {
    const TestParams& params = GetParam();

    NodeGraphEvaluator evaluator(OUTPUT_SIZE, OUTPUT_SIZE);
    evaluator.registerImage(0, inputData.data(), INPUT_WIDTH, INPUT_HEIGHT);
    evaluator.registerImage(1, outputData.data(), OUTPUT_SIZE, OUTPUT_SIZE);
    evaluator.setDstOrigin(DST_ORIGIN_X, DST_ORIGIN_Y);

    std::vector<GraphNode> nodes(3);
    nodes[0].type = "image";
    nodes[0].id = "img";
    nodes[0].imageId = 0;
    nodes[0].srcOriginX = params.origin.x;
    nodes[0].srcOriginY = params.origin.y;

    nodes[1].type = "affine";
    nodes[1].id = "affine";
    nodes[1].affineMatrix = createRotationMatrix(params.degrees);

    nodes[2].type = "output";
    nodes[2].id = "out";
    nodes[2].imageId = 1;

    evaluator.setNodes(nodes);

    std::vector<GraphConnection> connections = {
        {"img", "output", "affine", "in"},
        {"affine", "output", "out", "in"},
    };
    evaluator.setConnections(connections);

    evaluator.evaluateGraph();

    // すべての組み合わせで可視ピクセルが生成されるべき
    EXPECT_TRUE(hasVisiblePixels(outputData, OUTPUT_SIZE, OUTPUT_SIZE))
        << "No visible pixels for origin=" << params.origin.name
        << ", rotation=" << params.degrees << " degrees";
}

TEST_P(NodeGraphParameterizedTest, OutputWithinBounds) {
    const TestParams& params = GetParam();

    NodeGraphEvaluator evaluator(OUTPUT_SIZE, OUTPUT_SIZE);
    evaluator.registerImage(0, inputData.data(), INPUT_WIDTH, INPUT_HEIGHT);
    evaluator.registerImage(1, outputData.data(), OUTPUT_SIZE, OUTPUT_SIZE);
    evaluator.setDstOrigin(DST_ORIGIN_X, DST_ORIGIN_Y);

    std::vector<GraphNode> nodes(3);
    nodes[0].type = "image";
    nodes[0].id = "img";
    nodes[0].imageId = 0;
    nodes[0].srcOriginX = params.origin.x;
    nodes[0].srcOriginY = params.origin.y;

    nodes[1].type = "affine";
    nodes[1].id = "affine";
    nodes[1].affineMatrix = createRotationMatrix(params.degrees);

    nodes[2].type = "output";
    nodes[2].id = "out";
    nodes[2].imageId = 1;

    evaluator.setNodes(nodes);

    std::vector<GraphConnection> connections = {
        {"img", "output", "affine", "in"},
        {"affine", "output", "out", "in"},
    };
    evaluator.setConnections(connections);

    evaluator.evaluateGraph();

    int minX, minY, maxX, maxY;
    if (getOutputBounds(outputData, OUTPUT_SIZE, OUTPUT_SIZE, minX, minY, maxX, maxY)) {
        // 出力がバッファ範囲内に収まっている
        EXPECT_GE(minX, 0);
        EXPECT_GE(minY, 0);
        EXPECT_LT(maxX, OUTPUT_SIZE);
        EXPECT_LT(maxY, OUTPUT_SIZE);
    }
}

// テストパラメータを生成
std::vector<TestParams> GenerateTestParams() {
    std::vector<TestParams> params;
    for (const auto& origin : SRC_ORIGINS) {
        for (float angle : ROTATION_ANGLES) {
            params.push_back({origin, angle});
        }
    }
    return params;
}

// テスト名を生成
std::string ParamTestName(const ::testing::TestParamInfo<TestParams>& info) {
    std::string name = std::string(info.param.origin.name) + "_" +
                       std::to_string(static_cast<int>(info.param.degrees)) + "deg";
    for (char& c : name) {
        if (!std::isalnum(c)) c = '_';
    }
    return name;
}

INSTANTIATE_TEST_SUITE_P(
    AllOriginsAndRotations,
    NodeGraphParameterizedTest,
    ::testing::ValuesIn(GenerateTestParams()),
    ParamTestName
);

// ==============================================================================
// タイル分割テスト
// ==============================================================================

TEST_F(NodeGraphIntegrationTest, TileSplitProducesSameResult) {
    AffineMatrix rot45 = createRotationMatrix(45.0f);

    // 中央原点（ピクセル座標）
    float centerX = INPUT_WIDTH / 2.0f;   // 2.5
    float centerY = INPUT_HEIGHT / 2.0f;  // 5.0

    // タイル分割なしで評価
    evaluateImageAffineOutput(centerX, centerY, rot45);
    std::vector<uint8_t> noTileOutput = outputData;

    // タイル分割ありで評価
    clearOutput();

    NodeGraphEvaluator evaluator(OUTPUT_SIZE, OUTPUT_SIZE);
    evaluator.registerImage(0, inputData.data(), INPUT_WIDTH, INPUT_HEIGHT);
    evaluator.registerImage(1, outputData.data(), OUTPUT_SIZE, OUTPUT_SIZE);
    evaluator.setDstOrigin(DST_ORIGIN_X, DST_ORIGIN_Y);
    evaluator.setTileSize(8, 8);  // 8x8タイル

    std::vector<GraphNode> nodes(3);
    nodes[0].type = "image";
    nodes[0].id = "img";
    nodes[0].imageId = 0;
    nodes[0].srcOriginX = centerX;
    nodes[0].srcOriginY = centerY;

    nodes[1].type = "affine";
    nodes[1].id = "affine";
    nodes[1].affineMatrix = rot45;

    nodes[2].type = "output";
    nodes[2].id = "out";
    nodes[2].imageId = 1;

    evaluator.setNodes(nodes);

    std::vector<GraphConnection> connections = {
        {"img", "output", "affine", "in"},
        {"affine", "output", "out", "in"},
    };
    evaluator.setConnections(connections);
    evaluator.evaluateGraph();

    // 両方の結果が一致することを確認（許容誤差あり）
    int diffCount = 0;
    for (size_t i = 0; i < outputData.size(); i++) {
        if (std::abs(static_cast<int>(outputData[i]) - static_cast<int>(noTileOutput[i])) > 1) {
            diffCount++;
        }
    }

    // 差異が1%未満であれば許容
    float diffRatio = static_cast<float>(diffCount) / outputData.size();
    EXPECT_LT(diffRatio, 0.01f) << "Tile split produced different result: " << diffRatio * 100 << "% diff";
}

// ==============================================================================
// ピクセル値検証テスト
// ==============================================================================

TEST_F(NodeGraphIntegrationTest, PixelValuesPreserved) {
    // 単位行列で変換
    AffineMatrix identity;
    identity.a = 1.0f; identity.b = 0.0f;
    identity.c = 0.0f; identity.d = 1.0f;
    identity.tx = 0.0f; identity.ty = 0.0f;

    evaluateImageAffineOutput(0.0f, 0.0f, identity);

    // 基準点(16,16)は入力の(0,0)に対応
    // 入力(0,0)の色は R=0, G=0, B=128, A=255
    int idx = (16 * OUTPUT_SIZE + 16) * 4;
    EXPECT_NEAR(outputData[idx + 0], 0, 2);    // R
    EXPECT_NEAR(outputData[idx + 1], 0, 2);    // G
    EXPECT_NEAR(outputData[idx + 2], 128, 2);  // B
    EXPECT_GT(outputData[idx + 3], 250);       // A（不透明に近い）

    // (17,17)は入力の(1,1)に対応
    // 入力(1,1)の色は R=50, G=25, B=128, A=255
    idx = (17 * OUTPUT_SIZE + 17) * 4;
    EXPECT_NEAR(outputData[idx + 0], 50, 5);   // R
    EXPECT_NEAR(outputData[idx + 1], 25, 5);   // G
    EXPECT_NEAR(outputData[idx + 2], 128, 5);  // B
}

} // namespace
