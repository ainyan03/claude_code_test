#include "node_graph.h"

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
    // キャンバスサイズ変更時はpremultipliedキャッシュをクリア
    layerPremulCache.clear();
}

void NodeGraphEvaluator::setLayerImage(int layerId, const Image& img) {
    layerImages[layerId] = img;
    // 画像更新時は該当レイヤーのキャッシュをクリア
    layerPremulCache.erase(layerId);
}

void NodeGraphEvaluator::setNodes(const std::vector<GraphNode>& newNodes) {
    nodes = newNodes;
}

void NodeGraphEvaluator::setConnections(const std::vector<GraphConnection>& newConnections) {
    connections = newConnections;
}

// レイヤー画像のpremultiplied変換（変換パラメータ付き、キャッシュ使用）
Image16 NodeGraphEvaluator::getLayerPremultiplied(int layerId, const AffineParams& transform) {
    // レイヤー画像を取得
    auto it = layerImages.find(layerId);
    if (it == layerImages.end()) {
        return Image16(canvasWidth, canvasHeight);  // 空画像
    }

    const Image& img = it->second;

    // premultiplied変換（キャッシュを使用）
    Image16 premul;
    auto cacheIt = layerPremulCache.find(layerId);
    if (cacheIt != layerPremulCache.end()) {
        premul = cacheIt->second;
    } else {
        premul = processor.toPremultiplied(img);
        layerPremulCache[layerId] = premul;
    }

    // アフィン変換が必要かチェック
    const bool needsTransform = (
        transform.translateX != 0 || transform.translateY != 0 ||
        transform.rotation != 0 || transform.scaleX != 1.0 ||
        transform.scaleY != 1.0 || transform.alpha != 1.0
    );

    if (needsTransform) {
        // 元画像の中心を回転軸にする
        const Image& originalImg = layerImages.at(layerId);
        double centerX = originalImg.width / 2.0;
        double centerY = originalImg.height / 2.0;
        AffineMatrix matrix = AffineMatrix::fromParams(transform, centerX, centerY);
        return processor.applyTransformToImage16(premul, matrix, transform.alpha);
    }

    return premul;
}

