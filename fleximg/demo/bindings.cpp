// fleximg WASM Bindings
// 既存JSアプリとの後方互換性を維持しつつ、内部でv2 Node/Portモデルを使用

#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <vector>
#include <map>
#include <memory>
#include <string>
#include <algorithm>

#include "../src/fleximg/nodes/source_node.h"
#include "../src/fleximg/nodes/sink_node.h"
#include "../src/fleximg/nodes/affine_node.h"
#include "../src/fleximg/nodes/filter_node_base.h"
#include "../src/fleximg/nodes/brightness_node.h"
#include "../src/fleximg/nodes/grayscale_node.h"
#include "../src/fleximg/nodes/horizontal_blur_node.h"
#include "../src/fleximg/nodes/vertical_blur_node.h"
#include "../src/fleximg/nodes/alpha_node.h"
#include "../src/fleximg/nodes/composite_node.h"
#include "../src/fleximg/nodes/distributor_node.h"
#include "../src/fleximg/nodes/renderer_node.h"
#include "../src/fleximg/nodes/ninepatch_source_node.h"
#include "../src/fleximg/nodes/matte_node.h"
#include "../src/fleximg/core/format_metrics.h"
#include "../src/fleximg/core/memory/pool_allocator.h"

using namespace emscripten;
using namespace FLEXIMG_NAMESPACE;

// ========================================================================
// フォーマットID変換ヘルパー（後方互換用）
// ========================================================================
// 旧 PixelFormatID (uint32_t) と新 PixelFormatID (Descriptor*) の変換


// ========================================================================
// SinkOutput - Sink別出力管理（複数Sink対応）
// ========================================================================

struct SinkOutput {
    std::vector<uint8_t> buffer;
    PixelFormatID format = PixelFormatIDs::RGBA8_Straight;
    int width = 0;
    int height = 0;
};

// ========================================================================
// ImageStore - 入出力画像データの永続化管理
// ========================================================================

class ImageStore {
public:
    // 外部データをコピーして保存（入力画像用）
    ViewPort store(int id, const uint8_t* data, int w, int h, PixelFormatID fmt) {
        auto bpp = getBytesPerPixel(fmt);
        auto size = static_cast<size_t>(w * h * bpp);
        storage_[id].assign(data, data + size);
        return ViewPort(storage_[id].data(), fmt, w * bpp, w, h);
    }

    // バッファを確保（出力用）
    ViewPort allocate(int id, int w, int h, PixelFormatID fmt) {
        auto bpp = getBytesPerPixel(fmt);
        auto size = static_cast<size_t>(w * h * bpp);
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
    double pivotX = 0;  // 基準点X（pivot: 画像内のアンカーポイント）
    double pivotY = 0;  // 基準点Y（pivot: 画像内のアンカーポイント）
    float outputWidth = 0;    // sinkノード用: 出力バッファ幅、ninepatchノード用: 出力幅（小数対応）
    float outputHeight = 0;   // sinkノード用: 出力バッファ高さ、ninepatchノード用: 出力高さ（小数対応）
    std::string filterType;
    std::vector<float> filterParams;
    bool independent = false;
    bool bilinear = false;  // imageノード用: バイリニア補間を使用
    struct { double a=1, b=0, c=0, d=1, tx=0, ty=0; } affineMatrix;  // tx/tyが配置位置を表す
    std::vector<std::string> compositeInputIds;   // compositeノード用（N入力）
    std::vector<std::string> distributorOutputIds; // distributorノード用（N出力）
};

struct GraphConnection {
    std::string fromNodeId;
    std::string fromPortId;
    std::string toNodeId;
    std::string toPortId;
};

// ========================================================================
// ノード構築ヘルパー関数
// ========================================================================

// フィルタノードを生成（上流・下流共通）
std::unique_ptr<Node> createFilterNode(const GraphNode& gnode) {
    if (gnode.filterType == "brightness" && !gnode.filterParams.empty()) {
        auto node = std::make_unique<BrightnessNode>();
        node->setAmount(gnode.filterParams[0]);
        return node;
    }
    else if (gnode.filterType == "grayscale") {
        return std::make_unique<GrayscaleNode>();
    }
    else if (gnode.filterType == "horizontalBlur" && !gnode.filterParams.empty()) {
        auto node = std::make_unique<HorizontalBlurNode>();
        node->setRadius(static_cast<int>(gnode.filterParams[0]));
        if (gnode.filterParams.size() > 1) {
            node->setPasses(static_cast<int>(gnode.filterParams[1]));
        }
        return node;
    }
    else if (gnode.filterType == "verticalBlur" && !gnode.filterParams.empty()) {
        auto node = std::make_unique<VerticalBlurNode>();
        node->setRadius(static_cast<int>(gnode.filterParams[0]));
        if (gnode.filterParams.size() > 1) {
            node->setPasses(static_cast<int>(gnode.filterParams[1]));
        }
        return node;
    }
    else if (gnode.filterType == "alpha" && !gnode.filterParams.empty()) {
        auto node = std::make_unique<AlphaNode>();
        node->setScale(gnode.filterParams[0]);
        return node;
    }
    return nullptr;
}

// アフィンノードを生成（上流・下流共通）
std::unique_ptr<AffineNode> createAffineNode(const GraphNode& gnode) {
    auto affineNode = std::make_unique<AffineNode>();
    AffineMatrix mat;
    mat.a = static_cast<float>(gnode.affineMatrix.a);
    mat.b = static_cast<float>(gnode.affineMatrix.b);
    mat.c = static_cast<float>(gnode.affineMatrix.c);
    mat.d = static_cast<float>(gnode.affineMatrix.d);
    mat.tx = static_cast<float>(gnode.affineMatrix.tx);
    mat.ty = static_cast<float>(gnode.affineMatrix.ty);
    affineNode->setMatrix(mat);
    return affineNode;
}

// ========================================================================
// NodeGraphEvaluatorWrapper - 既存API互換ラッパー
// ========================================================================

class NodeGraphEvaluatorWrapper {
public:
    NodeGraphEvaluatorWrapper(int width, int height)
        : canvasWidth_(width), canvasHeight_(height) {
        // パイプライン用PoolAllocatorを初期化
        poolAllocator_.initialize(poolMemory_, POOL_BLOCK_SIZE, POOL_BLOCK_COUNT, false);
        poolAdapter_ = std::make_unique<core::memory::PoolAllocatorAdapter>(poolAllocator_);
    }

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

