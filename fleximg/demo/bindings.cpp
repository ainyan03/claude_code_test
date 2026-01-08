#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <vector>
#include "../src/fleximg/node_graph.h"

using namespace emscripten;
using namespace FLEXIMG_NAMESPACE;

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

    // タイル分割戦略を設定
    // strategy: 0=None, 1=Scanline, 2=Tile64, 3=Custom
    void setTileStrategy(int strategy, int tileWidth, int tileHeight) {
        TileStrategy ts = static_cast<TileStrategy>(strategy);
        evaluator.setTileStrategy(ts, tileWidth, tileHeight);
    }

    void registerImage(int imageId, const val& imageData, int width, int height) {
        unsigned int length = imageData["length"].as<unsigned int>();
        Image img(width, height);

        for (unsigned int i = 0; i < length; i++) {
            img.data[i] = imageData[i].as<uint8_t>();
        }

        evaluator.registerImage(imageId, img);
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
                        input.alpha = inputObj["alpha"].as<double>();
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

    val evaluateGraph() {
        Image result = evaluator.evaluateGraph();

        val uint8Array = val::global("Uint8ClampedArray").new_(
            typed_memory_view(result.data.size(), result.data.data())
        );

        val resultObj = val::object();
        resultObj.set("data", uint8Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    val getPerfMetrics() {
        val result = val::object();

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        const PerfMetrics& metrics = evaluator.getPerfMetrics();

        // インデックス名とキー名のマッピング
        static const char* timeKeys[] = {
            "filterTime", "affineTime", "compositeTime", "convertTime", "outputTime"
        };
        static const char* countKeys[] = {
            "filterCount", "affineCount", "compositeCount", "convertCount", "outputCount"
        };

        for (int i = 0; i < PerfMetricIndex::Count; i++) {
            result.set(timeKeys[i], metrics.times[i]);
            result.set(countKeys[i], metrics.counts[i]);
        }
#else
        // リリースビルド: ダミー値を返す（時間はマイクロ秒）
        result.set("filterTime", 0);
        result.set("affineTime", 0);
        result.set("compositeTime", 0);
        result.set("convertTime", 0);
        result.set("outputTime", 0);
        result.set("filterCount", 0);
        result.set("affineCount", 0);
        result.set("compositeCount", 0);
        result.set("convertCount", 0);
        result.set("outputCount", 0);
#endif

        return result;
    }

private:
    NodeGraphEvaluator evaluator;
};

EMSCRIPTEN_BINDINGS(image_transform) {
    class_<NodeGraphEvaluatorWrapper>("NodeGraphEvaluator")
        .constructor<int, int>()
        .function("setCanvasSize", &NodeGraphEvaluatorWrapper::setCanvasSize)
        .function("setDstOrigin", &NodeGraphEvaluatorWrapper::setDstOrigin)
        .function("setTileStrategy", &NodeGraphEvaluatorWrapper::setTileStrategy)
        .function("registerImage", &NodeGraphEvaluatorWrapper::registerImage)
        .function("setNodes", &NodeGraphEvaluatorWrapper::setNodes)
        .function("setConnections", &NodeGraphEvaluatorWrapper::setConnections)
        .function("evaluateGraph", &NodeGraphEvaluatorWrapper::evaluateGraph)
        .function("getPerfMetrics", &NodeGraphEvaluatorWrapper::getPerfMetrics);
}
