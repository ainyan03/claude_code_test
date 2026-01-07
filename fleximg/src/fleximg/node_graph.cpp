#include "node_graph.h"
#include "image_types.h"
#include "operators.h"
#include "evaluation_node.h"
#include <cmath>
#include <cstring>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// NodeGraphEvaluator実装
// ========================================================================

NodeGraphEvaluator::NodeGraphEvaluator(int width, int height)
    : canvasWidth(width), canvasHeight(height),
      dstOriginX(width / 2.0f), dstOriginY(height / 2.0f) {}  // デフォルトはキャンバス中央

// デストラクタ（Pipeline の完全な定義が見えるここで実装）
NodeGraphEvaluator::~NodeGraphEvaluator() = default;

void NodeGraphEvaluator::setCanvasSize(int width, int height) {
    canvasWidth = width;
    canvasHeight = height;
    // dstOrigin も更新（デフォルトは中央）
    dstOriginX = width / 2.0f;
    dstOriginY = height / 2.0f;
}

void NodeGraphEvaluator::setDstOrigin(float x, float y) {
    dstOriginX = x;
    dstOriginY = y;
}

void NodeGraphEvaluator::setTileStrategy(TileStrategy strategy, int tileWidth, int tileHeight) {
    tileStrategy = strategy;
    customTileWidth = tileWidth;
    customTileHeight = tileHeight;
}

void NodeGraphEvaluator::registerImage(int imageId, const Image& img) {
    // Image → ViewPort(RGBA8_Straight) に変換して保存
    imageLibrary[imageId] = ViewPort::fromImage(img);
    pipelineDirty_ = true;  // パイプライン再構築が必要（ImageEvalNodeの参照を更新）
}

void NodeGraphEvaluator::setNodes(const std::vector<GraphNode>& newNodes) {
    nodes = newNodes;
    pipelineDirty_ = true;  // パイプライン再構築が必要
}

void NodeGraphEvaluator::setConnections(const std::vector<GraphConnection>& newConnections) {
    connections = newConnections;
    pipelineDirty_ = true;  // パイプライン再構築が必要
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

    // パイプラインベース評価
    return evaluateWithPipeline(context);
}

// ========================================================================
// パイプラインベース評価システム
// ========================================================================

void NodeGraphEvaluator::buildPipelineIfNeeded() {
    if (!pipelineDirty_ && pipeline_) {
        return;  // 再構築不要
    }

    // パイプラインを構築
    Pipeline newPipeline = PipelineBuilder::build(nodes, connections, imageLibrary);

    if (newPipeline.isValid()) {
        pipeline_ = std::make_unique<Pipeline>(std::move(newPipeline));
    } else {
        pipeline_.reset();
    }

    pipelineDirty_ = false;
}

