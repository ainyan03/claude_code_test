#ifndef NODE_GRAPH_H
#define NODE_GRAPH_H

#include "viewport.h"
#include "image_processor.h"
#include <string>
#include <vector>
#include <map>
#include <set>

namespace ImageTransform {

// 前方宣言（後方互換性）
struct Image;
struct AffineParams;

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

    // image用（新形式: imageId + alpha）
    int imageId;       // 画像ライブラリのID
    double imageAlpha; // 画像ノードのアルファ値

    // image用（旧形式: 後方互換性のため保持）
    int layerId;
    AffineParams transform;

    // filter用（独立フィルタノード）
    std::string filterType;
    float filterParam;
    bool independent;

    // filter用（レイヤー付帯フィルタノード）
    int filterLayerId;
    int filterIndex;

    // composite用
    double alpha1;  // 後方互換性のため保持（非推奨）
    double alpha2;  // 後方互換性のため保持（非推奨）
    std::vector<CompositeInput> compositeInputs;  // 動的な入力配列
    AffineParams compositeTransform;

    // affine用（アフィン変換ノード）
    bool matrixMode;           // false: パラメータモード, true: 行列モード
    AffineParams affineParams; // パラメータモード用
    AffineMatrix affineMatrix; // 行列モード用

    GraphNode() : imageId(-1), imageAlpha(1.0),
                  layerId(-1), filterParam(0.0f), independent(false),
                  filterLayerId(-1), filterIndex(-1), alpha1(1.0), alpha2(1.0),
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

    // レイヤー画像を登録（8bit RGBA）
    void setLayerImage(int layerId, const Image& img);

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

    // レイヤー画像キャッシュ
    std::map<int, Image> layerImages;         // 元画像（8bit）
    std::map<int, ViewPort> layerPremulCache;  // premultiplied変換済み（ViewPort）

    // ノード評価結果キャッシュ（1回の評価で使い回す）
    std::map<std::string, ViewPort> nodeResultCache;

    // 内部評価関数（再帰的にノードを評価）
    ViewPort evaluateNode(const std::string& nodeId, std::set<std::string>& visited);

    // レイヤー画像のpremultiplied変換（キャッシュ付き）
    ViewPort getLayerPremultiplied(int layerId, const AffineParams& transform);
};

} // namespace ImageTransform

#endif // NODE_GRAPH_H
