#ifndef NODE_GRAPH_H
#define NODE_GRAPH_H

#include "viewport.h"
#include "image_processor.h"
#include "image_types.h"
#include <string>
#include <vector>
#include <map>
#include <set>

namespace ImageTransform {

// 前方宣言
struct Image;

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
    AffineParams compositeTransform;

    // affine用（アフィン変換ノード）
    bool matrixMode;           // false: パラメータモード, true: 行列モード
    AffineParams affineParams; // パラメータモード用
    AffineMatrix affineMatrix; // 行列モード用

    GraphNode() : imageId(-1), srcOriginX(0.0), srcOriginY(0.0),
                  filterParam(0.0f), independent(false),
                  matrixMode(false) {}
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

private:
    int canvasWidth;
    int canvasHeight;
    ImageProcessor processor;

    std::vector<GraphNode> nodes;
    std::vector<GraphConnection> connections;

    // 画像ライブラリ（RGBA8_Straight形式のViewPortで保存）
    std::map<int, ViewPort> imageLibrary;

    // ノード評価結果キャッシュ（1回の評価で使い回す）
    std::map<std::string, ViewPort> nodeResultCache;

    // 内部評価関数（再帰的にノードを評価）
    ViewPort evaluateNode(const std::string& nodeId, std::set<std::string>& visited);
};

} // namespace ImageTransform

#endif // NODE_GRAPH_H
