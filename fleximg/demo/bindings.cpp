#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <vector>
#include <map>
#include "../src/fleximg/node_graph.h"

using namespace emscripten;
using namespace FLEXIMG_NAMESPACE;

// ========================================================================
// ImageStore - 入出力画像データの永続化管理クラス
// ========================================================================

class ImageStore {
public:
    // 外部データをコピーして保存（入力画像用）
    ViewPort store(int id, const uint8_t* data, int w, int h, PixelFormatID fmt) {
        size_t bytesPerPixel = getBytesPerPixelForFormat(fmt);
        size_t size = w * h * bytesPerPixel;
        storage_[id].assign(data, data + size);
        return ViewPort(storage_[id].data(), fmt, w * bytesPerPixel, w, h);
    }

    // バッファを確保（出力用、または空の入力用）
    ViewPort allocate(int id, int w, int h, PixelFormatID fmt) {
        size_t bytesPerPixel = getBytesPerPixelForFormat(fmt);
        size_t size = w * h * bytesPerPixel;
        storage_[id].resize(size, 0);
        return ViewPort(storage_[id].data(), fmt, w * bytesPerPixel, w, h);
    }

private:
    // フォーマットからbytesPerPixelを取得するヘルパー
    static size_t getBytesPerPixelForFormat(PixelFormatID fmt) {
        const PixelFormatDescriptor* desc = PixelFormatRegistry::getInstance().getFormat(fmt);
        if (!desc) return 4;  // デフォルトは4バイト（RGBA8）
        if (desc->pixelsPerUnit > 1) {
            return (desc->bytesPerUnit + desc->pixelsPerUnit - 1) / desc->pixelsPerUnit;
        }
        return (desc->bitsPerPixel + 7) / 8;
    }

public:

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

    // 指定IDのバッファをゼロクリア
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
// NodeGraphEvaluatorのラッパークラス
// ========================================================================

class NodeGraphEvaluatorWrapper {
public:
    NodeGraphEvaluatorWrapper(int width, int height)
        : evaluator(width, height) {}

    void setCanvasSize(int width, int height) {
        evaluator.setCanvasSize(width, height);
    }

    void setDstOrigin(double x, double y) {
        evaluator.setDstOrigin(x, y);
    }

    // タイル分割サイズを設定（0 = 分割なし）
    void setTileSize(int width, int height) {
        evaluator.setTileSize(width, height);
    }

    // デバッグ用市松模様スキップを設定
    void setDebugCheckerboard(bool enabled) {
        evaluator.setDebugCheckerboard(enabled);
    }

    // 画像を登録（データをコピー、RGBA8_Straight形式）
    void storeImage(int id, const val& imageData, int width, int height) {
        unsigned int length = imageData["length"].as<unsigned int>();
        std::vector<uint8_t> tempData(length);

        for (unsigned int i = 0; i < length; i++) {
            tempData[i] = imageData[i].as<uint8_t>();
        }

        ViewPort view = imageStore.store(id, tempData.data(), width, height, PixelFormatIDs::RGBA8_Straight);
        evaluator.registerImage(id, view);
    }

    // 画像バッファを確保（RGBA8_Straight形式）
    void allocateImage(int id, int width, int height) {
        ViewPort view = imageStore.allocate(id, width, height, PixelFormatIDs::RGBA8_Straight);
        evaluator.registerImage(id, view);
    }

    // 画像データを取得
    val getImage(int id) {
        const std::vector<uint8_t>& data = imageStore.get(id);
        if (data.empty()) {
            return val::null();
        }
        return val::global("Uint8ClampedArray").new_(
            typed_memory_view(data.size(), data.data())
        );
    }

    void setNodes(const val& nodesArray) {
        unsigned int nodeCount = nodesArray["length"].as<unsigned int>();
        std::vector<GraphNode> nodes;

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
                // 原点情報（正規化座標 → ピクセル座標への変換は evaluateNode で行う）
                // ここでは正規化座標のまま受け取る
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

                        // filterParamsを配列として受け取る
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
                // 動的入力配列
                if (nodeObj["inputs"].typeOf().as<std::string>() != "undefined") {
                    val inputsArray = nodeObj["inputs"];
                    unsigned int inputCount = inputsArray["length"].as<unsigned int>();
                    for (unsigned int j = 0; j < inputCount; j++) {
                        val inputObj = inputsArray[j];
                        CompositeInput input;
                        input.id = inputObj["id"].as<std::string>();
                        node.compositeInputs.push_back(input);
                    }
                }
            }

