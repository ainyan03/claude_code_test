#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <vector>
#include "image_processor.h"
#include "node_graph.h"
#include "viewport.h"
#include "pixel_format.h"

using namespace emscripten;
using namespace ImageTransform;

// ========================================================================
// ViewPort ↔ JavaScript 変換ヘルパー関数
// ========================================================================

// JavaScript画像オブジェクト（Uint8Array）→ ViewPort（RGBA8_Straight）
ViewPort viewPortFromJSImage(const val& imageObj) {
    val inputData = imageObj["data"];
    int width = imageObj["width"].as<int>();
    int height = imageObj["height"].as<int>();

    // 入力検証: サイズの妥当性チェック
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        // 無効なサイズの場合は1x1の空ViewPortを返す
        return ViewPort(1, 1, PixelFormatIDs::RGBA8_Straight);
    }

    // オーバーフロー防止のためsize_tで計算
    size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

    // JavaScriptからデータを一時バッファにコピー
    std::vector<uint8_t> buffer(expectedSize);
    unsigned int length = inputData["length"].as<unsigned int>();

    // 境界チェック: コピー量を制限
    size_t copyLength = std::min(static_cast<size_t>(length), expectedSize);
    for (size_t i = 0; i < copyLength; i++) {
        buffer[i] = inputData[static_cast<unsigned int>(i)].as<uint8_t>();
    }

    // 外部データからViewPortを構築
    return ViewPort::fromExternalData(buffer.data(), width, height,
                                       PixelFormatIDs::RGBA8_Straight);
}

// ViewPort → JavaScript画像オブジェクト（Uint8ClampedArray）
val viewPortToJSImage(const ViewPort& vp) {
    // RGBA8形式に変換が必要な場合を考慮
    ViewPort output = vp;
    if (vp.formatID != PixelFormatIDs::RGBA8_Straight &&
        vp.formatID != PixelFormatIDs::RGBA8_Premultiplied) {
        // TODO: PixelFormatRegistryを使用した変換
        // とりあえず、RGBA8系を仮定
    }

    // strideを考慮して行ごとにコピー
    std::vector<uint8_t> buffer(output.width * output.height * 4);
    for (int y = 0; y < output.height; y++) {
        const uint8_t* srcRow = output.getPixelPtr<uint8_t>(0, y);
        uint8_t* dstRow = &buffer[y * output.width * 4];
        std::memcpy(dstRow, srcRow, output.width * 4);
    }

    val uint8Array = val::global("Uint8ClampedArray").new_(
        typed_memory_view(buffer.size(), buffer.data())
    );

    val resultObj = val::object();
    resultObj.set("data", uint8Array);
    resultObj.set("width", output.width);
    resultObj.set("height", output.height);

    return resultObj;
}

// ========================================================================
// JavaScriptからアクセスしやすいラッパークラス
// ========================================================================
class ImageProcessorWrapper {
public:
    ImageProcessorWrapper(int width, int height)
        : processor(width, height) {}

    // ノードグラフ用の単体処理関数（ViewPort直接使用）
    val applyFilterToImage(const val& inputImageObj, const std::string& filterType, float param) {
        // JavaScript → ViewPort (RGBA8_Straight)
        ViewPort inputVP = viewPortFromJSImage(inputImageObj);

        // RGBA8 → RGBA16形式変換してフィルタ処理
        ViewPort inputVP16 = processor.convertPixelFormat(inputVP, PixelFormatIDs::RGBA16_Straight);
        ViewPort resultVP16 = processor.applyFilter(inputVP16, filterType, param);
        ViewPort resultVP = processor.convertPixelFormat(resultVP16, PixelFormatIDs::RGBA8_Straight);

        // ViewPort → JavaScript
        return viewPortToJSImage(resultVP);
    }

