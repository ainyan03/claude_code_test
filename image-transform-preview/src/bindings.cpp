#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <vector>
#include "node_graph.h"

using namespace emscripten;
using namespace ImageTransform;

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

    void setLayerImage(int layerId, const val& imageData, int width, int height) {
        unsigned int length = imageData["length"].as<unsigned int>();
        Image img(width, height);

        for (unsigned int i = 0; i < length; i++) {
            img.data[i] = imageData[i].as<uint8_t>();
        }

        evaluator.setLayerImage(layerId, img);
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
                // 新形式: imageId（画像ライブラリ対応）
                if (nodeObj["imageId"].typeOf().as<std::string>() != "undefined") {
                    node.imageId = nodeObj["imageId"].as<int>();
                }
                // 旧形式: layerId + params（後方互換性）
                else if (nodeObj["layerId"].typeOf().as<std::string>() != "undefined") {
                    node.layerId = nodeObj["layerId"].as<int>();

                    if (nodeObj["params"].typeOf().as<std::string>() != "undefined") {
                        val params = nodeObj["params"];
                        node.transform.translateX = params["translateX"].as<double>();
                        node.transform.translateY = params["translateY"].as<double>();
                        node.transform.rotation = params["rotation"].as<double>();
                        node.transform.scaleX = params["scaleX"].as<double>();
                        node.transform.scaleY = params["scaleY"].as<double>();
                    }
                }
            }

            // filter用パラメータ
            if (node.type == "filter") {
                if (nodeObj["independent"].typeOf().as<std::string>() != "undefined") {
                    node.independent = nodeObj["independent"].as<bool>();

                    if (node.independent) {
                        node.filterType = nodeObj["filterType"].as<std::string>();
                        node.filterParam = nodeObj["param"].as<float>();
                    }
                }
            }

            // composite用パラメータ
            if (node.type == "composite") {
                // 旧形式のalpha1, alpha2（後方互換性）
                if (nodeObj["alpha1"].typeOf().as<std::string>() != "undefined") {
                    node.alpha1 = nodeObj["alpha1"].as<double>();
                }
                if (nodeObj["alpha2"].typeOf().as<std::string>() != "undefined") {
                    node.alpha2 = nodeObj["alpha2"].as<double>();
                }

                // 新形式の動的入力配列
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

                if (nodeObj["affineParams"].typeOf().as<std::string>() != "undefined") {
                    val affine = nodeObj["affineParams"];
                    node.compositeTransform.translateX = affine["translateX"].as<double>();
                    node.compositeTransform.translateY = affine["translateY"].as<double>();
                    // JavaScriptから度数法で渡されるのでラジアンに変換
                    node.compositeTransform.rotation = affine["rotation"].as<double>() * M_PI / 180.0;
                    node.compositeTransform.scaleX = affine["scaleX"].as<double>();
                    node.compositeTransform.scaleY = affine["scaleY"].as<double>();
                }
            }

            // affine用パラメータ（アフィン変換ノード）
            if (node.type == "affine") {
                // モードフラグを取得
                if (nodeObj["matrixMode"].typeOf().as<std::string>() != "undefined") {
                    node.matrixMode = nodeObj["matrixMode"].as<bool>();
                }

                if (!node.matrixMode) {
                    // パラメータモード
                    node.affineParams.translateX = nodeObj["translateX"].typeOf().as<std::string>() != "undefined"
                        ? nodeObj["translateX"].as<double>() : 0.0;
                    node.affineParams.translateY = nodeObj["translateY"].typeOf().as<std::string>() != "undefined"
                        ? nodeObj["translateY"].as<double>() : 0.0;
                    node.affineParams.rotation = nodeObj["rotation"].typeOf().as<std::string>() != "undefined"
                        ? nodeObj["rotation"].as<double>() * M_PI / 180.0 : 0.0;  // 度→ラジアン変換
                    node.affineParams.scaleX = nodeObj["scaleX"].typeOf().as<std::string>() != "undefined"
                        ? nodeObj["scaleX"].as<double>() : 1.0;
                    node.affineParams.scaleY = nodeObj["scaleY"].typeOf().as<std::string>() != "undefined"
                        ? nodeObj["scaleY"].as<double>() : 1.0;
                } else {
                    // 行列モード
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

private:
    NodeGraphEvaluator evaluator;
};

EMSCRIPTEN_BINDINGS(image_transform) {
    class_<NodeGraphEvaluatorWrapper>("NodeGraphEvaluator")
        .constructor<int, int>()
        .function("setCanvasSize", &NodeGraphEvaluatorWrapper::setCanvasSize)
        .function("setLayerImage", &NodeGraphEvaluatorWrapper::setLayerImage)
        .function("setNodes", &NodeGraphEvaluatorWrapper::setNodes)
        .function("setConnections", &NodeGraphEvaluatorWrapper::setConnections)
        .function("evaluateGraph", &NodeGraphEvaluatorWrapper::evaluateGraph);
}
