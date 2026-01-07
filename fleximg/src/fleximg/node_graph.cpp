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
      dstOriginX(width / 2.0), dstOriginY(height / 2.0) {}  // デフォルトはキャンバス中央

// デストラクタ（Pipeline の完全な定義が見えるここで実装）
NodeGraphEvaluator::~NodeGraphEvaluator() = default;

void NodeGraphEvaluator::setCanvasSize(int width, int height) {
    canvasWidth = width;
    canvasHeight = height;
    // dstOrigin も更新（デフォルトは中央）
    dstOriginX = width / 2.0;
    dstOriginY = height / 2.0;
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

void NodeGraphEvaluator::registerImage(int imageId, const Image& img) {
    // Image → ViewPort(RGBA8_Straight) に変換して保存
    imageLibrary[imageId] = ViewPort::fromImage(img);
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
            0, 0, canvasWidth, canvasHeight,
            dstOriginX, dstOriginY
        };

        // パイプラインで評価
        ViewPort resultViewPort = pipeline_->outputNode->evaluate(fullRequest, context);

        // srcOrigin が dstOrigin と一致しない場合、最終配置を適用
        const double epsilon = 0.001;
        if (std::abs(resultViewPort.srcOriginX - dstOriginX) > epsilon ||
            std::abs(resultViewPort.srcOriginY - dstOriginY) > epsilon) {
            if (resultViewPort.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                auto convStart = std::chrono::high_resolution_clock::now();
                resultViewPort = resultViewPort.convertTo(PixelFormatIDs::RGBA16_Premultiplied);
                auto convEnd = std::chrono::high_resolution_clock::now();
                perfMetrics.convertTime += std::chrono::duration<double, std::milli>(convEnd - convStart).count();
                perfMetrics.convertCount++;
            }
            auto compStart = std::chrono::high_resolution_clock::now();
            auto compositeOp = OperatorFactory::createCompositeOperator();
            OperatorContext ctx(canvasWidth, canvasHeight, dstOriginX, dstOriginY);
            resultViewPort = compositeOp->apply({resultViewPort}, ctx);
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
            // デバッグ用チェッカーボードモード
            if (tileStrategy == TileStrategy::Debug_Checkerboard) {
                if ((tx + ty) % 2 == 1) {
                    continue;
                }
            }

            RenderRequest tileReq = RenderRequest::fromTile(context, tx, ty);

            // パイプラインでタイル評価
            ViewPort tileResult = pipeline_->outputNode->evaluate(tileReq, context);

            // srcOrigin と dstOrigin の差を解消
            const double epsilon = 0.001;
            if (std::abs(tileResult.srcOriginX - dstOriginX) > epsilon ||
                std::abs(tileResult.srcOriginY - dstOriginY) > epsilon) {
                if (tileResult.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                    tileResult = tileResult.convertTo(PixelFormatIDs::RGBA16_Premultiplied);
                }
                auto compositeOp = OperatorFactory::createCompositeOperator();
                OperatorContext ctx(canvasWidth, canvasHeight, dstOriginX, dstOriginY);
                tileResult = compositeOp->apply({tileResult}, ctx);
            }

            // タイル結果を最終画像にコピー（RGBA8_Straightに変換してmemcpy）
            if (tileResult.formatID != PixelFormatIDs::RGBA8_Straight) {
                tileResult = tileResult.convertTo(PixelFormatIDs::RGBA8_Straight);
            }

            for (int y = 0; y < tileReq.height && (tileReq.y + y) < canvasHeight; y++) {
                int dstY = tileReq.y + y;
                if (dstY < 0 || dstY >= canvasHeight) continue;

                const uint8_t* srcRow = tileResult.getPixelPtr<uint8_t>(tileReq.x, dstY);
                uint8_t* dstRow = result.data.data() + dstY * canvasWidth * 4 + tileReq.x * 4;

                int copyWidth = std::min(tileReq.width, canvasWidth - tileReq.x);
                std::memcpy(dstRow, srcRow, copyWidth * 4);
            }
        }
    }

    return result;
}

} // namespace FLEXIMG_NAMESPACE