    val applyTransformToImage(const val& inputImageObj, double tx, double ty,
                             double rotation, double scaleX, double scaleY, double alpha) {
        // JavaScript → ViewPort (RGBA8_Straight)
        ViewPort inputVP = viewPortFromJSImage(inputImageObj);
        int width = inputVP.width;
        int height = inputVP.height;

        // 変換パラメータ設定（alpha除外）
        AffineParams params;
        params.translateX = tx;
        params.translateY = ty;
        params.rotation = rotation;
        params.scaleX = scaleX;
        params.scaleY = scaleY;

        // RGBA8 → RGBA16 Premultiplied変換
        ViewPort inputVP16 = processor.convertPixelFormat(inputVP, PixelFormatIDs::RGBA16_Premultiplied);

        // アフィン変換を適用
        double centerX = width / 2.0;
        double centerY = height / 2.0;
        AffineMatrix matrix = AffineMatrix::fromParams(params, centerX, centerY);
        ViewPort transformed = processor.applyTransform(inputVP16, matrix);

        // アルファ調整を適用（alphaフィルタ使用）
        ViewPort resultVP16 = processor.applyFilter(transformed, "alpha", static_cast<float>(alpha));
        ViewPort resultVP = processor.convertPixelFormat(resultVP16, PixelFormatIDs::RGBA8_Straight);

        // ViewPort → JavaScript
        return viewPortToJSImage(resultVP);
    }

    val mergeImages(const val& imagesArray, const val& alphasArray) {
        // JavaScript配列から画像とアルファ値を取得
        unsigned int imageCount = imagesArray["length"].as<unsigned int>();
        std::vector<ViewPort> viewports;
        std::vector<const ViewPort*> viewportPtrs;
        std::vector<double> alphas;

        // アルファ値を取得
        unsigned int alphaCount = alphasArray["length"].as<unsigned int>();
        for (unsigned int i = 0; i < alphaCount; i++) {
            alphas.push_back(alphasArray[i].as<double>());
        }

        for (unsigned int i = 0; i < imageCount; i++) {
            val imageObj = imagesArray[i];
            val imageData = imageObj["data"];
            int width = imageObj["width"].as<int>();
            int height = imageObj["height"].as<int>();

            // 入力検証: 負の値や異常に大きな値を拒否
            if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
                continue; // 不正なサイズの画像はスキップ
            }

            Image img(width, height);
            unsigned int length = imageData["length"].as<unsigned int>();
            unsigned int expectedLength = static_cast<unsigned int>(width * height * 4);

            // 境界チェック: データ長が期待値と一致するか確認
            unsigned int copyLength = std::min(length, expectedLength);
            copyLength = std::min(copyLength, static_cast<unsigned int>(img.data.size()));

            for (unsigned int j = 0; j < copyLength; j++) {
                img.data[j] = imageData[j].as<uint8_t>();
            }

            // 8bit → ViewPort変換（アルファ値を適用）
            double alpha = i < alphas.size() ? alphas[i] : 1.0;
            ViewPort temp = processor.fromImage(img);
            ViewPort vp = processor.applyFilter(temp, "alpha", static_cast<float>(alpha));
            viewports.push_back(std::move(vp));
        }

        // ポインタ配列を作成
        for (auto& vp : viewports) {
            viewportPtrs.push_back(&vp);
        }

        // マージ処理（ViewPort版）→ 8bit変換
        ViewPort resultVP = processor.mergeImages(viewportPtrs);
        Image result = processor.toImage(resultVP);

        // 結果をJavaScriptオブジェクトに変換
        val uint8Array = val::global("Uint8ClampedArray").new_(
            typed_memory_view(result.data.size(), result.data.data())
        );