    void setDebugDataRange(bool enabled) {
        debugDataRange_ = enabled;
    }

    // Sink別出力フォーマットを設定
    void setSinkFormat(const std::string& sinkId, const std::string& formatName) {
        PixelFormatID format = getFormatByName(formatName.c_str());
        sinkFormats_[sinkId] = format ? format : PixelFormatIDs::RGBA8_Straight;
    }

    // Sink別プレビュー取得（RGBA8888に変換して返す）
    val getSinkPreview(const std::string& sinkId) {
        auto it = sinkOutputs_.find(sinkId);
        if (it == sinkOutputs_.end()) {
            return val::null();
        }

        const auto& sinkOut = it->second;
        if (sinkOut.width == 0 || sinkOut.height == 0 || sinkOut.buffer.empty()) {
            return val::null();
        }

        // 結果オブジェクトを作成
        val result = val::object();
        result.set("width", sinkOut.width);
        result.set("height", sinkOut.height);
        result.set("format", std::string(getFormatName(sinkOut.format)));

        size_t pixelCount = static_cast<size_t>(sinkOut.width) * static_cast<size_t>(sinkOut.height);
        size_t rgba8Size = pixelCount * 4;

        // RGBA8888 に変換（または同一フォーマットならコピー）
        if (sinkOut.format == PixelFormatIDs::RGBA8_Straight) {
            // そのまま返す
            val data = val::global("Uint8ClampedArray").new_(
                typed_memory_view(sinkOut.buffer.size(), sinkOut.buffer.data())
            );
            result.set("data", data);
        } else {
            // フォーマット変換（UI用 - メトリクスから除外）
            FormatOpEntry snapshot[FormatIdx::Count][OpType::Count];
            FormatMetrics::instance().saveSnapshot(snapshot);

            std::vector<uint8_t> rgba8Data(rgba8Size);
            convertFormat(
                sinkOut.buffer.data(), sinkOut.format,
                rgba8Data.data(), PixelFormatIDs::RGBA8_Straight,
                static_cast<int>(pixelCount)
            );

            FormatMetrics::instance().restoreSnapshot(snapshot);

            val data = val::global("Uint8ClampedArray").new_(
                typed_memory_view(rgba8Data.size(), rgba8Data.data())
            );
            result.set("data", data);
        }

        return result;
    }

    // 画像を登録（データをコピー、フォーマット指定なし = RGBA8）
    void storeImage(int id, const val& imageData, int width, int height) {
        storeImageWithFormat(id, imageData, width, height, "RGBA8_Straight");
    }

