#include "node_graph.h"
#include "image_types.h"

namespace ImageTransform {

// ========================================================================
// NodeGraphEvaluator実装
// ========================================================================

NodeGraphEvaluator::NodeGraphEvaluator(int width, int height)
    : canvasWidth(width), canvasHeight(height), processor(width, height) {}

void NodeGraphEvaluator::setCanvasSize(int width, int height) {
    canvasWidth = width;
    canvasHeight = height;
    processor.setCanvasSize(width, height);
}

void NodeGraphEvaluator::registerImage(int imageId, const Image& img) {
    // Image → ViewPort(RGBA8_Straight) に変換して保存
    imageLibrary[imageId] = ViewPort::fromImage(img);
}

void NodeGraphEvaluator::setNodes(const std::vector<GraphNode>& newNodes) {
    nodes = newNodes;
}

void NodeGraphEvaluator::setConnections(const std::vector<GraphConnection>& newConnections) {
    connections = newConnections;
}

// ノードを再帰的に評価
ViewPort NodeGraphEvaluator::evaluateNode(const std::string& nodeId, std::set<std::string>& visited) {
    // 循環参照チェック
    if (visited.find(nodeId) != visited.end()) {
        return ViewPort(canvasWidth, canvasHeight, PixelFormatIDs::RGBA16_Premultiplied);  // 空画像
    }
    visited.insert(nodeId);

    // キャッシュチェック
    auto cacheIt = nodeResultCache.find(nodeId);
    if (cacheIt != nodeResultCache.end()) {
        return cacheIt->second;
    }

    // ノードを検索
    const GraphNode* node = nullptr;
    for (const auto& n : nodes) {
        if (n.id == nodeId) {
            node = &n;
            break;
        }
    }

    if (!node) {
        return ViewPort(canvasWidth, canvasHeight, PixelFormatIDs::RGBA16_Premultiplied);  // 空画像
    }

    ViewPort result(canvasWidth, canvasHeight, PixelFormatIDs::RGBA16_Premultiplied);

    if (node->type == "image") {
        if (node->imageId >= 0) {
            auto it = imageLibrary.find(node->imageId);
            if (it != imageLibrary.end()) {
                // imageLibrary は既に ViewPort(RGBA8_Straight) なのでそのまま使用
                result = it->second;
            }
        }

    } else if (node->type == "filter") {
        // フィルタノード: 入力画像にフィルタを適用
        // 入力接続を検索
        const GraphConnection* inputConn = nullptr;
        for (const auto& conn : connections) {
            if (conn.toNodeId == nodeId && conn.toPort == "in") {
                inputConn = &conn;
                break;
            }
        }

        if (inputConn) {
            ViewPort inputImage = evaluateNode(inputConn->fromNodeId, visited);

            std::string filterType;
            float filterParam;

            if (node->independent) {
                // 独立フィルタノード
                filterType = node->filterType;
                filterParam = node->filterParam;
            } else {
                // 非独立フィルタノード（未使用：スキップ）
                result = inputImage;
                nodeResultCache[nodeId] = result;
                return result;
            }

            result = processor.applyFilter(inputImage, filterType, filterParam);
        }

    } else if (node->type == "composite") {
        // 合成ノード: 可変長入力を合成
        std::vector<ViewPort> images;
        std::vector<const ViewPort*> imagePtrs;

        // 動的な入力配列を使用
        for (const auto& input : node->compositeInputs) {
            // この入力ポートへの接続を検索
            for (const auto& conn : connections) {
                if (conn.toNodeId == nodeId && conn.toPort == input.id) {
                    ViewPort img = evaluateNode(conn.fromNodeId, visited);

                    // 合成にはPremultiplied形式が必要
                    if (img.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                        img = processor.convertPixelFormat(img, PixelFormatIDs::RGBA16_Premultiplied);
                    }

                    // アルファ値を適用（行ごとにアクセス、ストライド考慮）
                    if (input.alpha != 1.0) {
                        uint16_t alphaU16 = static_cast<uint16_t>(input.alpha * 65535);
                        for (int y = 0; y < img.height; y++) {
                            uint16_t* row = img.getPixelPtr<uint16_t>(0, y);
                            for (int x = 0; x < img.width * 4; x++) {
                                row[x] = (row[x] * alphaU16) >> 16;
                            }
                        }
                    }

                    images.push_back(std::move(img));
                    break;
                }
            }
        }

        // ポインタ配列を作成
        for (auto& img : images) {
            imagePtrs.push_back(&img);
        }

        // 合成
        if (imagePtrs.size() == 1) {
            result = images[0];
        } else if (imagePtrs.size() > 1) {
            result = processor.mergeImages(imagePtrs);
        }

        // 合成ノードのアフィン変換
        const AffineParams& p = node->compositeTransform;
        const bool needsTransform = (
            p.translateX != 0 || p.translateY != 0 || p.rotation != 0 ||
            p.scaleX != 1.0 || p.scaleY != 1.0
        );

        if (needsTransform) {
            double centerX = canvasWidth / 2.0;
            double centerY = canvasHeight / 2.0;
            AffineMatrix matrix = AffineMatrix::fromParams(p, centerX, centerY);
            result = processor.applyTransform(result, matrix);
        }

    } else if (node->type == "affine") {
        // アフィン変換ノード: 入力画像にアフィン変換を適用
        // 入力接続を検索
        const GraphConnection* inputConn = nullptr;
        for (const auto& conn : connections) {
            if (conn.toNodeId == nodeId && conn.toPort == "in") {
                inputConn = &conn;
                break;
            }
        }

        if (inputConn) {
            ViewPort inputImage = evaluateNode(inputConn->fromNodeId, visited);

            // アフィン変換にはPremultiplied形式が必要
            if (inputImage.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                inputImage = processor.convertPixelFormat(inputImage, PixelFormatIDs::RGBA16_Premultiplied);
            }

            // 変換行列を取得
            AffineMatrix matrix;
            if (node->matrixMode) {
                // 行列モード: 直接指定された行列を使用
                matrix = node->affineMatrix;
            } else {
                // パラメータモード: パラメータから行列を生成
                // 元画像ではなくキャンバスの中心を回転軸にする
                double centerX = canvasWidth / 2.0;
                double centerY = canvasHeight / 2.0;
                matrix = AffineMatrix::fromParams(node->affineParams, centerX, centerY);
            }

            // アフィン変換を適用（アフィン変換ノード自体はalphaを持たない）
            result = processor.applyTransform(inputImage, matrix);
        }
    }

    // キャッシュに保存
    nodeResultCache[nodeId] = result;
    return result;
}

// ノードグラフ全体を評価（1回のWASM呼び出しで完結）
Image NodeGraphEvaluator::evaluateGraph() {
    // ノード評価キャッシュをクリア
    nodeResultCache.clear();

    // 出力ノードを検索
    const GraphNode* outputNode = nullptr;
    for (const auto& node : nodes) {
        if (node.type == "output") {
            outputNode = &node;
            break;
        }
    }

    if (!outputNode) {
        return Image(canvasWidth, canvasHeight);  // 空画像
    }

    // 出力ノードへの入力接続を検索
    const GraphConnection* inputConn = nullptr;
    for (const auto& conn : connections) {
        if (conn.toNodeId == outputNode->id && conn.toPort == "in") {
            inputConn = &conn;
            break;
        }
    }

    if (!inputConn) {
        return Image(canvasWidth, canvasHeight);  // 空画像
    }

    // グラフを評価
    std::set<std::string> visited;
    ViewPort resultViewPort = evaluateNode(inputConn->fromNodeId, visited);

    // ViewPort → 8bit Image変換（ViewPortのtoImage()を使用）
    return resultViewPort.toImage();
}

} // namespace ImageTransform