// ノードを再帰的に評価
Image16 NodeGraphEvaluator::evaluateNode(const std::string& nodeId, std::set<std::string>& visited) {
    // 循環参照チェック
    if (visited.find(nodeId) != visited.end()) {
        return Image16(canvasWidth, canvasHeight);  // 空画像
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
        return Image16(canvasWidth, canvasHeight);  // 空画像
    }

    Image16 result(canvasWidth, canvasHeight);

    if (node->type == "image") {
        // 新形式: imageId + alpha（画像ライブラリ対応）
        if (node->imageId >= 0) {
            auto it = layerImages.find(node->imageId);
            if (it != layerImages.end()) {
                // premultiplied変換時にalphaを適用
                result = processor.toPremultiplied(it->second, node->imageAlpha);
            }
        }
        // 旧形式: layerId + transform（後方互換性）
        else if (node->layerId >= 0) {
            result = getLayerPremultiplied(node->layerId, node->transform);
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
            Image16 inputImage = evaluateNode(inputConn->fromNodeId, visited);

            std::string filterType;
            float filterParam;

            if (node->independent) {
                // 独立フィルタノード
                filterType = node->filterType;
                filterParam = node->filterParam;
            } else {
                // レイヤー付帯フィルタノード（未実装：簡略化のためスキップ）
                result = inputImage;
                nodeResultCache[nodeId] = result;
                return result;
            }

            result = processor.applyFilterToImage16(inputImage, filterType, filterParam);
        }

    } else if (node->type == "composite") {
        // 合成ノード: 可変長入力を合成
        std::vector<Image16> images;
        std::vector<const Image16*> imagePtrs;

        // 動的な入力配列を使用（compositeInputsが空の場合は旧形式の互換性処理）
        if (!node->compositeInputs.empty()) {
            // 新形式: 動的な入力配列
            for (const auto& input : node->compositeInputs) {
                // この入力ポートへの接続を検索
                for (const auto& conn : connections) {
                    if (conn.toNodeId == nodeId && conn.toPort == input.id) {
                        Image16 img = evaluateNode(conn.fromNodeId, visited);

                        // ★Phase 4: 合成にはPremultiplied形式が必要
                        if (img.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                            img = processor.convertPixelFormat(img, PixelFormatIDs::RGBA16_Premultiplied);
                        }

                        // アルファ値を適用
                        if (input.alpha != 1.0) {
                            uint16_t alphaU16 = static_cast<uint16_t>(input.alpha * 65535);
                            for (size_t i = 0; i < img.data.size(); i++) {
                                img.data[i] = (img.data[i] * alphaU16) >> 16;
                            }
                        }

                        images.push_back(std::move(img));
                        break;
                    }
                }
            }
        } else {
            // 旧形式: alpha1, alpha2（後方互換性）
            // 入力1を取得
            for (const auto& conn : connections) {
                if (conn.toNodeId == nodeId && conn.toPort == "in1") {
                    Image16 img1 = evaluateNode(conn.fromNodeId, visited);

                    // ★Phase 4: 合成にはPremultiplied形式が必要
                    if (img1.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                        img1 = processor.convertPixelFormat(img1, PixelFormatIDs::RGBA16_Premultiplied);
                    }

                    // alpha1を適用
                    if (node->alpha1 != 1.0) {
                        uint16_t alphaU16 = static_cast<uint16_t>(node->alpha1 * 65535);
                        for (size_t i = 0; i < img1.data.size(); i++) {
                            img1.data[i] = (img1.data[i] * alphaU16) >> 16;
                        }
                    }

                    images.push_back(std::move(img1));
                    break;
                }
            }

            // 入力2を取得
            for (const auto& conn : connections) {
                if (conn.toNodeId == nodeId && conn.toPort == "in2") {
                    Image16 img2 = evaluateNode(conn.fromNodeId, visited);

                    // ★Phase 4: 合成にはPremultiplied形式が必要
                    if (img2.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                        img2 = processor.convertPixelFormat(img2, PixelFormatIDs::RGBA16_Premultiplied);
                    }

                    // alpha2を適用
                    if (node->alpha2 != 1.0) {
                        uint16_t alphaU16 = static_cast<uint16_t>(node->alpha2 * 65535);
                        for (size_t i = 0; i < img2.data.size(); i++) {
                            img2.data[i] = (img2.data[i] * alphaU16) >> 16;
                        }
                    }

                    images.push_back(std::move(img2));
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
            result = processor.mergeImages16(imagePtrs);
        }

        // 合成ノードのアフィン変換
        const AffineParams& p = node->compositeTransform;
        const bool needsTransform = (
            p.translateX != 0 || p.translateY != 0 || p.rotation != 0 ||
            p.scaleX != 1.0 || p.scaleY != 1.0 || p.alpha != 1.0
        );

        if (needsTransform) {
            double centerX = canvasWidth / 2.0;
            double centerY = canvasHeight / 2.0;
            AffineMatrix matrix = AffineMatrix::fromParams(p, centerX, centerY);
            result = processor.applyTransformToImage16(result, matrix, p.alpha);
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
            Image16 inputImage = evaluateNode(inputConn->fromNodeId, visited);

            // ★Phase 4: アフィン変換にはPremultiplied形式が必要
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

            // アフィン変換を適用（alpha=1.0、アフィン変換ノード自体はalphaを持たない）
            result = processor.applyTransformToImage16(inputImage, matrix, 1.0);
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

    // グラフを評価（16bit premultiplied）
    std::set<std::string> visited;
    Image16 result16 = evaluateNode(inputConn->fromNodeId, visited);

    // 16bit → 8bit変換
    return processor.fromPremultiplied(result16);
}

} // namespace ImageTransform
