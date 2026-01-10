// fleximg WASM Bindings
// 既存JSアプリとの後方互換性を維持しつつ、内部でv2 Node/Portモデルを使用

#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <vector>
#include <map>
#include <memory>
#include <string>

#include "../src/fleximg/nodes/source_node.h"
#include "../src/fleximg/nodes/sink_node.h"
#include "../src/fleximg/nodes/affine_node.h"
#include "../src/fleximg/nodes/filter_node_base.h"
#include "../src/fleximg/nodes/brightness_node.h"
#include "../src/fleximg/nodes/grayscale_node.h"
#include "../src/fleximg/nodes/box_blur_node.h"
#include "../src/fleximg/nodes/alpha_node.h"
#include "../src/fleximg/nodes/composite_node.h"
#include "../src/fleximg/nodes/renderer_node.h"

using namespace emscripten;
using namespace FLEXIMG_NAMESPACE;

// ========================================================================
// ImageStore - 入出力画像データの永続化管理
// ========================================================================

class ImageStore {
public:
    // 外部データをコピーして保存（入力画像用）
    ViewPort store(int id, const uint8_t* data, int w, int h, PixelFormatID fmt) {
        size_t bpp = getBytesPerPixel(fmt);
        size_t size = w * h * bpp;
        storage_[id].assign(data, data + size);
        return ViewPort(storage_[id].data(), fmt, w * bpp, w, h);
    }

    // バッファを確保（出力用）
    ViewPort allocate(int id, int w, int h, PixelFormatID fmt) {
        size_t bpp = getBytesPerPixel(fmt);
        size_t size = w * h * bpp;
        storage_[id].resize(size, 0);
        return ViewPort(storage_[id].data(), fmt, w * bpp, w, h);
    }

    // データ取得（JSへ返す用）
    const std::vector<uint8_t>& get(int id) const {
        static const std::vector<uint8_t> empty;
        auto it = storage_.find(id);
        return (it != storage_.end()) ? it->second : empty;
    }

    void release(int id) {
        storage_.erase(id);
    }

    void clear() {
        storage_.clear();
    }

    void zeroFill(int id) {
        auto it = storage_.find(id);
        if (it != storage_.end()) {
            std::fill(it->second.begin(), it->second.end(), 0);
        }
    }

private:
    std::map<int, std::vector<uint8_t>> storage_;
};

// ========================================================================
// GraphNode/GraphConnection 構造体（既存APIとの互換用）
// ========================================================================

struct GraphNode {
    std::string type;
    std::string id;
    int imageId = -1;
    double srcOriginX = 0;
    double srcOriginY = 0;
    std::string filterType;
    std::vector<float> filterParams;
    bool independent = false;
    struct { double a=1, b=0, c=0, d=1, tx=0, ty=0; } affineMatrix;
    std::vector<std::string> compositeInputIds;  // compositeノード用
};

struct GraphConnection {
    std::string fromNodeId;
    std::string fromPort;
    std::string toNodeId;
    std::string toPort;
};

// ========================================================================
// NodeGraphEvaluatorWrapper - 既存API互換ラッパー
// ========================================================================

class NodeGraphEvaluatorWrapper {
public:
    NodeGraphEvaluatorWrapper(int width, int height)
        : canvasWidth_(width), canvasHeight_(height) {}

    void setCanvasSize(int width, int height) {
        canvasWidth_ = width;
        canvasHeight_ = height;
    }

    void setDstOrigin(double x, double y) {
        dstOriginX_ = x;
        dstOriginY_ = y;
    }

    void setTileSize(int width, int height) {
        tileWidth_ = width;
        tileHeight_ = height;
    }

    void setDebugCheckerboard(bool enabled) {
        debugCheckerboard_ = enabled;
    }

    // 画像を登録（データをコピー）
    void storeImage(int id, const val& imageData, int width, int height) {
        unsigned int length = imageData["length"].as<unsigned int>();
        std::vector<uint8_t> tempData(length);

        for (unsigned int i = 0; i < length; i++) {
            tempData[i] = imageData[i].as<uint8_t>();
        }

        imageViews_[id] = imageStore_.store(id, tempData.data(), width, height,
                                            PixelFormatIDs::RGBA8_Straight);
    }