    // 画像を登録（フォーマット指定あり）
    void storeImageWithFormat(int id, const val& imageData, int width, int height, const std::string& formatName) {
        PixelFormatID targetFormat = getFormatByName(formatName.c_str());
        if (!targetFormat) targetFormat = PixelFormatIDs::RGBA8_Straight;

        // JS から RGBA8 データを受け取り
        unsigned int length = imageData["length"].as<unsigned int>();
        std::vector<uint8_t> rgba8Data(length);
        for (unsigned int i = 0; i < length; i++) {
            rgba8Data[i] = imageData[i].as<uint8_t>();
        }

        // フォーマット変換（バインディング層の責務）
        if (targetFormat == PixelFormatIDs::RGBA8_Straight) {
            // 変換不要
            imageViews_[id] = imageStore_.store(id, rgba8Data.data(), width, height, targetFormat);
        } else {
            // フォーマット変換（UI用 - メトリクスから除外）
            FormatOpEntry snapshot[FormatIdx::Count][OpType::Count];
            FormatMetrics::instance().saveSnapshot(snapshot);

            auto targetBpp = getBytesPerPixel(targetFormat);
            std::vector<uint8_t> converted(static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(targetBpp));

            convertFormat(
                rgba8Data.data(), PixelFormatIDs::RGBA8_Straight,
                converted.data(), targetFormat,
                static_cast<int>(static_cast<size_t>(width) * static_cast<size_t>(height))
            );

            FormatMetrics::instance().restoreSnapshot(snapshot);

            imageViews_[id] = imageStore_.store(id, converted.data(), width, height, targetFormat);
        }
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
                // 新API: pivot オブジェクト（推奨）
                if (nodeObj["pivot"].typeOf().as<std::string>() != "undefined") {
                    auto pivot = nodeObj["pivot"];
                    if (pivot["x"].typeOf().as<std::string>() != "undefined") {
                        node.pivotX = pivot["x"].as<double>();
                    }
                    if (pivot["y"].typeOf().as<std::string>() != "undefined") {
                        node.pivotY = pivot["y"].as<double>();
                    }
                }
                // 旧API: originX/originY（後方互換）
                else {
                    if (nodeObj["originX"].typeOf().as<std::string>() != "undefined") {
                        node.pivotX = nodeObj["originX"].as<double>();
                    }
                    if (nodeObj["originY"].typeOf().as<std::string>() != "undefined") {
                        node.pivotY = nodeObj["originY"].as<double>();
                    }
                }
                if (nodeObj["bilinear"].typeOf().as<std::string>() != "undefined") {
                    node.bilinear = nodeObj["bilinear"].as<bool>();
                }
                // アフィン変換行列（AffineCapability対応）
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

            // composite用パラメータ（N入力・1出力）
            if (node.type == "composite") {
                if (nodeObj["inputs"].typeOf().as<std::string>() != "undefined") {
                    val inputsArray = nodeObj["inputs"];
                    unsigned int inputCount = inputsArray["length"].as<unsigned int>();
                    for (unsigned int j = 0; j < inputCount; j++) {
                        val inputObj = inputsArray[j];
                        node.compositeInputIds.push_back(inputObj["id"].as<std::string>());
                    }
                }
                // アフィン変換行列（AffineCapability対応）
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

            // distributor用パラメータ（1入力・N出力、compositeと対称）
            if (node.type == "distributor") {
                if (nodeObj["outputs"].typeOf().as<std::string>() != "undefined") {
                    val outputsArray = nodeObj["outputs"];
                    unsigned int outputCount = outputsArray["length"].as<unsigned int>();
                    for (unsigned int j = 0; j < outputCount; j++) {
                        val outputObj = outputsArray[j];
                        node.distributorOutputIds.push_back(outputObj["id"].as<std::string>());
                    }
                }
                // アフィン変換行列（AffineCapability対応）
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
                if (nodeObj["outputWidth"].typeOf().as<std::string>() != "undefined") {
                    node.outputWidth = nodeObj["outputWidth"].as<float>();
                }
                if (nodeObj["outputHeight"].typeOf().as<std::string>() != "undefined") {
                    node.outputHeight = nodeObj["outputHeight"].as<float>();
                }
                // Sink固有の基準点（pivot: ワールド原点に対応するバッファ座標）
                if (nodeObj["pivotX"].typeOf().as<std::string>() != "undefined") {
                    node.pivotX = nodeObj["pivotX"].as<double>();
                }
                if (nodeObj["pivotY"].typeOf().as<std::string>() != "undefined") {
                    node.pivotY = nodeObj["pivotY"].as<double>();
                }
                // アフィン変換行列（AffineCapability対応）
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

            // ninepatch用パラメータ
            if (node.type == "ninepatch") {
                if (nodeObj["imageId"].typeOf().as<std::string>() != "undefined") {
                    node.imageId = nodeObj["imageId"].as<int>();
                }
                if (nodeObj["outputWidth"].typeOf().as<std::string>() != "undefined") {
                    node.outputWidth = nodeObj["outputWidth"].as<float>();
                }
                if (nodeObj["outputHeight"].typeOf().as<std::string>() != "undefined") {
                    node.outputHeight = nodeObj["outputHeight"].as<float>();
                }
                // 新API: pivot オブジェクト（推奨）
                if (nodeObj["pivot"].typeOf().as<std::string>() != "undefined") {
                    auto pivot = nodeObj["pivot"];
                    if (pivot["x"].typeOf().as<std::string>() != "undefined") {
                        node.pivotX = pivot["x"].as<double>();
                    }
                    if (pivot["y"].typeOf().as<std::string>() != "undefined") {
                        node.pivotY = pivot["y"].as<double>();
                    }
                }
                // 旧API: originX/originY（後方互換）
                else {
                    if (nodeObj["originX"].typeOf().as<std::string>() != "undefined") {
                        node.pivotX = nodeObj["originX"].as<double>();
                    }
                    if (nodeObj["originY"].typeOf().as<std::string>() != "undefined") {
                        node.pivotY = nodeObj["originY"].as<double>();
                    }
                }
                // バイリニア補間
                if (nodeObj["bilinear"].typeOf().as<std::string>() != "undefined") {
                    node.bilinear = nodeObj["bilinear"].as<bool>();
                }
                // アフィン変換行列（AffineCapability対応）
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
            conn.fromPortId = connObj["fromPortId"].as<std::string>();
            conn.toNodeId = connObj["toNodeId"].as<std::string>();
            conn.toPortId = connObj["toPortId"].as<std::string>();

            graphConnections_.push_back(conn);
        }
    }

    // グラフ評価
    // 戻り値: 0 = 成功、非0 = エラー（PrepareStatus値）
    int evaluateGraph() {
        // グラフからv2ノードを構築して実行
        return buildAndExecute();
    }

    val getPerfMetrics() {
        val result = val::object();

        // nodes配列を構築
        val nodes = val::array();
        for (int i = 0; i < NodeType::Count; i++) {
            val nodeMetrics = val::object();
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            nodeMetrics.set("time_us", lastPerfMetrics_.nodes[i].time_us);
            nodeMetrics.set("count", lastPerfMetrics_.nodes[i].count);
            nodeMetrics.set("requestedPixels", static_cast<double>(lastPerfMetrics_.nodes[i].requestedPixels));
            nodeMetrics.set("usedPixels", static_cast<double>(lastPerfMetrics_.nodes[i].usedPixels));
            nodeMetrics.set("theoreticalMinPixels", static_cast<double>(lastPerfMetrics_.nodes[i].theoreticalMinPixels));
            nodeMetrics.set("wasteRatio", lastPerfMetrics_.nodes[i].wasteRatio());
            nodeMetrics.set("pixelEfficiency", lastPerfMetrics_.nodes[i].pixelEfficiency());
            nodeMetrics.set("splitEfficiencyEstimate", lastPerfMetrics_.nodes[i].splitEfficiencyEstimate());
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
            nodeMetrics.set("theoreticalMinPixels", 0.0);
            nodeMetrics.set("wasteRatio", 0.0f);
            nodeMetrics.set("pixelEfficiency", 1.0f);
            nodeMetrics.set("splitEfficiencyEstimate", 1.0f);
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
        // フィルタ3種の合計
        uint32_t filterTimeSum = lastPerfMetrics_.nodes[NodeType::Brightness].time_us
                               + lastPerfMetrics_.nodes[NodeType::Grayscale].time_us
                               + lastPerfMetrics_.nodes[NodeType::Alpha].time_us;
        int filterCountSum = lastPerfMetrics_.nodes[NodeType::Brightness].count
                           + lastPerfMetrics_.nodes[NodeType::Grayscale].count
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
        // PoolAllocator統計
        {
            val poolStats = val::object();
            const auto& adapterStats = poolAdapter_->stats();
            poolStats.set("poolHits", static_cast<int>(adapterStats.poolHits));
            poolStats.set("poolMisses", static_cast<int>(adapterStats.poolMisses));
            poolStats.set("poolDeallocs", static_cast<int>(adapterStats.poolDeallocs));
            poolStats.set("defaultDeallocs", static_cast<int>(adapterStats.defaultDeallocs));
            poolStats.set("lastAllocSize", static_cast<int>(adapterStats.lastAllocSize));
            // PoolAllocator本体の統計
            const auto& coreStats = poolAllocator_.stats();
            poolStats.set("totalAllocations", static_cast<int>(coreStats.totalAllocations));
            poolStats.set("totalDeallocations", static_cast<int>(coreStats.totalDeallocations));
            poolStats.set("usedBlocks", static_cast<int>(poolAllocator_.usedBlockCount()));
            poolStats.set("peakUsedBlocks", static_cast<int>(coreStats.peakUsedBlocks));
            poolStats.set("freeBlocks", static_cast<int>(poolAllocator_.freeBlockCount()));
            poolStats.set("blockSize", static_cast<int>(poolAllocator_.blockSize()));
            poolStats.set("blockCount", static_cast<int>(poolAllocator_.blockCount()));
            result.set("poolAllocator", poolStats);
        }
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
        // PoolAllocator統計（リリースビルドでは基本情報のみ）
        {
            val poolStats = val::object();
            poolStats.set("poolHits", 0);
            poolStats.set("poolMisses", 0);
            poolStats.set("poolDeallocs", 0);
            poolStats.set("defaultDeallocs", 0);
            poolStats.set("lastAllocSize", 0);
            poolStats.set("totalAllocations", 0);
            poolStats.set("totalDeallocations", 0);
            poolStats.set("usedBlocks", static_cast<int>(poolAllocator_.usedBlockCount()));
            poolStats.set("peakUsedBlocks", 0);
            poolStats.set("freeBlocks", static_cast<int>(poolAllocator_.freeBlockCount()));
            poolStats.set("blockSize", static_cast<int>(poolAllocator_.blockSize()));
            poolStats.set("blockCount", static_cast<int>(poolAllocator_.blockCount()));
            result.set("poolAllocator", poolStats);
        }
#endif

        return result;
    }

    val getFormatMetrics() {
        val result = val::object();

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = FormatMetrics::instance();

        // フォーマット名配列
        static const char* formatNames[] = {
            "RGBA8_Straight", "RGB565_LE", "RGB565_BE",
            "RGB332", "RGB888", "BGR888", "Alpha8", "Grayscale8", "Index8"
        };
        static const char* opNames[] = {
            "toStraight", "fromStraight", "blendUnder"
        };

        // フォーマット別データ
        val formats = val::array();
        for (int f = 0; f < FormatIdx::Count; f++) {
            val fmtData = val::object();
            fmtData.set("name", formatNames[f]);

            val ops = val::array();
            for (int o = 0; o < OpType::Count; o++) {
                val opData = val::object();
                opData.set("name", opNames[o]);
                opData.set("callCount", metrics.data[f][o].callCount);
                opData.set("pixelCount", static_cast<double>(metrics.data[f][o].pixelCount));
                ops.call<void>("push", opData);
            }
            fmtData.set("ops", ops);

            // フォーマット合計
            auto fmtTotal = metrics.totalByFormat(f);
            fmtData.set("totalCalls", fmtTotal.callCount);
            fmtData.set("totalPixels", static_cast<double>(fmtTotal.pixelCount));

            formats.call<void>("push", fmtData);
        }
        result.set("formats", formats);

        // 操作別合計
        val opTotals = val::array();
        for (int o = 0; o < OpType::Count; o++) {
            val opTotal = val::object();
            opTotal.set("name", opNames[o]);
            auto t = metrics.totalByOp(o);
            opTotal.set("callCount", t.callCount);
            opTotal.set("pixelCount", static_cast<double>(t.pixelCount));
            opTotals.call<void>("push", opTotal);
        }
        result.set("opTotals", opTotals);

        // 全体合計
        auto total = metrics.total();
        result.set("totalCalls", total.callCount);
        result.set("totalPixels", static_cast<double>(total.pixelCount));
#else
        result.set("formats", val::array());
        result.set("opTotals", val::array());
        result.set("totalCalls", 0);
        result.set("totalPixels", 0.0);
#endif

        return result;
    }

private:
    int canvasWidth_, canvasHeight_;
    double dstOriginX_ = 0, dstOriginY_ = 0;
    int tileWidth_ = 0, tileHeight_ = 0;
    bool debugCheckerboard_ = false;
    bool debugDataRange_ = false;

    ImageStore imageStore_;
    std::map<int, ViewPort> imageViews_;
    std::vector<GraphNode> graphNodes_;
    std::vector<GraphConnection> graphConnections_;
    PerfMetrics lastPerfMetrics_;

    // Sink別出力管理（複数Sink対応）
    std::map<std::string, SinkOutput> sinkOutputs_;
    std::map<std::string, PixelFormatID> sinkFormats_;

    // パイプライン用PoolAllocator（動作検証用）
    static constexpr size_t POOL_BLOCK_SIZE = 1024;
    static constexpr size_t POOL_BLOCK_COUNT = 32;
    uint8_t poolMemory_[POOL_BLOCK_SIZE * POOL_BLOCK_COUNT];
    core::memory::PoolAllocator poolAllocator_;
    std::unique_ptr<core::memory::PoolAllocatorAdapter> poolAdapter_;

    // グラフを解析してv2ノードを構築・実行
    // 戻り値: 0 = 成功、非0 = エラー（PrepareStatus値）
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

        // 全てのSinkノードを収集（複数Sink対応）
        std::vector<const GraphNode*> sinkGraphNodes;
        for (const auto& node : graphNodes_) {
            if (node.type == "sink" && node.imageId >= 0) {
                sinkGraphNodes.push_back(&node);
            }
        }

        if (sinkGraphNodes.empty()) {
            return static_cast<int>(PrepareStatus::Prepared);  // 描画対象なし
        }

        // 各Sinkに対応するSinkNode + 出力バッファを準備
        struct SinkInfo {
            const GraphNode* graphNode;
            std::unique_ptr<SinkNode> node;
            ViewPort targetView;
            ViewPort outputView;  // JS側表示用
            PixelFormatID format;
            int width, height;
        };
        std::vector<SinkInfo> sinkInfos;
        std::map<std::string, SinkNode*> sinkNodeMap;  // ID -> SinkNode*

        for (const GraphNode* gnode : sinkGraphNodes) {
            SinkInfo info;
            info.graphNode = gnode;

            // 出力先ViewPortを取得（JS側表示用）
            auto outputIt = imageViews_.find(gnode->imageId);
            if (outputIt == imageViews_.end()) {
                continue;  // 出力先が確保されていないSinkはスキップ
            }
            info.outputView = outputIt->second;

            // 出力バッファをクリア
            view_ops::clear(info.outputView, 0, 0, info.outputView.width, info.outputView.height);

            // Sink別出力バッファを準備
            auto formatIt = sinkFormats_.find(gnode->id);
            info.format = (formatIt != sinkFormats_.end())
                ? formatIt->second
                : PixelFormatIDs::RGBA8_Straight;

            // Sinkノード固有のサイズと基準点を使用（整数に丸め）
            info.width = (gnode->outputWidth > 0) ? static_cast<int>(gnode->outputWidth) : canvasWidth_;
            info.height = (gnode->outputHeight > 0) ? static_cast<int>(gnode->outputHeight) : canvasHeight_;

            auto& sinkOut = sinkOutputs_[gnode->id];
            sinkOut.format = info.format;
            sinkOut.width = info.width;
            sinkOut.height = info.height;
            auto sinkBpp = getBytesPerPixel(info.format);
            auto sinkBufferSize = static_cast<size_t>(info.width * info.height * sinkBpp);
            sinkOut.buffer.resize(sinkBufferSize);
            std::fill(sinkOut.buffer.begin(), sinkOut.buffer.end(), 0);

            // SinkNode用のViewPortを作成
            info.targetView = ViewPort(sinkOut.buffer.data(), info.format,
                                       info.width * sinkBpp, info.width, info.height);

            // SinkNodeを作成
            info.node = std::make_unique<SinkNode>();
            info.node->setTarget(info.targetView);
            info.node->setPivot(float_to_fixed(static_cast<float>(gnode->pivotX)),
                                float_to_fixed(static_cast<float>(gnode->pivotY)));

            // アフィン変換行列の適用（単位行列でない場合のみ）
            if (gnode->affineMatrix.a != 1.0 || gnode->affineMatrix.b != 0.0 ||
                gnode->affineMatrix.c != 0.0 || gnode->affineMatrix.d != 1.0 ||
                gnode->affineMatrix.tx != 0.0 || gnode->affineMatrix.ty != 0.0) {
                AffineMatrix mat;
                mat.a = static_cast<float>(gnode->affineMatrix.a);
                mat.b = static_cast<float>(gnode->affineMatrix.b);
                mat.c = static_cast<float>(gnode->affineMatrix.c);
                mat.d = static_cast<float>(gnode->affineMatrix.d);
                mat.tx = static_cast<float>(gnode->affineMatrix.tx);
                mat.ty = static_cast<float>(gnode->affineMatrix.ty);
                info.node->setMatrix(mat);
            }

            sinkNodeMap[gnode->id] = info.node.get();
            sinkInfos.push_back(std::move(info));
        }

        if (sinkInfos.empty()) {
            return static_cast<int>(PrepareStatus::Prepared);  // 有効なSinkなし
        }

        // v2ノードを一時的に保持するコンテナ
        std::map<std::string, std::unique_ptr<Node>> v2Nodes;
        std::map<std::string, std::unique_ptr<SourceNode>> sourceNodes;

        // RendererNodeを作成
        auto rendererNode = std::make_unique<RendererNode>();
        rendererNode->setVirtualScreen(canvasWidth_, canvasHeight_);
        rendererNode->setPivot(static_cast<float>(dstOriginX_), static_cast<float>(dstOriginY_));
        rendererNode->setAllocator(poolAdapter_.get());

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        // フレームごとの統計をリセット
        poolAllocator_.resetStats();
        poolAdapter_->resetStats();
#endif

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
                src->setPivot(float_to_fixed(static_cast<float>(gnode.pivotX)),
                              float_to_fixed(static_cast<float>(gnode.pivotY)));
                // 配置位置は affineMatrix.tx/ty で管理（setPosition廃止）
                if (gnode.bilinear) {
                    src->setInterpolationMode(InterpolationMode::Bilinear);
                }
                // アフィン変換行列の適用（単位行列でない場合のみ）
                if (gnode.affineMatrix.a != 1.0 || gnode.affineMatrix.b != 0.0 ||
                    gnode.affineMatrix.c != 0.0 || gnode.affineMatrix.d != 1.0 ||
                    gnode.affineMatrix.tx != 0.0 || gnode.affineMatrix.ty != 0.0) {
                    AffineMatrix mat;
                    mat.a = static_cast<float>(gnode.affineMatrix.a);
                    mat.b = static_cast<float>(gnode.affineMatrix.b);
                    mat.c = static_cast<float>(gnode.affineMatrix.c);
                    mat.d = static_cast<float>(gnode.affineMatrix.d);
                    mat.tx = static_cast<float>(gnode.affineMatrix.tx);
                    mat.ty = static_cast<float>(gnode.affineMatrix.ty);
                    src->setMatrix(mat);
                }

                Node* result = src.get();
                sourceNodes[nodeId] = std::move(src);
                return result;
            }
            else if (gnode.type == "ninepatch") {
                auto viewIt = imageViews_.find(gnode.imageId);
                if (viewIt == imageViews_.end()) return nullptr;

                auto np = std::make_unique<NinePatchSourceNode>();
                np->setupFromNinePatch(viewIt->second);
                np->setOutputSize(
                    gnode.outputWidth > 0 ? gnode.outputWidth : static_cast<float>(viewIt->second.width - 2),
                    gnode.outputHeight > 0 ? gnode.outputHeight : static_cast<float>(viewIt->second.height - 2)
                );
                np->setPivot(float_to_fixed(static_cast<float>(gnode.pivotX)),
                             float_to_fixed(static_cast<float>(gnode.pivotY)));
                // 配置位置は affineMatrix.tx/ty で管理（setPosition廃止）
                if (gnode.bilinear) {
                    np->setInterpolationMode(InterpolationMode::Bilinear);
                }
                // アフィン変換行列の適用（単位行列でない場合のみ）
                if (gnode.affineMatrix.a != 1.0 || gnode.affineMatrix.b != 0.0 ||
                    gnode.affineMatrix.c != 0.0 || gnode.affineMatrix.d != 1.0 ||
                    gnode.affineMatrix.tx != 0.0 || gnode.affineMatrix.ty != 0.0) {
                    AffineMatrix mat;
                    mat.a = static_cast<float>(gnode.affineMatrix.a);
                    mat.b = static_cast<float>(gnode.affineMatrix.b);
                    mat.c = static_cast<float>(gnode.affineMatrix.c);
                    mat.d = static_cast<float>(gnode.affineMatrix.d);
                    mat.tx = static_cast<float>(gnode.affineMatrix.tx);
                    mat.ty = static_cast<float>(gnode.affineMatrix.ty);
                    np->setMatrix(mat);
                }

                Node* result = np.get();
                v2Nodes[nodeId] = std::move(np);
                return result;
            }
            else if (gnode.type == "filter" && gnode.independent) {
                auto filterNode = createFilterNode(gnode);
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
                auto affineNode = createAffineNode(gnode);

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

                // 各入力を接続（toPortIdからポート番号を抽出）
                for (const auto& conn : graphConnections_) {
                    if (conn.toNodeId == nodeId) {
                        // toPortId（"in1", "in2", "in3", ...）からポート番号を抽出
                        // "in1" → 0, "in2" → 1, "in3" → 2, ...
                        int portIndex = -1;
                        if (conn.toPortId.size() >= 3 && conn.toPortId.substr(0, 2) == "in") {
                            portIndex = std::stoi(conn.toPortId.substr(2)) - 1;
                        }
                        if (portIndex >= 0 && portIndex < inputCount) {
                            Node* upstream = buildNode(conn.fromNodeId);
                            if (upstream) {
                                upstream->connectTo(*compositeNode, portIndex);
                            }
                        }
                    }
                }

                // アフィン変換行列の適用（単位行列でない場合のみ）
                if (gnode.affineMatrix.a != 1.0 || gnode.affineMatrix.b != 0.0 ||
                    gnode.affineMatrix.c != 0.0 || gnode.affineMatrix.d != 1.0 ||
                    gnode.affineMatrix.tx != 0.0 || gnode.affineMatrix.ty != 0.0) {
                    AffineMatrix mat;
                    mat.a = static_cast<float>(gnode.affineMatrix.a);
                    mat.b = static_cast<float>(gnode.affineMatrix.b);
                    mat.c = static_cast<float>(gnode.affineMatrix.c);
                    mat.d = static_cast<float>(gnode.affineMatrix.d);
                    mat.tx = static_cast<float>(gnode.affineMatrix.tx);
                    mat.ty = static_cast<float>(gnode.affineMatrix.ty);
                    compositeNode->setMatrix(mat);
                }

                Node* result = compositeNode.get();
                v2Nodes[nodeId] = std::move(compositeNode);
                return result;
            }
            else if (gnode.type == "matte") {
                // MatteNode: 3入力（前景, 背景, マスク）→1出力
                auto matteNode = std::make_unique<MatteNode>();

                // 各入力を接続（toPortIdからポート番号を抽出）
                for (const auto& conn : graphConnections_) {
                    if (conn.toNodeId == nodeId) {
                        // toPortId（"in1", "in2", "in3"）からポート番号を抽出
                        // "in1" → 0, "in2" → 1, "in3" → 2
                        int portIndex = -1;
                        if (conn.toPortId.size() >= 3 && conn.toPortId.substr(0, 2) == "in") {
                            portIndex = std::stoi(conn.toPortId.substr(2)) - 1;
                        }
                        if (portIndex >= 0 && portIndex < 3) {
                            Node* upstream = buildNode(conn.fromNodeId);
                            if (upstream) {
                                upstream->connectTo(*matteNode, portIndex);
                            }
                        }
                    }
                }

                Node* result = matteNode.get();
                v2Nodes[nodeId] = std::move(matteNode);
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
            // sinkノードならsinkNodeMapから取得して返す
            auto sinkIt = sinkNodeMap.find(nodeId);
            if (sinkIt != sinkNodeMap.end()) {
                return sinkIt->second;
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
                newNode = createFilterNode(gnode);
            }
            else if (gnode.type == "affine") {
                newNode = createAffineNode(gnode);
            }
            else if (gnode.type == "distributor") {
                // DistributorNode: 1入力・N出力（CompositeNodeと対称）
                int outputCount = static_cast<int>(gnode.distributorOutputIds.size());
                if (outputCount < 1) outputCount = 1;

                auto distributorNode = std::make_unique<DistributorNode>(outputCount);

                // 各出力を接続（接続情報から下流を探す）
                auto outputIt = outputConnections.find(nodeId);
                if (outputIt != outputConnections.end()) {
                    int portIndex = 0;
                    for (const auto& downstreamId : outputIt->second) {
                        Node* downstream = buildDownstreamChain(downstreamId);
                        if (downstream && portIndex < outputCount) {
                            distributorNode->connectTo(*downstream, 0, portIndex);
                            portIndex++;
                        }
                    }
                }

                // アフィン変換行列の適用（単位行列でない場合のみ）
                if (gnode.affineMatrix.a != 1.0 || gnode.affineMatrix.b != 0.0 ||
                    gnode.affineMatrix.c != 0.0 || gnode.affineMatrix.d != 1.0 ||
                    gnode.affineMatrix.tx != 0.0 || gnode.affineMatrix.ty != 0.0) {
                    AffineMatrix mat;
                    mat.a = static_cast<float>(gnode.affineMatrix.a);
                    mat.b = static_cast<float>(gnode.affineMatrix.b);
                    mat.c = static_cast<float>(gnode.affineMatrix.c);
                    mat.d = static_cast<float>(gnode.affineMatrix.d);
                    mat.tx = static_cast<float>(gnode.affineMatrix.tx);
                    mat.ty = static_cast<float>(gnode.affineMatrix.ty);
                    distributorNode->setMatrix(mat);
                }

                Node* result = distributorNode.get();
                v2Nodes[nodeId] = std::move(distributorNode);
                return result;
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
            // 旧形式: sinkの入力が直接upstreamの場合（最初のSinkを使用）
            if (!sinkGraphNodes.empty()) {
                auto sinkConnIt = inputConnections.find(sinkGraphNodes[0]->id);
                if (sinkConnIt != inputConnections.end() && !sinkConnIt->second.empty()) {
                    const std::string& inputId = sinkConnIt->second[0];
                    if (inputId != "renderer") {
                        rendererInputId = inputId;
                    }
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
                    // 全ての下流を接続（distributor経由で複数Sinkに分岐する可能性）
                    for (const auto& downstreamId : rendererOutputIt->second) {
                        Node* downstream = buildDownstreamChain(downstreamId);
                        if (downstream) {
                            rendererNode->connectTo(*downstream);
                        }
                    }
                } else {
                    // フォールバック: 最初のSinkに直接接続
                    if (!sinkInfos.empty()) {
                        rendererNode->connectTo(*sinkInfos[0].node);
                    }
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
        rendererNode->setDebugDataRange(debugDataRange_);
        PrepareStatus result = rendererNode->exec();

        // パフォーマンスメトリクスを保存
        lastPerfMetrics_ = rendererNode->getPerfMetrics();

        // 全てのSink出力をJS表示用バッファにコピー（UI用 - メトリクスから除外）
        FormatOpEntry snapshot[FormatIdx::Count][OpType::Count];
        FormatMetrics::instance().saveSnapshot(snapshot);

        for (auto& info : sinkInfos) {
            const auto& sinkOut = sinkOutputs_[info.graphNode->id];
            if (info.format == PixelFormatIDs::RGBA8_Straight) {
                // 同一フォーマットなら直接コピー
                view_ops::copy(info.outputView, 0, 0, info.targetView, 0, 0,
                              info.width, info.height);
            } else {
                // フォーマット変換してコピー
                size_t pixelCount = static_cast<size_t>(info.width) * static_cast<size_t>(info.height);
                convertFormat(
                    sinkOut.buffer.data(), info.format,
                    static_cast<uint8_t*>(info.outputView.data), PixelFormatIDs::RGBA8_Straight,
                    static_cast<int>(pixelCount)
                );
            }
        }

        FormatMetrics::instance().restoreSnapshot(snapshot);

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
        .function("setDebugDataRange", &NodeGraphEvaluatorWrapper::setDebugDataRange)
        .function("storeImage", &NodeGraphEvaluatorWrapper::storeImage)
        .function("storeImageWithFormat", &NodeGraphEvaluatorWrapper::storeImageWithFormat)
        .function("allocateImage", &NodeGraphEvaluatorWrapper::allocateImage)
        .function("getImage", &NodeGraphEvaluatorWrapper::getImage)
        .function("setNodes", &NodeGraphEvaluatorWrapper::setNodes)
        .function("setConnections", &NodeGraphEvaluatorWrapper::setConnections)
        .function("evaluateGraph", &NodeGraphEvaluatorWrapper::evaluateGraph)
        .function("getPerfMetrics", &NodeGraphEvaluatorWrapper::getPerfMetrics)
        .function("getFormatMetrics", &NodeGraphEvaluatorWrapper::getFormatMetrics)
        .function("setSinkFormat", &NodeGraphEvaluatorWrapper::setSinkFormat)
        .function("getSinkPreview", &NodeGraphEvaluatorWrapper::getSinkPreview);
}