        val resultObj = val::object();
        resultObj.set("data", uint8Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    // ========================================================================
    // 16bit Premultiplied Alpha版 高速処理関数群
    // ========================================================================

    // 8bit RGBA → ViewPort（16bit Premultiplied）変換
    val toPremultiplied(const val& inputImageObj, double alpha = 1.0) {
        val inputData = inputImageObj["data"];
        int width = inputImageObj["width"].as<int>();
        int height = inputImageObj["height"].as<int>();

        Image input(width, height);
        unsigned int length = inputData["length"].as<unsigned int>();
        for (unsigned int i = 0; i < length; i++) {
            input.data[i] = inputData[i].as<uint8_t>();
        }

        ViewPort temp = processor.fromImage(input);
        ViewPort result = processor.applyFilter(temp, "alpha", static_cast<float>(alpha));

        // Uint16Arrayとして返す（ViewPortの内部データをuint16_tとして公開）
        uint16_t* resultData = static_cast<uint16_t*>(result.data);
        // オーバーフロー防止のためsize_tで計算
        size_t pixelCount = static_cast<size_t>(result.width) * static_cast<size_t>(result.height) * 4;
        val uint16Array = val::global("Uint16Array").new_(
            typed_memory_view(pixelCount, resultData)
        );

        val resultObj = val::object();
        resultObj.set("data", uint16Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    // ViewPort（16bit Premultiplied）→ 8bit RGBA変換
    val fromPremultiplied(const val& inputImageObj) {
        val inputData = inputImageObj["data"];
        int width = inputImageObj["width"].as<int>();
        int height = inputImageObj["height"].as<int>();

        ViewPort input(width, height, PixelFormatIDs::RGBA16_Premultiplied);
        unsigned int length = inputData["length"].as<unsigned int>();
        uint16_t* inputPtr = static_cast<uint16_t*>(input.data);
        for (unsigned int i = 0; i < length; i++) {
            inputPtr[i] = inputData[i].as<uint16_t>();
        }

        Image result = processor.toImage(input);

        val uint8Array = val::global("Uint8ClampedArray").new_(
            typed_memory_view(result.data.size(), result.data.data())
        );

        val resultObj = val::object();
        resultObj.set("data", uint8Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    // ViewPort版フィルタ処理（16bit互換）
    val applyFilterToImage16(const val& inputImageObj, const std::string& filterType, float param) {
        val inputData = inputImageObj["data"];
        int width = inputImageObj["width"].as<int>();
        int height = inputImageObj["height"].as<int>();

        ViewPort input(width, height, PixelFormatIDs::RGBA16_Premultiplied);
        unsigned int length = inputData["length"].as<unsigned int>();
        uint16_t* inputPtr = static_cast<uint16_t*>(input.data);
        for (unsigned int i = 0; i < length; i++) {
            inputPtr[i] = inputData[i].as<uint16_t>();
        }

        ViewPort result = processor.applyFilter(input, filterType, param);

        uint16_t* resultData = static_cast<uint16_t*>(result.data);
        size_t pixelCount = static_cast<size_t>(result.width) * static_cast<size_t>(result.height) * 4;
        val uint16Array = val::global("Uint16Array").new_(
            typed_memory_view(pixelCount, resultData)
        );

        val resultObj = val::object();
        resultObj.set("data", uint16Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    // ViewPort版行列ベースアフィン変換（16bit互換）
    val applyTransformToImage16(const val& inputImageObj, double a, double b, double c,
                                double d, double tx, double ty, double alpha) {
        val inputData = inputImageObj["data"];
        int width = inputImageObj["width"].as<int>();
        int height = inputImageObj["height"].as<int>();

        ViewPort input(width, height, PixelFormatIDs::RGBA16_Premultiplied);
        unsigned int length = inputData["length"].as<unsigned int>();
        uint16_t* inputPtr = static_cast<uint16_t*>(input.data);
        for (unsigned int i = 0; i < length; i++) {
            inputPtr[i] = inputData[i].as<uint16_t>();
        }

        AffineMatrix matrix;
        matrix.a = a;
        matrix.b = b;
        matrix.c = c;
        matrix.d = d;
        matrix.tx = tx;
        matrix.ty = ty;

        ViewPort transformed = processor.applyTransform(input, matrix);
        ViewPort result = processor.applyFilter(transformed, "alpha", static_cast<float>(alpha));

        uint16_t* resultData = static_cast<uint16_t*>(result.data);
        size_t pixelCount = static_cast<size_t>(result.width) * static_cast<size_t>(result.height) * 4;
        val uint16Array = val::global("Uint16Array").new_(
            typed_memory_view(pixelCount, resultData)
        );

        val resultObj = val::object();
        resultObj.set("data", uint16Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    // ViewPort版マージ（16bit互換）
    val mergeImages16(const val& imagesArray) {
        unsigned int imageCount = imagesArray["length"].as<unsigned int>();
        std::vector<ViewPort> viewports;
        std::vector<const ViewPort*> viewportPtrs;

        // ダングリングポインタ防止: 事前にメモリを確保
        viewports.reserve(imageCount);
        viewportPtrs.reserve(imageCount);

        for (unsigned int i = 0; i < imageCount; i++) {
            val imageObj = imagesArray[i];
            val imageData = imageObj["data"];
            int width = imageObj["width"].as<int>();
            int height = imageObj["height"].as<int>();

            ViewPort vp(width, height, PixelFormatIDs::RGBA16_Premultiplied);
            unsigned int length = imageData["length"].as<unsigned int>();
            uint16_t* vpPtr = static_cast<uint16_t*>(vp.data);
            for (unsigned int j = 0; j < length; j++) {
                vpPtr[j] = imageData[j].as<uint16_t>();
            }
            viewports.push_back(std::move(vp));
        }

        for (auto& vp : viewports) {
            viewportPtrs.push_back(&vp);
        }

        ViewPort result = processor.mergeImages(viewportPtrs);

        uint16_t* resultData = static_cast<uint16_t*>(result.data);
        size_t pixelCount = static_cast<size_t>(result.width) * static_cast<size_t>(result.height) * 4;
        val uint16Array = val::global("Uint16Array").new_(
            typed_memory_view(pixelCount, resultData)
        );

        val resultObj = val::object();
        resultObj.set("data", uint16Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    // AffineMatrixをパラメータから生成するヘルパー
    val createAffineMatrix(double translateX, double translateY, double rotation,
                          double scaleX, double scaleY, double centerX, double centerY) {
        AffineParams params;
        params.translateX = translateX;
        params.translateY = translateY;
        params.rotation = rotation;
        params.scaleX = scaleX;
        params.scaleY = scaleY;

        AffineMatrix matrix = AffineMatrix::fromParams(params, centerX, centerY);

        val resultObj = val::object();
        resultObj.set("a", matrix.a);
        resultObj.set("b", matrix.b);
        resultObj.set("c", matrix.c);
        resultObj.set("d", matrix.d);
        resultObj.set("tx", matrix.tx);
        resultObj.set("ty", matrix.ty);

        return resultObj;
    }

private:
    ImageProcessor processor;
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
    class_<ImageProcessorWrapper>("ImageProcessor")
        .constructor<int, int>()
        // 8bit互換APIメソッド（内部的に16bit処理を使用）
        .function("applyFilterToImage", &ImageProcessorWrapper::applyFilterToImage)
        .function("applyTransformToImage", &ImageProcessorWrapper::applyTransformToImage)
        .function("mergeImages", &ImageProcessorWrapper::mergeImages)
        // 16bit Premultiplied Alpha版 高速処理関数
        .function("toPremultiplied", &ImageProcessorWrapper::toPremultiplied)
        .function("fromPremultiplied", &ImageProcessorWrapper::fromPremultiplied)
        .function("applyFilterToImage16", &ImageProcessorWrapper::applyFilterToImage16)
        .function("applyTransformToImage16", &ImageProcessorWrapper::applyTransformToImage16)
        .function("mergeImages16", &ImageProcessorWrapper::mergeImages16)
        .function("createAffineMatrix", &ImageProcessorWrapper::createAffineMatrix);

    class_<NodeGraphEvaluatorWrapper>("NodeGraphEvaluator")
        .constructor<int, int>()
        .function("setCanvasSize", &NodeGraphEvaluatorWrapper::setCanvasSize)
        .function("setLayerImage", &NodeGraphEvaluatorWrapper::setLayerImage)
        .function("setNodes", &NodeGraphEvaluatorWrapper::setNodes)
        .function("setConnections", &NodeGraphEvaluatorWrapper::setConnections)
        .function("evaluateGraph", &NodeGraphEvaluatorWrapper::evaluateGraph);
}