    // 画像バッファを確保
    void allocateImage(int id, int width, int height) {
        imageViews_[id] = imageStore_.allocate(id, width, height,
                                               PixelFormatIDs::RGBA8_Straight);
    }

    // 画像データを取得
    val getImage(int id) {
        const std::vector<uint8_t>& data = imageStore_.get(id);
        if (data.empty()) {
            return val::null();
        }
        return val::global("Uint8ClampedArray").new_(
            typed_memory_view(data.size(), data.data())
        );
    }

    // ノード設定（既存API互換）
    void setNodes(const val& nodesArray) {
        graphNodes_.clear();
        unsigned int nodeCount = nodesArray["length"].as<unsigned int>();

        for (unsigned int i = 0; i < nodeCount; i++) {
            val nodeObj = nodesArray[i];
            GraphNode node;

            node.type = nodeObj["type"].as<std::string>();
            node.id = nodeObj["id"].as<std::string>();

            // image用パラメータ
            if (node.type == "image") {
                if (nodeObj["imageId"].typeOf().as<std::string>() != "undefined") {
                    node.imageId = nodeObj["imageId"].as<int>();
                }
                if (nodeObj["originX"].typeOf().as<std::string>() != "undefined") {
                    node.srcOriginX = nodeObj["originX"].as<double>();
                }
                if (nodeObj["originY"].typeOf().as<std::string>() != "undefined") {
                    node.srcOriginY = nodeObj["originY"].as<double>();
                }
            }

            // filter用パラメータ
            if (node.type == "filter") {
                if (nodeObj["independent"].typeOf().as<std::string>() != "undefined") {
                    node.independent = nodeObj["independent"].as<bool>();
                    if (node.independent) {
                        node.filterType = nodeObj["filterType"].as<std::string>();
                        if (nodeObj["filterParams"].typeOf().as<std::string>() != "undefined") {
                            val paramsArray = nodeObj["filterParams"];
                            unsigned int paramCount = paramsArray["length"].as<unsigned int>();
                            for (unsigned int j = 0; j < paramCount; j++) {
                                node.filterParams.push_back(paramsArray[j].as<float>());
                            }
                        }
                    }
                }
            }

            // composite用パラメータ
            if (node.type == "composite") {
                if (nodeObj["inputs"].typeOf().as<std::string>() != "undefined") {
                    val inputsArray = nodeObj["inputs"];
                    unsigned int inputCount = inputsArray["length"].as<unsigned int>();
                    for (unsigned int j = 0; j < inputCount; j++) {
                        val inputObj = inputsArray[j];
                        node.compositeInputIds.push_back(inputObj["id"].as<std::string>());
                    }
                }
            }

            // affine用パラメータ
            if (node.type == "affine") {
                if (nodeObj["matrix"].typeOf().as<std::string>() != "undefined") {
                    val matrix = nodeObj["matrix"];
                    node.affineMatrix.a = matrix["a"].typeOf().as<std::string>() != "undefined"
                        ? matrix["a"].as<double>() : 1.0;
                    node.affineMatrix.b = matrix["b"].typeOf().as<std::string>() != "undefined"
                        ? matrix["b"].as<double>() : 0.0;
                    node.affineMatrix.c = matrix["c"].typeOf().as<std::string>() != "undefined"
                        ? matrix["c"].as<double>() : 0.0;
                    node.affineMatrix.d = matrix["d"].typeOf().as<std::string>() != "undefined"
                        ? matrix["d"].as<double>() : 1.0;
                    node.affineMatrix.tx = matrix["tx"].typeOf().as<std::string>() != "undefined"
                        ? matrix["tx"].as<double>() : 0.0;
                    node.affineMatrix.ty = matrix["ty"].typeOf().as<std::string>() != "undefined"
                        ? matrix["ty"].as<double>() : 0.0;
                }
            }

            // sink用パラメータ
            if (node.type == "sink") {
                if (nodeObj["imageId"].typeOf().as<std::string>() != "undefined") {
                    node.imageId = nodeObj["imageId"].as<int>();
                }
            }

            graphNodes_.push_back(node);
        }
    }

