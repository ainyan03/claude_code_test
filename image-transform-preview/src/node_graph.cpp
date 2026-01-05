#include "node_graph.h"
#include "image_types.h"
#include <cmath>

namespace ImageTransform {

// ========================================================================
// NodeGraphEvaluator実装
// ========================================================================

NodeGraphEvaluator::NodeGraphEvaluator(int width, int height)
    : canvasWidth(width), canvasHeight(height),
      dstOriginX(width / 2.0), dstOriginY(height / 2.0),  // デフォルトはキャンバス中央
      processor(width, height) {}

void NodeGraphEvaluator::setCanvasSize(int width, int height) {
    canvasWidth = width;
    canvasHeight = height;
    // dstOrigin も更新（デフォルトは中央）
    dstOriginX = width / 2.0;
    dstOriginY = height / 2.0;
    processor.setCanvasSize(width, height);
}

void NodeGraphEvaluator::setDstOrigin(double x, double y) {
    dstOriginX = x;
    dstOriginY = y;
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
                // 正規化座標をピクセル座標に変換して設定
                // node->srcOriginX/Y は 0.0〜1.0 の正規化座標
                result.srcOriginX = node->srcOriginX * result.width;
                result.srcOriginY = node->srcOriginY * result.height;
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
            std::vector<float> filterParams;

            if (node->independent) {
                // 独立フィルタノード
                filterType = node->filterType;
                filterParams = node->filterParams;
            } else {
                // 非独立フィルタノード（未使用：スキップ）
                result = inputImage;
                nodeResultCache[nodeId] = result;
                return result;
            }

            auto filterStart = std::chrono::high_resolution_clock::now();
            result = processor.applyFilter(inputImage, filterType, filterParams);
            auto filterEnd = std::chrono::high_resolution_clock::now();
            perfMetrics.filterTime += std::chrono::duration<double, std::milli>(filterEnd - filterStart).count();
            perfMetrics.filterCount++;
            // フィルタは画像サイズを変えないので srcOrigin をそのまま継承
            result.srcOriginX = inputImage.srcOriginX;
            result.srcOriginY = inputImage.srcOriginY;
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
                        auto convStart = std::chrono::high_resolution_clock::now();
                        img = processor.convertPixelFormat(img, PixelFormatIDs::RGBA16_Premultiplied);
                        auto convEnd = std::chrono::high_resolution_clock::now();
                        perfMetrics.convertTime += std::chrono::duration<double, std::milli>(convEnd - convStart).count();
                        perfMetrics.convertCount++;
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
        // mergeImages は各画像の srcOrigin を基準点（dstOrigin）に揃えて配置し、
        // 結果の srcOrigin を基準点に設定する
        // 単一入力でも原点処理が必要なため、常に mergeImages を使用
        if (imagePtrs.size() >= 1) {
            auto compStart = std::chrono::high_resolution_clock::now();
            result = processor.mergeImages(imagePtrs, dstOriginX, dstOriginY);
            auto compEnd = std::chrono::high_resolution_clock::now();
            perfMetrics.compositeTime += std::chrono::duration<double, std::milli>(compEnd - compStart).count();
            perfMetrics.compositeCount++;
            // mergeImages が srcOrigin を設定済み（dstOrigin）
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
                auto convStart = std::chrono::high_resolution_clock::now();
                inputImage = processor.convertPixelFormat(inputImage, PixelFormatIDs::RGBA16_Premultiplied);
                auto convEnd = std::chrono::high_resolution_clock::now();
                perfMetrics.convertTime += std::chrono::duration<double, std::milli>(convEnd - convStart).count();
                perfMetrics.convertCount++;
            }

            // 変換行列を取得（JS側で行列に統一済み）
            AffineMatrix matrix = node->affineMatrix;

            // 原点座標（変換の中心点）- 入力座標系での位置
            double inputOriginX = inputImage.srcOriginX;
            double inputOriginY = inputImage.srcOriginY;
            // 出力座標系での原点位置（四隅オフセット適用後に更新）
            double outputOriginX = inputOriginX;
            double outputOriginY = inputOriginY;

            // 負の座標を避けるためのオフセットを出力座標系で適用
            // matrix.tx/tyには加算しない（逆行列計算に影響し原点が不安定になるため）
            // 画像サイズに基づく固定オフセットを使用（動的計算は揺らぎの原因となる）
            double fixedOffset = std::max(inputImage.width, inputImage.height);
            double outputOffsetX = fixedOffset;
            double outputOffsetY = fixedOffset;
            outputOriginX += fixedOffset;
            outputOriginY += fixedOffset;
            auto affineStart = std::chrono::high_resolution_clock::now();
            result = processor.applyTransform(inputImage, matrix, inputOriginX, inputOriginY, outputOffsetX, outputOffsetY);
            auto affineEnd = std::chrono::high_resolution_clock::now();
            perfMetrics.affineTime += std::chrono::duration<double, std::milli>(affineEnd - affineStart).count();
            perfMetrics.affineCount++;

            // srcOrigin を更新（出力座標系での原点位置）
            result.srcOriginX = outputOriginX;
            result.srcOriginY = outputOriginY;
        }
    }

    // キャッシュに保存
    nodeResultCache[nodeId] = result;
    return result;
}

// ノードグラフ全体を評価（1回のWASM呼び出しで完結）
Image NodeGraphEvaluator::evaluateGraph() {
    // パフォーマンス計測をリセット
    perfMetrics.reset();

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

    // srcOrigin が dstOrigin と一致しない場合、最終配置を適用
    // （合成ノードを経由した場合は既に適用済みなのでスキップ）
    const double epsilon = 0.001;
    if (std::abs(resultViewPort.srcOriginX - dstOriginX) > epsilon ||
        std::abs(resultViewPort.srcOriginY - dstOriginY) > epsilon) {
        // Premultiplied形式に変換（必要な場合）
        if (resultViewPort.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
            auto convStart = std::chrono::high_resolution_clock::now();
            resultViewPort = processor.convertPixelFormat(resultViewPort, PixelFormatIDs::RGBA16_Premultiplied);
            auto convEnd = std::chrono::high_resolution_clock::now();
            perfMetrics.convertTime += std::chrono::duration<double, std::milli>(convEnd - convStart).count();
            perfMetrics.convertCount++;
        }
        // dstOrigin に基づいて配置
        std::vector<const ViewPort*> singleImage = { &resultViewPort };
        auto compStart = std::chrono::high_resolution_clock::now();
        resultViewPort = processor.mergeImages(singleImage, dstOriginX, dstOriginY);
        auto compEnd = std::chrono::high_resolution_clock::now();
        perfMetrics.compositeTime += std::chrono::duration<double, std::milli>(compEnd - compStart).count();
        perfMetrics.compositeCount++;
    }

    // ViewPort → 8bit Image変換（ViewPortのtoImage()を使用）
    auto outputStart = std::chrono::high_resolution_clock::now();
    Image result = resultViewPort.toImage();
    auto outputEnd = std::chrono::high_resolution_clock::now();
    perfMetrics.outputTime = std::chrono::duration<double, std::milli>(outputEnd - outputStart).count();

    return result;
}

} // namespace ImageTransform