Image NodeGraphEvaluator::evaluateWithPipeline(const RenderContext& context) {
    // パイプラインを構築（必要な場合のみ）
    buildPipelineIfNeeded();

    if (!pipeline_ || !pipeline_->isValid()) {
        // パイプラインが無効な場合は空の画像を返す
        return Image(canvasWidth, canvasHeight);
    }

    // 描画準備（逆行列計算等）
    pipeline_->prepare(context);

    // タイル分割なし（従来互換モード）
    if (tileStrategy == TileStrategy::None) {
        RenderRequest fullRequest = {
            canvasWidth, canvasHeight,
            dstOriginX, dstOriginY
        };

        // パイプラインで評価
        ViewPort resultViewPort = pipeline_->outputNode->evaluate(fullRequest, context);

        // 応答と要求の座標系が一致するか確認
        // - 要求の左上 = 基準から見て -originX の位置
        // - 応答の左上 = srcOriginX の位置
        // - 一致条件: srcOriginX == -originX (かつサイズも一致)
        const float epsilon = 0.001f;
        bool needsComposite = (std::abs(resultViewPort.srcOriginX + fullRequest.originX) > epsilon ||
                               std::abs(resultViewPort.srcOriginY + fullRequest.originY) > epsilon ||
                               resultViewPort.width != canvasWidth ||
                               resultViewPort.height != canvasHeight);
        if (needsComposite) {
            if (resultViewPort.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                auto convStart = std::chrono::high_resolution_clock::now();
                resultViewPort = resultViewPort.convertTo(PixelFormatIDs::RGBA16_Premultiplied);
                auto convEnd = std::chrono::high_resolution_clock::now();
                perfMetrics.convertTime += std::chrono::duration<float, std::milli>(convEnd - convStart).count();
                perfMetrics.convertCount++;
            }
            auto compStart = std::chrono::high_resolution_clock::now();
            auto compositeOp = OperatorFactory::createCompositeOperator();
            RenderRequest compReq{canvasWidth, canvasHeight, dstOriginX, dstOriginY};
            resultViewPort = compositeOp->apply({resultViewPort}, compReq);
            auto compEnd = std::chrono::high_resolution_clock::now();
            perfMetrics.compositeTime += std::chrono::duration<float, std::milli>(compEnd - compStart).count();
            perfMetrics.compositeCount++;
        }

        // ViewPort → 8bit Image変換
        auto outputStart = std::chrono::high_resolution_clock::now();
        Image result = resultViewPort.toImage();
        auto outputEnd = std::chrono::high_resolution_clock::now();
        perfMetrics.outputTime = std::chrono::duration<float, std::milli>(outputEnd - outputStart).count();

        return result;
    }

    // タイル分割モード
    Image result(canvasWidth, canvasHeight);
    int tileCountX = context.getTileCountX();
    int tileCountY = context.getTileCountY();

    for (int ty = 0; ty < tileCountY; ty++) {
        for (int tx = 0; tx < tileCountX; tx++) {
            // デバッグ用チェッカーボードモード
            if (tileStrategy == TileStrategy::Debug_Checkerboard) {
                if ((tx + ty) % 2 == 1) {
                    continue;
                }
            }

            RenderRequest tileReq = RenderRequest::fromTile(context, tx, ty);

            // パイプラインでタイル評価
            ViewPort tileResult = pipeline_->outputNode->evaluate(tileReq, context);

            // 応答と要求の座標系が一致するか確認
            // - 要求の左上 = 基準から見て -originX の位置
            // - 応答の左上 = srcOriginX の位置
            // - 一致条件: srcOriginX == -originX (かつサイズも一致)
            const float epsilon = 0.001f;
            bool needsComposite = (std::abs(tileResult.srcOriginX + tileReq.originX) > epsilon ||
                                   std::abs(tileResult.srcOriginY + tileReq.originY) > epsilon ||
                                   tileResult.width != tileReq.width ||
                                   tileResult.height != tileReq.height);
            if (needsComposite) {
                if (tileResult.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                    tileResult = tileResult.convertTo(PixelFormatIDs::RGBA16_Premultiplied);
                }
                auto compositeOp = OperatorFactory::createCompositeOperator();
                // 合成リクエスト: タイルバッファの originX/Y を使用
                RenderRequest compReq{tileReq.width, tileReq.height, tileReq.originX, tileReq.originY};
                tileResult = compositeOp->apply({tileResult}, compReq);
            }

            // タイル結果を最終画像にコピー（RGBA8_Straightに変換してmemcpy）
            if (tileResult.formatID != PixelFormatIDs::RGBA8_Straight) {
                tileResult = tileResult.convertTo(PixelFormatIDs::RGBA8_Straight);
            }

            // タイルのキャンバス上の位置を計算
            // originX = context.originX - tileLeft なので、tileLeft = context.originX - originX
            int tileLeft = static_cast<int>(context.originX - tileReq.originX);
            int tileTop = static_cast<int>(context.originY - tileReq.originY);

            // タイル結果はバッファ座標 (0,0) から始まる
            for (int y = 0; y < tileReq.height && (tileTop + y) < canvasHeight; y++) {
                int dstY = tileTop + y;
                if (dstY < 0 || dstY >= canvasHeight) continue;

                // ソースはバッファ相対座標 (0, y)
                const uint8_t* srcRow = tileResult.getPixelPtr<uint8_t>(0, y);
                // デスティネーションはキャンバス絶対座標
                uint8_t* dstRow = result.data.data() + dstY * canvasWidth * 4 + tileLeft * 4;

                int copyWidth = std::min(tileReq.width, canvasWidth - tileLeft);
                std::memcpy(dstRow, srcRow, copyWidth * 4);
            }
        }
    }

    return result;
}

} // namespace FLEXIMG_NAMESPACE
