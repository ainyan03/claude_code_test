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

void NodeGraphEvaluator::setTileStrategy(TileStrategy strategy, int tileWidth, int tileHeight) {
    tileStrategy = strategy;
    customTileWidth = tileWidth;
    customTileHeight = tileHeight;
}

// ========================================================================
// ユーティリティ関数
// ========================================================================

const GraphNode* NodeGraphEvaluator::findOutputNode() const {
    for (const auto& node : nodes) {
        if (node.type == "output") {
            return &node;
        }
    }
    return nullptr;
}

const GraphConnection* NodeGraphEvaluator::findInputConnection(
    const std::string& nodeId, const std::string& portName) const {
    for (const auto& conn : connections) {
        if (conn.toNodeId == nodeId && conn.toPort == portName) {
            return &conn;
        }
    }
    return nullptr;
}

// ========================================================================
// 段階0: 事前準備（prepare）
// ========================================================================

void NodeGraphEvaluator::prepare(const RenderContext& context) {
    // キャッシュをクリア
    affinePreparedCache.clear();
    filterPreparedCache.clear();
    nodeRequestCache.clear();
    nodeResultCache.clear();

    // 出力ノードを検索
    const GraphNode* outputNode = findOutputNode();
    if (!outputNode) return;

    // 出力ノードへの入力接続を検索
    const GraphConnection* inputConn = findInputConnection(outputNode->id, "in");
    if (!inputConn) return;

    // 再帰的に事前準備を実行
    std::set<std::string> visited;
    prepareNode(inputConn->fromNodeId, context, visited);
}

