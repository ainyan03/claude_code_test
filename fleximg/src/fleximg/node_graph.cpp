#include "node_graph.h"
#include "operators.h"
#include "evaluation_node.h"
#include "eval_result.h"
#include "pixel_format_registry.h"
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

void NodeGraphEvaluator::setTileSize(int width, int height) {
    tileWidth_ = width;
    tileHeight_ = height;
}

void NodeGraphEvaluator::setDebugCheckerboard(bool enabled) {
    debugCheckerboard_ = enabled;
}

void NodeGraphEvaluator::registerInput(int id, const ViewPort& view) {
    inputLibrary[id] = view;
    pipelineDirty_ = true;  // パイプライン再構築が必要（ImageEvalNodeの参照を更新）
}

void NodeGraphEvaluator::registerInput(int id, const void* data, int width, int height, PixelFormatID format) {
    const PixelFormatDescriptor* desc = PixelFormatRegistry::getInstance().getFormat(format);
    size_t bytesPerPixel = desc ? (desc->bitsPerPixel + 7) / 8 : 4;
    int stride = width * bytesPerPixel;
    inputLibrary[id] = ViewPort(const_cast<void*>(data), format, stride, width, height);
    pipelineDirty_ = true;
}

void NodeGraphEvaluator::registerOutput(int id, const ViewPort& view) {
    outputLibrary[id] = view;
    pipelineDirty_ = true;
}

void NodeGraphEvaluator::registerOutput(int id, void* data, int width, int height, PixelFormatID format) {
    const PixelFormatDescriptor* desc = PixelFormatRegistry::getInstance().getFormat(format);
    size_t bytesPerPixel = desc ? (desc->bitsPerPixel + 7) / 8 : 4;
    int stride = width * bytesPerPixel;
    outputLibrary[id] = ViewPort(data, format, stride, width, height);
    pipelineDirty_ = true;
}

void NodeGraphEvaluator::setNodes(const std::vector<GraphNode>& newNodes) {
    nodes = newNodes;
    pipelineDirty_ = true;  // パイプライン再構築が必要
}

void NodeGraphEvaluator::setConnections(const std::vector<GraphConnection>& newConnections) {
    connections = newConnections;
    pipelineDirty_ = true;  // パイプライン再構築が必要
}

// ノードグラフ全体を評価（出力は登録済みのoutputLibraryに書き込まれる）
void NodeGraphEvaluator::evaluateGraph() {
    // パフォーマンス計測をリセット
    perfMetrics.reset();

    // RenderContextを構築
    RenderContext context;
    context.totalWidth = canvasWidth;
    context.totalHeight = canvasHeight;
    context.originX = dstOriginX;
    context.originY = dstOriginY;
    context.tileWidth = tileWidth_;
    context.tileHeight = tileHeight_;
    context.debugCheckerboard = debugCheckerboard_;

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    context.perfMetrics = &perfMetrics;
#endif

    // パイプラインベース評価
    evaluateWithPipeline(context);
}

// ========================================================================
// パイプラインベース評価システム
// ========================================================================

void NodeGraphEvaluator::buildPipelineIfNeeded() {
    if (!pipelineDirty_ && pipeline_) {
        return;  // 再構築不要
    }

    // パイプラインを構築
    Pipeline newPipeline = PipelineBuilder::build(nodes, connections, inputLibrary, outputLibrary);

    if (newPipeline.isValid()) {
        pipeline_ = std::make_unique<Pipeline>(std::move(newPipeline));
    } else {
        pipeline_.reset();
    }

    pipelineDirty_ = false;
}

void NodeGraphEvaluator::evaluateWithPipeline(const RenderContext& context) {
    // パイプラインを構築（必要な場合のみ）
    buildPipelineIfNeeded();

    if (!pipeline_ || !pipeline_->isValid()) {
        return;  // パイプラインが無効な場合は何もしない
    }

    // 描画準備（逆行列計算等）
    pipeline_->prepare(context);

    // タイル処理ループ
    int tileCountX = context.getTileCountX();
    int tileCountY = context.getTileCountY();

    for (int ty = 0; ty < tileCountY; ty++) {
        for (int tx = 0; tx < tileCountX; tx++) {
            // デバッグ用チェッカーボードモード（市松模様スキップ）
            if (context.debugCheckerboard && ((tx + ty) % 2 == 1)) {
                continue;
            }

            RenderRequest tileReq = RenderRequest::fromTile(context, tx, ty);

            // パイプラインでタイル評価
            // OutputEvalNode が直接 outputLibrary に書き込む
            pipeline_->outputNode->evaluate(tileReq, context);
        }
    }
}

} // namespace FLEXIMG_NAMESPACE
