#ifndef NODE_GRAPH_H
#define NODE_GRAPH_H

#include "viewport.h"
#include "image_processor.h"
#include "image_types.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <chrono>

namespace ImageTransform {

// 前方宣言
struct Image;

// ========================================================================
// パフォーマンス計測構造体
// ========================================================================

struct PerfMetrics {
    double filterTime;        // フィルタ処理時間（ms）
    double affineTime;        // アフィン変換時間（ms）
    double compositeTime;     // 合成処理時間（ms）
    double convertTime;       // フォーマット変換時間（ms）
    double outputTime;        // 最終出力変換時間（ms）
    int filterCount;          // フィルタ処理回数
    int affineCount;          // アフィン変換回数
    int compositeCount;       // 合成処理回数
    int convertCount;         // フォーマット変換回数

    PerfMetrics()
        : filterTime(0), affineTime(0), compositeTime(0),
          convertTime(0), outputTime(0),
          filterCount(0), affineCount(0), compositeCount(0), convertCount(0) {}

    void reset() {
        filterTime = affineTime = compositeTime = convertTime = outputTime = 0;
        filterCount = affineCount = compositeCount = convertCount = 0;
    }
};

// ========================================================================
// ノードグラフ構造定義
// ========================================================================

// 合成ノードの入力定義
struct CompositeInput {
    std::string id;
    double alpha;

    CompositeInput() : id(""), alpha(1.0) {}
    CompositeInput(const std::string& inputId, double inputAlpha)
        : id(inputId), alpha(inputAlpha) {}
};

// ノードグラフのノード定義
struct GraphNode {
    std::string type;  // "image", "filter", "composite", "affine", "output"
    std::string id;

    // image用
    int imageId;       // 画像ライブラリのID
    double srcOriginX; // 画像の原点X（ピクセル座標）
    double srcOriginY; // 画像の原点Y（ピクセル座標）

    // filter用
    std::string filterType;
    float filterParam;
    bool independent;

    // composite用
    std::vector<CompositeInput> compositeInputs;  // 動的な入力配列

    // affine用（アフィン変換ノード）
    // JS側で行列に統一されるため、行列のみ保持
    AffineMatrix affineMatrix;

    GraphNode() : imageId(-1), srcOriginX(0.0), srcOriginY(0.0),
                  filterParam(0.0f), independent(false) {}
};

// ノードグラフの接続定義
struct GraphConnection {
    std::string fromNodeId;
    std::string fromPort;
    std::string toNodeId;
    std::string toPort;
};

// ========================================================================
// ノードグラフ評価エンジン
// ========================================================================

class NodeGraphEvaluator {
public:
    NodeGraphEvaluator(int canvasWidth, int canvasHeight);

    // 画像ライブラリに画像を登録（8bit RGBA）
    void registerImage(int imageId, const Image& img);

    // ノードグラフ構造を設定
    void setNodes(const std::vector<GraphNode>& nodes);
    void setConnections(const std::vector<GraphConnection>& connections);

    // ノードグラフを評価して最終画像を取得（1回のWASM呼び出しで完結）
    Image evaluateGraph();

    // キャンバスサイズ変更
    void setCanvasSize(int width, int height);

    // 出力先原点（dstOrigin）を設定
    void setDstOrigin(double x, double y);

    // パフォーマンス計測結果を取得
    const PerfMetrics& getPerfMetrics() const { return perfMetrics; }

private:
    int canvasWidth;
    int canvasHeight;
    double dstOriginX;  // 出力先の基準点X（ピクセル座標）
    double dstOriginY;  // 出力先の基準点Y（ピクセル座標）
    ImageProcessor processor;

    std::vector<GraphNode> nodes;
    std::vector<GraphConnection> connections;

    // 画像ライブラリ（RGBA8_Straight形式のViewPortで保存）
    std::map<int, ViewPort> imageLibrary;

    // ノード評価結果キャッシュ（1回の評価で使い回す）
    std::map<std::string, ViewPort> nodeResultCache;

    // パフォーマンス計測
    PerfMetrics perfMetrics;

    // 内部評価関数（再帰的にノードを評価）
    ViewPort evaluateNode(const std::string& nodeId, std::set<std::string>& visited);
};

} // namespace ImageTransform

#endif // NODE_GRAPH_H