void NodeGraphEvaluator::prepareNode(const std::string& nodeId,
                                     const RenderContext& context,
                                     std::set<std::string>& visited) {
    // 循環参照チェック
    if (visited.find(nodeId) != visited.end()) return;
    visited.insert(nodeId);

    // ノードを検索
    const GraphNode* node = nullptr;
    for (const auto& n : nodes) {
        if (n.id == nodeId) {
            node = &n;
            break;
        }
    }
    if (!node) return;

    // 入力ノードを先に準備（再帰）
    if (node->type == "filter" || node->type == "affine") {
        const GraphConnection* inputConn = findInputConnection(nodeId, "in");
        if (inputConn) {
            prepareNode(inputConn->fromNodeId, context, visited);
        }
    } else if (node->type == "composite") {
        for (const auto& input : node->compositeInputs) {
            const GraphConnection* conn = findInputConnection(nodeId, input.id);
            if (conn) {
                prepareNode(conn->fromNodeId, context, visited);
            }
        }
    }

    // ノードタイプ別の事前計算
    if (node->type == "affine") {
        AffinePreparedData data;

        // 逆行列を計算
        const AffineMatrix& matrix = node->affineMatrix;
        double det = matrix.a * matrix.d - matrix.b * matrix.c;
        if (std::abs(det) < 1e-10) {
            data.prepared = false;
            affinePreparedCache[nodeId] = data;
            return;
        }

        double invDet = 1.0 / det;
        double invA = matrix.d * invDet;
        double invB = -matrix.b * invDet;
        double invC = -matrix.c * invDet;
        double invD = matrix.a * invDet;
        double invTx = (-matrix.d * matrix.tx + matrix.b * matrix.ty) * invDet;
        double invTy = (matrix.c * matrix.tx - matrix.a * matrix.ty) * invDet;

        // 固定小数点に変換
        constexpr int FIXED_POINT_BITS = 16;
        constexpr int32_t FIXED_POINT_SCALE = 1 << FIXED_POINT_BITS;

        data.fixedInvA = std::lround(invA * FIXED_POINT_SCALE);
        data.fixedInvB = std::lround(invB * FIXED_POINT_SCALE);
        data.fixedInvC = std::lround(invC * FIXED_POINT_SCALE);
        data.fixedInvD = std::lround(invD * FIXED_POINT_SCALE);
        data.fixedInvTx = std::lround(invTx * FIXED_POINT_SCALE);
        data.fixedInvTy = std::lround(invTy * FIXED_POINT_SCALE);

        // 入力画像情報は評価時に設定
        data.prepared = true;
        affinePreparedCache[nodeId] = data;

    } else if (node->type == "filter") {
        FilterPreparedData data;
        data.filterType = node->filterType;
        data.params = node->filterParams;

        // フィルタタイプごとのカーネル半径を設定
        if (node->filterType == "boxblur" && !node->filterParams.empty()) {
            data.kernelRadius = static_cast<int>(node->filterParams[0]);
        } else {
            data.kernelRadius = 0;
        }

        data.prepared = true;
        filterPreparedCache[nodeId] = data;
    }
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
        // アフィン変換ノード
        const GraphConnection* inputConn = nullptr;
        for (const auto& conn : connections) {
            if (conn.toNodeId == nodeId && conn.toPort == "in") {
                inputConn = &conn;
                break;
            }
        }

        if (inputConn) {
            ViewPort inputImage = evaluateNode(inputConn->fromNodeId, visited);

            // Premultiplied形式に変換（アフィン変換に必要）
            if (inputImage.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                auto convStart = std::chrono::high_resolution_clock::now();
                inputImage = processor.convertPixelFormat(inputImage, PixelFormatIDs::RGBA16_Premultiplied);
                auto convEnd = std::chrono::high_resolution_clock::now();
                perfMetrics.convertTime += std::chrono::duration<double, std::milli>(convEnd - convStart).count();
                perfMetrics.convertCount++;
            }

            AffineMatrix matrix = node->affineMatrix;
            double inputOriginX = inputImage.srcOriginX;
            double inputOriginY = inputImage.srcOriginY;

            // 出力オフセット: 負座標を避けるためのマージン
            double outputOffset = std::max(inputImage.width, inputImage.height);

            // 出力サイズ: 入力画像 + オフセット×2 で変換後の画像が確実に収まる
            int outputWidth = inputImage.width + static_cast<int>(outputOffset * 2);
            int outputHeight = inputImage.height + static_cast<int>(outputOffset * 2);

            auto affineStart = std::chrono::high_resolution_clock::now();
            result = processor.applyTransform(inputImage, matrix, inputOriginX, inputOriginY,
                                              outputOffset, outputOffset, outputWidth, outputHeight);
            auto affineEnd = std::chrono::high_resolution_clock::now();
            perfMetrics.affineTime += std::chrono::duration<double, std::milli>(affineEnd - affineStart).count();
            perfMetrics.affineCount++;

            // srcOrigin = 入力原点を順変換した位置
            // mergeImages() で dstOrigin に揃えて配置されるため、変換後の原点位置を設定
            result.srcOriginX = matrix.tx + inputOriginX + outputOffset;
            result.srcOriginY = matrix.ty + inputOriginY + outputOffset;
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

    // RenderContextを構築
    RenderContext context;
    context.totalWidth = canvasWidth;
    context.totalHeight = canvasHeight;
    context.originX = dstOriginX;
    context.originY = dstOriginY;
    context.strategy = tileStrategy;
    context.tileWidth = customTileWidth;
    context.tileHeight = customTileHeight;

    // 段階0: 事前準備（逆行列計算等）
    prepare(context);

    // タイル分割なし（従来互換モード）
    if (tileStrategy == TileStrategy::None) {
        // 全体を1つのタイルとして処理
        RenderRequest fullRequest = {
            0, 0, canvasWidth, canvasHeight,
            dstOriginX, dstOriginY
        };

        // 段階1: 要求伝播
        propagateRequests(fullRequest);

        // 段階2: 評価
        ViewPort resultViewPort = evaluateTile(fullRequest);

        // srcOrigin が dstOrigin と一致しない場合、最終配置を適用
        const double epsilon = 0.001;
        if (std::abs(resultViewPort.srcOriginX - dstOriginX) > epsilon ||
            std::abs(resultViewPort.srcOriginY - dstOriginY) > epsilon) {
            if (resultViewPort.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                auto convStart = std::chrono::high_resolution_clock::now();
                resultViewPort = processor.convertPixelFormat(resultViewPort, PixelFormatIDs::RGBA16_Premultiplied);
                auto convEnd = std::chrono::high_resolution_clock::now();
                perfMetrics.convertTime += std::chrono::duration<double, std::milli>(convEnd - convStart).count();
                perfMetrics.convertCount++;
            }
            std::vector<const ViewPort*> singleImage = { &resultViewPort };
            auto compStart = std::chrono::high_resolution_clock::now();
            resultViewPort = processor.mergeImages(singleImage, dstOriginX, dstOriginY);
            auto compEnd = std::chrono::high_resolution_clock::now();
            perfMetrics.compositeTime += std::chrono::duration<double, std::milli>(compEnd - compStart).count();
            perfMetrics.compositeCount++;
        }

        // ViewPort → 8bit Image変換
        auto outputStart = std::chrono::high_resolution_clock::now();
        Image result = resultViewPort.toImage();
        auto outputEnd = std::chrono::high_resolution_clock::now();
        perfMetrics.outputTime = std::chrono::duration<double, std::milli>(outputEnd - outputStart).count();

        return result;
    }

    // タイル分割モード
    Image result(canvasWidth, canvasHeight);
    int tileCountX = context.getTileCountX();
    int tileCountY = context.getTileCountY();

    for (int ty = 0; ty < tileCountY; ty++) {
        for (int tx = 0; tx < tileCountX; tx++) {
            // デバッグ用チェッカーボードモード: 市松模様でスキップ
            if (tileStrategy == TileStrategy::Debug_Checkerboard) {
                if ((tx + ty) % 2 == 1) {
                    continue;  // 奇数タイルはスキップ
                }
            }

            // タイル要求を生成
            RenderRequest tileReq = RenderRequest::fromTile(context, tx, ty);

            // 段階1: 要求伝播
            propagateRequests(tileReq);

            // 段階2: タイル評価
            ViewPort tileResult = evaluateTile(tileReq);

            // srcOrigin が dstOrigin と一致しない場合、配置を適用
            const double epsilon = 0.001;
            if (std::abs(tileResult.srcOriginX - dstOriginX) > epsilon ||
                std::abs(tileResult.srcOriginY - dstOriginY) > epsilon) {
                if (tileResult.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                    tileResult = processor.convertPixelFormat(tileResult, PixelFormatIDs::RGBA16_Premultiplied);
                }
                std::vector<const ViewPort*> singleImage = { &tileResult };
                tileResult = processor.mergeImages(singleImage, dstOriginX, dstOriginY);
            }

            // タイル結果を最終画像にコピー
            // tileResult は canvasSize のViewPort、その中の tileReq 領域をコピー
            if (tileResult.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                tileResult = processor.convertPixelFormat(tileResult, PixelFormatIDs::RGBA16_Premultiplied);
            }

            for (int y = 0; y < tileReq.height && (tileReq.y + y) < canvasHeight; y++) {
                int dstY = tileReq.y + y;
                if (dstY < 0 || dstY >= canvasHeight) continue;

                const uint16_t* srcRow = tileResult.getPixelPtr<uint16_t>(tileReq.x, dstY);
                uint8_t* dstRow = result.data.data() + dstY * canvasWidth * 4 + tileReq.x * 4;

                for (int x = 0; x < tileReq.width && (tileReq.x + x) < canvasWidth; x++) {
                    // 16bit Premultiplied → 8bit Straight 変換
                    uint16_t r16 = srcRow[x * 4 + 0];
                    uint16_t g16 = srcRow[x * 4 + 1];
                    uint16_t b16 = srcRow[x * 4 + 2];
                    uint16_t a16 = srcRow[x * 4 + 3];

                    if (a16 > 0) {
                        uint32_t r_unpre = ((uint32_t)r16 * 65535) / a16;
                        uint32_t g_unpre = ((uint32_t)g16 * 65535) / a16;
                        uint32_t b_unpre = ((uint32_t)b16 * 65535) / a16;
                        dstRow[x * 4 + 0] = std::min(r_unpre >> 8, 255u);
                        dstRow[x * 4 + 1] = std::min(g_unpre >> 8, 255u);
                        dstRow[x * 4 + 2] = std::min(b_unpre >> 8, 255u);
                    } else {
                        dstRow[x * 4 + 0] = 0;
                        dstRow[x * 4 + 1] = 0;
                        dstRow[x * 4 + 2] = 0;
                    }
                    dstRow[x * 4 + 3] = a16 >> 8;
                }
            }
        }
    }

    return result;
}

// ========================================================================
// 段階1: 要求伝播（propagateRequests）
// ========================================================================

void NodeGraphEvaluator::propagateRequests(const RenderRequest& tileRequest) {
    // 要求キャッシュをクリア（タイルごとに再計算）
    nodeRequestCache.clear();

    // 出力ノードを検索
    const GraphNode* outputNode = findOutputNode();
    if (!outputNode) return;

    // 出力ノードへの入力接続を検索
    const GraphConnection* inputConn = findInputConnection(outputNode->id, "in");
    if (!inputConn) return;

    // 再帰的に要求を伝播
    std::set<std::string> visited;
    propagateNodeRequest(inputConn->fromNodeId, tileRequest, visited);
}

RenderRequest NodeGraphEvaluator::propagateNodeRequest(
    const std::string& nodeId,
    const RenderRequest& outputRequest,
    std::set<std::string>& visited) {

    // 循環参照チェック
    if (visited.find(nodeId) != visited.end()) {
        return RenderRequest{};  // 空の要求
    }
    visited.insert(nodeId);

    // ノードを検索
    const GraphNode* node = nullptr;
    for (const auto& n : nodes) {
        if (n.id == nodeId) {
            node = &n;
            break;
        }
    }
    if (!node) return RenderRequest{};

    // このノードの出力要求を保存
    nodeRequestCache[nodeId] = outputRequest;

    RenderRequest inputRequest = outputRequest;

    if (node->type == "image") {
        // 画像ノード: 画像の実サイズと要求の交差を計算
        auto it = imageLibrary.find(node->imageId);
        if (it != imageLibrary.end()) {
            const ViewPort& img = it->second;

            // 画像のローカル座標系での領域
            // srcOrigin を基準に配置されるため、要求座標を画像座標に変換
            double imgOriginX = node->srcOriginX * img.width;
            double imgOriginY = node->srcOriginY * img.height;

            // 出力要求を画像ローカル座標に変換
            // outputRequest は dstOrigin 基準、画像は srcOrigin 基準
            int localX = outputRequest.x - static_cast<int>(outputRequest.originX - imgOriginX);
            int localY = outputRequest.y - static_cast<int>(outputRequest.originY - imgOriginY);

            RenderRequest imageRect = {
                0, 0, img.width, img.height,
                imgOriginX, imgOriginY
            };

            RenderRequest localRequest = {
                localX, localY,
                outputRequest.width, outputRequest.height,
                imgOriginX, imgOriginY
            };

            inputRequest = localRequest.intersect(imageRect);
        }

    } else if (node->type == "filter") {
        // フィルタノード: カーネル半径分拡大して上流に伝播
        auto it = filterPreparedCache.find(nodeId);
        int margin = (it != filterPreparedCache.end()) ? it->second.kernelRadius : 0;

        RenderRequest expandedRequest = outputRequest.expand(margin);

        const GraphConnection* inputConn = findInputConnection(nodeId, "in");
        if (inputConn) {
            inputRequest = propagateNodeRequest(inputConn->fromNodeId, expandedRequest, visited);
        }

    } else if (node->type == "composite") {
        // 合成ノード: 各入力に同じ要求を伝播
        for (const auto& input : node->compositeInputs) {
            const GraphConnection* conn = findInputConnection(nodeId, input.id);
            if (conn) {
                propagateNodeRequest(conn->fromNodeId, outputRequest, visited);
            }
        }

    } else if (node->type == "affine") {
        // アフィン変換ノード: 出力要求を逆変換して入力要求を算出
        auto it = affinePreparedCache.find(nodeId);
        if (it != affinePreparedCache.end() && it->second.prepared) {
            const AffinePreparedData& prep = it->second;

            // 出力要求の4頂点を逆変換してAABBを算出
            constexpr int FIXED_POINT_BITS = 16;
            constexpr int32_t FIXED_POINT_SCALE = 1 << FIXED_POINT_BITS;

            // 出力矩形の4頂点（基準座標からの相対位置）
            double corners[4][2] = {
                {outputRequest.x - outputRequest.originX, outputRequest.y - outputRequest.originY},
                {outputRequest.x + outputRequest.width - outputRequest.originX, outputRequest.y - outputRequest.originY},
                {outputRequest.x - outputRequest.originX, outputRequest.y + outputRequest.height - outputRequest.originY},
                {outputRequest.x + outputRequest.width - outputRequest.originX, outputRequest.y + outputRequest.height - outputRequest.originY}
            };

            double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;

            for (int i = 0; i < 4; i++) {
                // 固定小数点で逆変換
                int32_t relX = std::lround(corners[i][0] * FIXED_POINT_SCALE);
                int32_t relY = std::lround(corners[i][1] * FIXED_POINT_SCALE);

                int64_t srcX = ((int64_t)prep.fixedInvA * relX + (int64_t)prep.fixedInvB * relY) >> FIXED_POINT_BITS;
                int64_t srcY = ((int64_t)prep.fixedInvC * relX + (int64_t)prep.fixedInvD * relY) >> FIXED_POINT_BITS;

                srcX += prep.fixedInvTx;
                srcY += prep.fixedInvTy;

                double sx = srcX / (double)FIXED_POINT_SCALE;
                double sy = srcY / (double)FIXED_POINT_SCALE;

                minX = std::min(minX, sx);
                minY = std::min(minY, sy);
                maxX = std::max(maxX, sx);
                maxY = std::max(maxY, sy);
            }

            // 入力要求を構築（入力画像の srcOrigin 基準）
            inputRequest = {
                static_cast<int>(std::floor(minX)),
                static_cast<int>(std::floor(minY)),
                static_cast<int>(std::ceil(maxX) - std::floor(minX)) + 1,
                static_cast<int>(std::ceil(maxY) - std::floor(minY)) + 1,
                0, 0  // 入力の原点は後で設定
            };
        }

        const GraphConnection* inputConn = findInputConnection(nodeId, "in");
        if (inputConn) {
            inputRequest = propagateNodeRequest(inputConn->fromNodeId, inputRequest, visited);
        }
    }

    return inputRequest;
}

// ========================================================================
// 段階2: タイル評価（evaluateTile）
// ========================================================================

ViewPort NodeGraphEvaluator::evaluateTile(const RenderRequest& tileRequest) {
    // 評価結果キャッシュをクリア
    nodeResultCache.clear();

    // 出力ノードを検索
    const GraphNode* outputNode = findOutputNode();
    if (!outputNode) {
        return ViewPort(tileRequest.width, tileRequest.height, PixelFormatIDs::RGBA16_Premultiplied);
    }

    // 出力ノードへの入力接続を検索
    const GraphConnection* inputConn = findInputConnection(outputNode->id, "in");
    if (!inputConn) {
        return ViewPort(tileRequest.width, tileRequest.height, PixelFormatIDs::RGBA16_Premultiplied);
    }

    // 再帰的に評価
    std::set<std::string> visited;
    ViewPort result = evaluateNodeWithRequest(inputConn->fromNodeId, visited);

    // 結果をタイルサイズにクリップ（必要な場合）
    // TODO: 実装

    return result;
}

ViewPort NodeGraphEvaluator::evaluateNodeWithRequest(
    const std::string& nodeId,
    std::set<std::string>& visited) {

    // 循環参照チェック
    if (visited.find(nodeId) != visited.end()) {
        auto reqIt = nodeRequestCache.find(nodeId);
        int w = reqIt != nodeRequestCache.end() ? reqIt->second.width : 1;
        int h = reqIt != nodeRequestCache.end() ? reqIt->second.height : 1;
        return ViewPort(w, h, PixelFormatIDs::RGBA16_Premultiplied);
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

    // 要求を取得
    auto reqIt = nodeRequestCache.find(nodeId);
    RenderRequest request = reqIt != nodeRequestCache.end() ? reqIt->second : RenderRequest{};

    if (!node || request.isEmpty()) {
        return ViewPort(1, 1, PixelFormatIDs::RGBA16_Premultiplied);
    }

    ViewPort result(request.width, request.height, PixelFormatIDs::RGBA16_Premultiplied);

    if (node->type == "image") {
        // 画像ノード: 要求領域の画像データを返す
        if (node->imageId >= 0) {
            auto it = imageLibrary.find(node->imageId);
            if (it != imageLibrary.end()) {
                // TODO: 要求領域のみをコピーする最適化
                result = it->second;
                result.srcOriginX = node->srcOriginX * result.width;
                result.srcOriginY = node->srcOriginY * result.height;
            }
        }

    } else if (node->type == "filter") {
        // フィルタノード
        const GraphConnection* inputConn = findInputConnection(nodeId, "in");
        if (inputConn) {
            ViewPort inputImage = evaluateNodeWithRequest(inputConn->fromNodeId, visited);

            auto filterStart = std::chrono::high_resolution_clock::now();
            result = processor.applyFilter(inputImage, node->filterType, node->filterParams);
            auto filterEnd = std::chrono::high_resolution_clock::now();
            perfMetrics.filterTime += std::chrono::duration<double, std::milli>(filterEnd - filterStart).count();
            perfMetrics.filterCount++;

            result.srcOriginX = inputImage.srcOriginX;
            result.srcOriginY = inputImage.srcOriginY;
        }

    } else if (node->type == "composite") {
        // 合成ノード: 従来のロジックを使用
        std::vector<ViewPort> images;
        std::vector<const ViewPort*> imagePtrs;

        for (const auto& input : node->compositeInputs) {
            const GraphConnection* conn = findInputConnection(nodeId, input.id);
            if (conn) {
                ViewPort img = evaluateNodeWithRequest(conn->fromNodeId, visited);

                if (img.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                    img = processor.convertPixelFormat(img, PixelFormatIDs::RGBA16_Premultiplied);
                }

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
            }
        }

        for (auto& img : images) {
            imagePtrs.push_back(&img);
        }

        if (imagePtrs.size() >= 1) {
            auto compStart = std::chrono::high_resolution_clock::now();
            result = processor.mergeImages(imagePtrs, request.originX, request.originY);
            auto compEnd = std::chrono::high_resolution_clock::now();
            perfMetrics.compositeTime += std::chrono::duration<double, std::milli>(compEnd - compStart).count();
            perfMetrics.compositeCount++;
        }

    } else if (node->type == "affine") {
        // アフィン変換ノード
        const GraphConnection* inputConn = findInputConnection(nodeId, "in");
        if (inputConn) {
            ViewPort inputImage = evaluateNodeWithRequest(inputConn->fromNodeId, visited);

            if (inputImage.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                inputImage = processor.convertPixelFormat(inputImage, PixelFormatIDs::RGBA16_Premultiplied);
            }

            AffineMatrix matrix = node->affineMatrix;
            double inputOriginX = inputImage.srcOriginX;
            double inputOriginY = inputImage.srcOriginY;

            // 出力オフセット: 負座標を避けるためのマージン
            double outputOffset = std::max(inputImage.width, inputImage.height);

            // 出力サイズ: 入力画像 + オフセット×2 で変換後の画像が確実に収まる
            int outputWidth = inputImage.width + static_cast<int>(outputOffset * 2);
            int outputHeight = inputImage.height + static_cast<int>(outputOffset * 2);

            auto affineStart = std::chrono::high_resolution_clock::now();
            result = processor.applyTransform(inputImage, matrix, inputOriginX, inputOriginY,
                                              outputOffset, outputOffset, outputWidth, outputHeight);
            auto affineEnd = std::chrono::high_resolution_clock::now();
            perfMetrics.affineTime += std::chrono::duration<double, std::milli>(affineEnd - affineStart).count();
            perfMetrics.affineCount++;

            result.srcOriginX = matrix.tx + inputOriginX + outputOffset;
            result.srcOriginY = matrix.ty + inputOriginY + outputOffset;
        }
    }

    nodeResultCache[nodeId] = result;
    return result;
}

} // namespace ImageTransform