    // 接続設定（既存API互換）
    void setConnections(const val& connectionsArray) {
        graphConnections_.clear();
        unsigned int connCount = connectionsArray["length"].as<unsigned int>();

        for (unsigned int i = 0; i < connCount; i++) {
            val connObj = connectionsArray[i];
            GraphConnection conn;

            conn.fromNodeId = connObj["fromNodeId"].as<std::string>();
            conn.fromPort = connObj["fromPortId"].as<std::string>();
            conn.toNodeId = connObj["toNodeId"].as<std::string>();
            conn.toPort = connObj["toPortId"].as<std::string>();

            graphConnections_.push_back(conn);
        }
    }

    // グラフ評価
    // 戻り値: 0 = 成功、非0 = エラー（ExecResult値）
    int evaluateGraph() {
        // グラフからv2ノードを構築して実行
        return buildAndExecute();
    }

    void clearImage(int id) {
        imageStore_.zeroFill(id);
    }

    val getPerfMetrics() {
        val result = val::object();

        // ノードタイプ名（NodeType enum順）
        static const char* nodeNames[] = {
            "renderer", "source", "sink", "transform", "composite",
            "brightness", "grayscale", "boxBlur", "alpha"
        };

        // nodes配列を構築
        val nodes = val::array();
        for (int i = 0; i < NodeType::Count; i++) {
            val nodeMetrics = val::object();
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            nodeMetrics.set("time_us", lastPerfMetrics_.nodes[i].time_us);
            nodeMetrics.set("count", lastPerfMetrics_.nodes[i].count);
            nodeMetrics.set("requestedPixels", static_cast<double>(lastPerfMetrics_.nodes[i].requestedPixels));
            nodeMetrics.set("usedPixels", static_cast<double>(lastPerfMetrics_.nodes[i].usedPixels));
            nodeMetrics.set("wasteRatio", lastPerfMetrics_.nodes[i].wasteRatio());
            nodeMetrics.set("allocatedBytes", static_cast<double>(lastPerfMetrics_.nodes[i].allocatedBytes));
            nodeMetrics.set("allocCount", lastPerfMetrics_.nodes[i].allocCount);
            nodeMetrics.set("maxAllocBytes", static_cast<double>(lastPerfMetrics_.nodes[i].maxAllocBytes));
            nodeMetrics.set("maxAllocWidth", lastPerfMetrics_.nodes[i].maxAllocWidth);
            nodeMetrics.set("maxAllocHeight", lastPerfMetrics_.nodes[i].maxAllocHeight);
#else
            nodeMetrics.set("time_us", 0);
            nodeMetrics.set("count", 0);
            nodeMetrics.set("requestedPixels", 0.0);
            nodeMetrics.set("usedPixels", 0.0);
            nodeMetrics.set("wasteRatio", 0.0f);
            nodeMetrics.set("allocatedBytes", 0.0);
            nodeMetrics.set("allocCount", 0);
            nodeMetrics.set("maxAllocBytes", 0.0);
            nodeMetrics.set("maxAllocWidth", 0);
            nodeMetrics.set("maxAllocHeight", 0);
#endif
            nodes.call<void>("push", nodeMetrics);
        }
        result.set("nodes", nodes);

        // 後方互換用フラットキー（主要な時間とカウント）
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        // フィルタ4種の合計
        uint32_t filterTimeSum = lastPerfMetrics_.nodes[NodeType::Brightness].time_us
                               + lastPerfMetrics_.nodes[NodeType::Grayscale].time_us
                               + lastPerfMetrics_.nodes[NodeType::BoxBlur].time_us
                               + lastPerfMetrics_.nodes[NodeType::Alpha].time_us;
        int filterCountSum = lastPerfMetrics_.nodes[NodeType::Brightness].count
                           + lastPerfMetrics_.nodes[NodeType::Grayscale].count
                           + lastPerfMetrics_.nodes[NodeType::BoxBlur].count
                           + lastPerfMetrics_.nodes[NodeType::Alpha].count;
        result.set("filterTime", filterTimeSum);
        result.set("affineTime", lastPerfMetrics_.nodes[NodeType::Affine].time_us);
        result.set("compositeTime", lastPerfMetrics_.nodes[NodeType::Composite].time_us);
        result.set("outputTime", lastPerfMetrics_.nodes[NodeType::Renderer].time_us);
        result.set("filterCount", filterCountSum);
        result.set("affineCount", lastPerfMetrics_.nodes[NodeType::Affine].count);
        result.set("compositeCount", lastPerfMetrics_.nodes[NodeType::Composite].count);
        result.set("outputCount", lastPerfMetrics_.nodes[NodeType::Renderer].count);
        result.set("totalTime", lastPerfMetrics_.totalTime());
        // グローバルメモリ統計
        result.set("totalAllocBytes", static_cast<double>(lastPerfMetrics_.totalAllocatedBytes));
        result.set("peakMemoryBytes", static_cast<double>(lastPerfMetrics_.peakMemoryBytes));
        result.set("nodeAllocBytes", static_cast<double>(lastPerfMetrics_.totalNodeAllocatedBytes()));
        // 最大確保サイズ
        result.set("maxAllocBytes", static_cast<double>(lastPerfMetrics_.maxAllocBytes));
        result.set("maxAllocWidth", lastPerfMetrics_.maxAllocWidth);
        result.set("maxAllocHeight", lastPerfMetrics_.maxAllocHeight);
#else
        result.set("filterTime", 0);
        result.set("affineTime", 0);
        result.set("compositeTime", 0);
        result.set("outputTime", 0);
        result.set("filterCount", 0);
        result.set("affineCount", 0);
        result.set("compositeCount", 0);
        result.set("outputCount", 0);
        result.set("totalTime", 0);
        result.set("totalAllocBytes", 0);
        result.set("peakMemoryBytes", 0);
        result.set("nodeAllocBytes", 0);
        result.set("maxAllocBytes", 0);
        result.set("maxAllocWidth", 0);
        result.set("maxAllocHeight", 0);
#endif

        return result;
    }

private:
    int canvasWidth_, canvasHeight_;
    double dstOriginX_ = 0, dstOriginY_ = 0;
    int tileWidth_ = 0, tileHeight_ = 0;
    bool debugCheckerboard_ = false;