            // affine用パラメータ（アフィン変換ノード）
            // JS側で行列に統一されているため、常に行列モードとして処理
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

            // output用パラメータ（imageIdで出力先を指定）
            if (node.type == "output") {
                if (nodeObj["imageId"].typeOf().as<std::string>() != "undefined") {
                    node.imageId = nodeObj["imageId"].as<int>();
                }
            }

            nodes.push_back(node);
        }

        evaluator.setNodes(nodes);
    }

    void setConnections(const val& connectionsArray) {
        unsigned int connCount = connectionsArray["length"].as<unsigned int>();
        std::vector<GraphConnection> connections;

        for (unsigned int i = 0; i < connCount; i++) {
            val connObj = connectionsArray[i];
            GraphConnection conn;

            conn.fromNodeId = connObj["fromNodeId"].as<std::string>();
            conn.fromPort = connObj["fromPortId"].as<std::string>();
            conn.toNodeId = connObj["toNodeId"].as<std::string>();
            conn.toPort = connObj["toPortId"].as<std::string>();

            connections.push_back(conn);
        }

        evaluator.setConnections(connections);
    }

    void evaluateGraph() {
        evaluator.evaluateGraph();
    }

    // 画像バッファをクリア（市松模様などで以前の画像が残らないように）
    void clearImage(int id) {
        imageStore.zeroFill(id);
    }

    val getPerfMetrics() {
        val result = val::object();

        const PerfMetrics& metrics = evaluator.getPerfMetrics();

        // ノードタイプ名
        static const char* nodeNames[] = {
            "image", "filter", "affine", "composite", "output"
        };

        // nodes配列を構築
        val nodes = val::array();
        for (int i = 0; i < NodeType::Count; i++) {
            val nodeMetrics = val::object();
            nodeMetrics.set("time_us", metrics.nodes[i].time_us);
            nodeMetrics.set("count", metrics.nodes[i].count);
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            nodeMetrics.set("allocBytes", static_cast<double>(metrics.nodes[i].allocBytes));
            nodeMetrics.set("allocCount", metrics.nodes[i].allocCount);
            nodeMetrics.set("requestedPixels", static_cast<double>(metrics.nodes[i].requestedPixels));
            nodeMetrics.set("usedPixels", static_cast<double>(metrics.nodes[i].usedPixels));
            nodeMetrics.set("wasteRatio", metrics.nodes[i].wasteRatio());
#else
            nodeMetrics.set("allocBytes", 0);
            nodeMetrics.set("allocCount", 0);
            nodeMetrics.set("requestedPixels", 0);
            nodeMetrics.set("usedPixels", 0);
            nodeMetrics.set("wasteRatio", 0.0f);
#endif
            nodes.call<void>("push", nodeMetrics);
        }
        result.set("nodes", nodes);

        // 後方互換用フラットキー（主要な時間とカウント）
        result.set("filterTime", metrics.nodes[NodeType::Filter].time_us);
        result.set("affineTime", metrics.nodes[NodeType::Affine].time_us);
        result.set("compositeTime", metrics.nodes[NodeType::Composite].time_us);
        result.set("outputTime", metrics.nodes[NodeType::Output].time_us);
        result.set("filterCount", metrics.nodes[NodeType::Filter].count);
        result.set("affineCount", metrics.nodes[NodeType::Affine].count);
        result.set("compositeCount", metrics.nodes[NodeType::Composite].count);
        result.set("outputCount", metrics.nodes[NodeType::Output].count);

        // 集計値
        result.set("totalTime", metrics.totalTime());
        result.set("totalAllocBytes", static_cast<double>(metrics.totalAllocBytes()));

        return result;
    }

private:
    NodeGraphEvaluator evaluator;
    ImageStore imageStore;
};

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