    ImageStore imageStore_;
    std::map<int, ViewPort> imageViews_;
    std::vector<GraphNode> graphNodes_;
    std::vector<GraphConnection> graphConnections_;
    PerfMetrics lastPerfMetrics_;

    // グラフを解析してv2ノードを構築・実行
    // 戻り値: 0 = 成功、非0 = エラー（ExecResult値）
    int buildAndExecute() {
        // ノードIDからGraphNodeへのマップ
        std::map<std::string, const GraphNode*> nodeMap;
        for (const auto& node : graphNodes_) {
            nodeMap[node.id] = &node;
        }

        // 接続情報からtoNodeId -> fromNodeIdのマップを構築
        std::map<std::string, std::vector<std::string>> inputConnections;
        for (const auto& conn : graphConnections_) {
            inputConnections[conn.toNodeId].push_back(conn.fromNodeId);
        }

        // 接続情報からfromNodeId -> toNodeIdのマップを構築（下流探索用）
        std::map<std::string, std::vector<std::string>> outputConnections;
        for (const auto& conn : graphConnections_) {
            outputConnections[conn.fromNodeId].push_back(conn.toNodeId);
        }

        // sinkノードを探す
        const GraphNode* sinkGraphNode = nullptr;
        for (const auto& node : graphNodes_) {
            if (node.type == "sink") {
                sinkGraphNode = &node;
                break;
            }
        }

        if (!sinkGraphNode || sinkGraphNode->imageId < 0) {
            return static_cast<int>(ExecResult::Success);  // 描画対象なし
        }

        // 出力先ViewPortを取得
        auto outputIt = imageViews_.find(sinkGraphNode->imageId);
        if (outputIt == imageViews_.end()) {
            return static_cast<int>(ExecResult::Success);  // 出力先なし
        }
        ViewPort outputView = outputIt->second;

        // 出力バッファをクリア（以前の描画結果を消去）
        view_ops::clear(outputView, 0, 0, outputView.width, outputView.height);

        // v2ノードを一時的に保持するコンテナ
        std::map<std::string, std::unique_ptr<Node>> v2Nodes;
        std::map<std::string, std::unique_ptr<SourceNode>> sourceNodes;

        // RendererNodeを作成
        auto rendererNode = std::make_unique<RendererNode>();
        rendererNode->setVirtualScreenf(canvasWidth_, canvasHeight_,
                                        static_cast<float>(dstOriginX_),
                                        static_cast<float>(dstOriginY_));

        // SinkNodeを作成
        auto sinkNode = std::make_unique<SinkNode>();
        sinkNode->setTarget(outputView);
        sinkNode->setOriginf(static_cast<float>(dstOriginX_),
                             static_cast<float>(dstOriginY_));

        // 再帰的にノードを構築
        std::function<Node*(const std::string&)> buildNode;
        buildNode = [&](const std::string& nodeId) -> Node* {
            auto nodeIt = nodeMap.find(nodeId);
            if (nodeIt == nodeMap.end()) return nullptr;
            const GraphNode& gnode = *nodeIt->second;

            // 既に構築済みならそれを返す
            auto v2It = v2Nodes.find(nodeId);
            if (v2It != v2Nodes.end()) {
                return v2It->second.get();
            }
            auto srcIt = sourceNodes.find(nodeId);
            if (srcIt != sourceNodes.end()) {
                return srcIt->second.get();
            }

            // ノードタイプに応じて構築
            if (gnode.type == "image") {
                auto viewIt = imageViews_.find(gnode.imageId);
                if (viewIt == imageViews_.end()) return nullptr;

                auto src = std::make_unique<SourceNode>();
                src->setSource(viewIt->second);
                src->setOriginf(static_cast<float>(gnode.srcOriginX),
                                static_cast<float>(gnode.srcOriginY));

                Node* result = src.get();
                sourceNodes[nodeId] = std::move(src);
                return result;
            }
            else if (gnode.type == "filter" && gnode.independent) {
                std::unique_ptr<Node> filterNode;

                if (gnode.filterType == "brightness" && !gnode.filterParams.empty()) {
                    auto node = std::make_unique<BrightnessNode>();
                    node->setAmount(gnode.filterParams[0]);
                    filterNode = std::move(node);
                }
                else if (gnode.filterType == "grayscale") {
                    filterNode = std::make_unique<GrayscaleNode>();
                }
                else if ((gnode.filterType == "blur" || gnode.filterType == "boxBlur") && !gnode.filterParams.empty()) {
                    auto node = std::make_unique<BoxBlurNode>();
                    node->setRadius(static_cast<int>(gnode.filterParams[0]));
                    filterNode = std::move(node);
                }
                else if (gnode.filterType == "alpha" && !gnode.filterParams.empty()) {
                    auto node = std::make_unique<AlphaNode>();
                    node->setScale(gnode.filterParams[0]);
                    filterNode = std::move(node);
                }

                if (filterNode) {
                    // 入力を接続
                    auto connIt = inputConnections.find(nodeId);
                    if (connIt != inputConnections.end() && !connIt->second.empty()) {
                        Node* upstream = buildNode(connIt->second[0]);
                        if (upstream) {
                            upstream->connectTo(*filterNode);
                        }
                    }

                    Node* result = filterNode.get();
                    v2Nodes[nodeId] = std::move(filterNode);
                    return result;
                }
                return nullptr;
            }
            else if (gnode.type == "affine") {
                auto affineNode = std::make_unique<AffineNode>();

                AffineMatrix mat;
                mat.a = static_cast<float>(gnode.affineMatrix.a);
                mat.b = static_cast<float>(gnode.affineMatrix.b);
                mat.c = static_cast<float>(gnode.affineMatrix.c);
                mat.d = static_cast<float>(gnode.affineMatrix.d);
                mat.tx = static_cast<float>(gnode.affineMatrix.tx);
                mat.ty = static_cast<float>(gnode.affineMatrix.ty);
                affineNode->setMatrix(mat);

                // 入力を接続
                auto connIt = inputConnections.find(nodeId);
                if (connIt != inputConnections.end() && !connIt->second.empty()) {
                    Node* upstream = buildNode(connIt->second[0]);
                    if (upstream) {
                        upstream->connectTo(*affineNode);
                    }
                }

                Node* result = affineNode.get();
                v2Nodes[nodeId] = std::move(affineNode);
                return result;
            }
            else if (gnode.type == "composite") {
                int inputCount = static_cast<int>(gnode.compositeInputIds.size());
                if (inputCount < 2) inputCount = 2;

                auto compositeNode = std::make_unique<CompositeNode>(inputCount);

                // 各入力を接続
                auto connIt = inputConnections.find(nodeId);
                if (connIt != inputConnections.end()) {
                    int portIndex = 0;
                    for (const auto& inputId : connIt->second) {
                        Node* upstream = buildNode(inputId);
                        if (upstream && portIndex < inputCount) {
                            upstream->connectTo(*compositeNode, portIndex);
                            portIndex++;
                        }
                    }
                }

                Node* result = compositeNode.get();
                v2Nodes[nodeId] = std::move(compositeNode);
                return result;
            }
            else if (gnode.type == "filter" && !gnode.independent) {
                // パススルーフィルタ（独立でない場合）
                auto connIt = inputConnections.find(nodeId);
                if (connIt != inputConnections.end() && !connIt->second.empty()) {
                    return buildNode(connIt->second[0]);
                }
            }

            return nullptr;
        };

        // Renderer下流のチェーンを再帰的に構築する関数
        std::function<Node*(const std::string&)> buildDownstreamChain;
        buildDownstreamChain = [&](const std::string& nodeId) -> Node* {
            // sinkノードならそれを返す
            if (nodeId == "sink" || nodeId == sinkGraphNode->id) {
                return sinkNode.get();
            }

            auto nodeIt = nodeMap.find(nodeId);
            if (nodeIt == nodeMap.end()) return nullptr;
            const GraphNode& gnode = *nodeIt->second;

            // 既に構築済みならそれを返す
            auto v2It = v2Nodes.find(nodeId);
            if (v2It != v2Nodes.end()) {
                return v2It->second.get();
            }

            // ノードタイプに応じて構築
            std::unique_ptr<Node> newNode;

            if (gnode.type == "filter" && gnode.independent) {
                if (gnode.filterType == "brightness" && !gnode.filterParams.empty()) {
                    auto node = std::make_unique<BrightnessNode>();
                    node->setAmount(gnode.filterParams[0]);
                    newNode = std::move(node);
                }
                else if (gnode.filterType == "grayscale") {
                    newNode = std::make_unique<GrayscaleNode>();
                }
                else if ((gnode.filterType == "blur" || gnode.filterType == "boxBlur") && !gnode.filterParams.empty()) {
                    auto node = std::make_unique<BoxBlurNode>();
                    node->setRadius(static_cast<int>(gnode.filterParams[0]));
                    newNode = std::move(node);
                }
                else if (gnode.filterType == "alpha" && !gnode.filterParams.empty()) {
                    auto node = std::make_unique<AlphaNode>();
                    node->setScale(gnode.filterParams[0]);
                    newNode = std::move(node);
                }
            }
            else if (gnode.type == "affine") {
                auto affineNode = std::make_unique<AffineNode>();
                AffineMatrix mat;
                mat.a = static_cast<float>(gnode.affineMatrix.a);
                mat.b = static_cast<float>(gnode.affineMatrix.b);
                mat.c = static_cast<float>(gnode.affineMatrix.c);
                mat.d = static_cast<float>(gnode.affineMatrix.d);
                mat.tx = static_cast<float>(gnode.affineMatrix.tx);
                mat.ty = static_cast<float>(gnode.affineMatrix.ty);
                affineNode->setMatrix(mat);
                newNode = std::move(affineNode);
            }

            if (!newNode) return nullptr;

            // 次の下流を探して接続
            auto outputIt = outputConnections.find(nodeId);
            if (outputIt != outputConnections.end() && !outputIt->second.empty()) {
                Node* downstream = buildDownstreamChain(outputIt->second[0]);
                if (downstream) {
                    newNode->connectTo(*downstream);
                }
            }

            Node* result = newNode.get();
            v2Nodes[nodeId] = std::move(newNode);
            return result;
        };

        // renderer ノードの入力を探す（JSグラフ: upstream → renderer → ... → sink）
        std::string rendererInputId;
        auto rendererConnIt = inputConnections.find("renderer");
        if (rendererConnIt != inputConnections.end() && !rendererConnIt->second.empty()) {
            rendererInputId = rendererConnIt->second[0];
        } else {
            // 旧形式: sinkの入力が直接upstreamの場合
            auto sinkConnIt = inputConnections.find(sinkGraphNode->id);
            if (sinkConnIt != inputConnections.end() && !sinkConnIt->second.empty()) {
                const std::string& inputId = sinkConnIt->second[0];
                if (inputId != "renderer") {
                    rendererInputId = inputId;
                }
            }
        }

        if (!rendererInputId.empty()) {
            Node* upstream = buildNode(rendererInputId);
            if (upstream) {
                upstream->connectTo(*rendererNode);

                // Renderer下流のチェーンを構築
                auto rendererOutputIt = outputConnections.find("renderer");
                if (rendererOutputIt != outputConnections.end() && !rendererOutputIt->second.empty()) {
                    Node* downstream = buildDownstreamChain(rendererOutputIt->second[0]);
                    if (downstream) {
                        rendererNode->connectTo(*downstream);
                    }
                } else {
                    // フォールバック: 直接Sinkに接続
                    rendererNode->connectTo(*sinkNode);
                }
            }
        }

        // RendererNodeで実行
        // タイル分割設定（幅0の場合はキャンバス幅を使用=スキャンライン）
        int effectiveTileW = (tileWidth_ > 0) ? tileWidth_ : canvasWidth_;
        int effectiveTileH = (tileHeight_ > 0) ? tileHeight_ : canvasHeight_;
        if (tileWidth_ > 0 || tileHeight_ > 0) {
            rendererNode->setTileConfig(effectiveTileW, effectiveTileH);
        }
        rendererNode->setDebugCheckerboard(debugCheckerboard_);
        ExecResult result = rendererNode->exec();

        // パフォーマンスメトリクスを保存
        lastPerfMetrics_ = rendererNode->getPerfMetrics();

        return static_cast<int>(result);
    }
};

// ========================================================================
// EMSCRIPTEN バインディング
// ========================================================================

EMSCRIPTEN_BINDINGS(image_transform) {
    class_<NodeGraphEvaluatorWrapper>("NodeGraphEvaluator")
        .constructor<int, int>()
        .function("setCanvasSize", &NodeGraphEvaluatorWrapper::setCanvasSize)
        .function("setDstOrigin", &NodeGraphEvaluatorWrapper::setDstOrigin)
        .function("setTileSize", &NodeGraphEvaluatorWrapper::setTileSize)
        .function("setDebugCheckerboard", &NodeGraphEvaluatorWrapper::setDebugCheckerboard)
        .function("storeImage", &NodeGraphEvaluatorWrapper::storeImage)
        .function("allocateImage", &NodeGraphEvaluatorWrapper::allocateImage)
        .function("getImage", &NodeGraphEvaluatorWrapper::getImage)
        .function("setNodes", &NodeGraphEvaluatorWrapper::setNodes)
        .function("setConnections", &NodeGraphEvaluatorWrapper::setConnections)
        .function("evaluateGraph", &NodeGraphEvaluatorWrapper::evaluateGraph)
        .function("clearImage", &NodeGraphEvaluatorWrapper::clearImage)
        .function("getPerfMetrics", &NodeGraphEvaluatorWrapper::getPerfMetrics);
}
