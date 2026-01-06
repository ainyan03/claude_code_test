#include "evaluation_node.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace ImageTransform {

// ========================================================================
// ImageEvalNode 実装
// ========================================================================

ViewPort ImageEvalNode::evaluate(const RenderRequest& request,
                                  const RenderContext& context) {
    (void)request;
    (void)context;

    if (!imageData || !imageData->isValid()) {
        return ViewPort(1, 1, PixelFormatIDs::RGBA8_Straight);
    }

    // 画像データをコピーして返却
    ViewPort result = *imageData;
    result.srcOriginX = srcOriginX * result.width;
    result.srcOriginY = srcOriginY * result.height;
    return result;
}

RenderRequest ImageEvalNode::computeInputRequest(
    const RenderRequest& outputRequest) const {
    (void)outputRequest;
    // 画像ノードは終端なので入力要求なし
    return RenderRequest{};
}

// ========================================================================
// FilterEvalNode 実装
// ========================================================================

void FilterEvalNode::prepare(const RenderContext& context) {
    (void)context;

    // フィルタオペレーターを生成
    op = OperatorFactory::createFilterOperator(filterType, filterParams);

    // カーネル半径を設定
    if (filterType == "boxblur" && !filterParams.empty()) {
        kernelRadius = static_cast<int>(filterParams[0]);
    } else {
        kernelRadius = 0;
    }

    prepared_ = true;
}

ViewPort FilterEvalNode::evaluate(const RenderRequest& request,
                                   const RenderContext& context) {
    if (inputs.empty()) {
        return ViewPort(1, 1, PixelFormatIDs::RGBA8_Straight);
    }

    // 1. 入力要求を計算
    RenderRequest inputReq = computeInputRequest(request);

    // 2. 上流ノードを評価
    ViewPort input = inputs[0]->evaluate(inputReq, context);

    // 3. フィルタ処理を適用
    if (op) {
        OperatorContext ctx(context.totalWidth, context.totalHeight,
                           request.originX, request.originY);
        ViewPort result = op->apply({input}, ctx);
        result.srcOriginX = input.srcOriginX;
        result.srcOriginY = input.srcOriginY;
        return result;
    }

    // オペレーターがない場合はパススルー
    return input;
}

RenderRequest FilterEvalNode::computeInputRequest(
    const RenderRequest& outputRequest) const {
    // カーネル半径分だけ領域を拡大
    return outputRequest.expand(kernelRadius);
}

// ========================================================================
// AffineEvalNode 実装
// ========================================================================

void AffineEvalNode::prepare(const RenderContext& context) {
    (void)context;

    // 逆行列を計算
    double det = matrix.a * matrix.d - matrix.b * matrix.c;
    if (std::abs(det) < 1e-10) {
        prepared_ = false;
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

    fixedInvA = std::lround(invA * FIXED_POINT_SCALE);
    fixedInvB = std::lround(invB * FIXED_POINT_SCALE);
    fixedInvC = std::lround(invC * FIXED_POINT_SCALE);
    fixedInvD = std::lround(invD * FIXED_POINT_SCALE);
    fixedInvTx = std::lround(invTx * FIXED_POINT_SCALE);
    fixedInvTy = std::lround(invTy * FIXED_POINT_SCALE);

    prepared_ = true;
}

ViewPort AffineEvalNode::evaluate(const RenderRequest& request,
                                   const RenderContext& context) {
    if (inputs.empty() || !prepared_) {
        return ViewPort(1, 1, PixelFormatIDs::RGBA16_Premultiplied);
    }

    // 1. 入力要求を計算
    RenderRequest inputReq = computeInputRequest(request);

    // 2. 上流ノードを評価
    ViewPort input = inputs[0]->evaluate(inputReq, context);

    // 3. フォーマット変換
    if (input.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
        input = input.convertTo(PixelFormatIDs::RGBA16_Premultiplied);
    }

    // 4. アフィン変換を適用
    double inputOriginX = input.srcOriginX;
    double inputOriginY = input.srcOriginY;

    // 出力オフセット: tx,ty を考慮してマージンを設定
    double baseOffset = std::max(input.width, input.height);
    double outputOffsetX = baseOffset + std::abs(matrix.tx);
    double outputOffsetY = baseOffset + std::abs(matrix.ty);

    int outputWidth = input.width + static_cast<int>(outputOffsetX * 2);
    int outputHeight = input.height + static_cast<int>(outputOffsetY * 2);

    auto affineOp = OperatorFactory::createAffineOperator(
        matrix, inputOriginX, inputOriginY,
        outputOffsetX, outputOffsetY, outputWidth, outputHeight);

    OperatorContext ctx(context.totalWidth, context.totalHeight,
                       request.originX, request.originY);
    ViewPort result = affineOp->apply({input}, ctx);

    // srcOrigin は tx,ty を含めない
    result.srcOriginX = inputOriginX + outputOffsetX;
    result.srcOriginY = inputOriginY + outputOffsetY;

    return result;
}

RenderRequest AffineEvalNode::computeInputRequest(
    const RenderRequest& outputRequest) const {
    if (!prepared_) {
        return outputRequest;
    }

    // 出力要求の4頂点を逆変換してAABBを算出
    constexpr int FIXED_POINT_BITS = 16;
    constexpr int32_t FIXED_POINT_SCALE = 1 << FIXED_POINT_BITS;

    double corners[4][2] = {
        {outputRequest.x - outputRequest.originX,
         outputRequest.y - outputRequest.originY},
        {outputRequest.x + outputRequest.width - outputRequest.originX,
         outputRequest.y - outputRequest.originY},
        {outputRequest.x - outputRequest.originX,
         outputRequest.y + outputRequest.height - outputRequest.originY},
        {outputRequest.x + outputRequest.width - outputRequest.originX,
         outputRequest.y + outputRequest.height - outputRequest.originY}
    };

    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;

    for (int i = 0; i < 4; i++) {
        int32_t relX = std::lround(corners[i][0] * FIXED_POINT_SCALE);
        int32_t relY = std::lround(corners[i][1] * FIXED_POINT_SCALE);

        int64_t srcX = ((int64_t)fixedInvA * relX + (int64_t)fixedInvB * relY) >> FIXED_POINT_BITS;
        int64_t srcY = ((int64_t)fixedInvC * relX + (int64_t)fixedInvD * relY) >> FIXED_POINT_BITS;

        srcX += fixedInvTx;
        srcY += fixedInvTy;

        double sx = srcX / (double)FIXED_POINT_SCALE;
        double sy = srcY / (double)FIXED_POINT_SCALE;

        minX = std::min(minX, sx);
        minY = std::min(minY, sy);
        maxX = std::max(maxX, sx);
        maxY = std::max(maxY, sy);
    }

    return RenderRequest{
        static_cast<int>(std::floor(minX)),
        static_cast<int>(std::floor(minY)),
        static_cast<int>(std::ceil(maxX) - std::floor(minX)) + 1,
        static_cast<int>(std::ceil(maxY) - std::floor(minY)) + 1,
        0, 0
    };
}

// ========================================================================
// CompositeEvalNode 実装
// ========================================================================

ViewPort CompositeEvalNode::evaluate(const RenderRequest& request,
                                      const RenderContext& context) {
    if (inputs.empty()) {
        return ViewPort(context.totalWidth, context.totalHeight,
                       PixelFormatIDs::RGBA16_Premultiplied);
    }

    // 1. 全入力ノードを評価
    std::vector<ViewPort> inputImages;
    for (size_t i = 0; i < inputs.size(); i++) {
        ViewPort img = inputs[i]->evaluate(request, context);

        // フォーマット変換
        if (img.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
            img = img.convertTo(PixelFormatIDs::RGBA16_Premultiplied);
        }

        // アルファ適用
        if (i < alphas.size() && alphas[i] != 1.0) {
            uint16_t alphaU16 = static_cast<uint16_t>(alphas[i] * 65535);
            for (int y = 0; y < img.height; y++) {
                uint16_t* row = img.getPixelPtr<uint16_t>(0, y);
                for (int x = 0; x < img.width * 4; x++) {
                    row[x] = (row[x] * alphaU16) >> 16;
                }
            }
        }

        inputImages.push_back(std::move(img));
    }

    // 2. 合成処理
    auto compositeOp = OperatorFactory::createCompositeOperator();
    OperatorContext ctx(context.totalWidth, context.totalHeight,
                       request.originX, request.originY);
    return compositeOp->apply(inputImages, ctx);
}

RenderRequest CompositeEvalNode::computeInputRequest(
    const RenderRequest& outputRequest) const {
    // 合成ノードは入力要求をそのまま伝播
    return outputRequest;
}

// ========================================================================
// OutputEvalNode 実装
// ========================================================================

ViewPort OutputEvalNode::evaluate(const RenderRequest& request,
                                   const RenderContext& context) {
    if (inputs.empty()) {
        return ViewPort(context.totalWidth, context.totalHeight,
                       PixelFormatIDs::RGBA16_Premultiplied);
    }

    // 上流ノードを評価して結果を返す
    return inputs[0]->evaluate(request, context);
}

RenderRequest OutputEvalNode::computeInputRequest(
    const RenderRequest& outputRequest) const {
    // 出力ノードは入力要求をそのまま伝播
    return outputRequest;
}

// ========================================================================
// PipelineBuilder 実装
// ========================================================================

std::unique_ptr<EvaluationNode> PipelineBuilder::createEvalNode(
    const GraphNode& node,
    const std::map<int, ViewPort>& imageLibrary) {

    if (node.type == "image") {
        auto evalNode = std::make_unique<ImageEvalNode>();
        evalNode->id = node.id;

        // 画像データへの参照を設定
        auto it = imageLibrary.find(node.imageId);
        if (it != imageLibrary.end()) {
            evalNode->imageData = &it->second;
        }
        evalNode->srcOriginX = node.srcOriginX;
        evalNode->srcOriginY = node.srcOriginY;

        return evalNode;
    }
    else if (node.type == "filter") {
        auto evalNode = std::make_unique<FilterEvalNode>();
        evalNode->id = node.id;
        evalNode->filterType = node.filterType;
        evalNode->filterParams = node.filterParams;

        return evalNode;
    }
    else if (node.type == "affine") {
        auto evalNode = std::make_unique<AffineEvalNode>();
        evalNode->id = node.id;
        evalNode->matrix = node.affineMatrix;

        return evalNode;
    }
    else if (node.type == "composite") {
        auto evalNode = std::make_unique<CompositeEvalNode>();
        evalNode->id = node.id;

        // アルファ値を設定
        for (const auto& input : node.compositeInputs) {
            evalNode->alphas.push_back(input.alpha);
        }

        return evalNode;
    }
    else if (node.type == "output") {
        auto evalNode = std::make_unique<OutputEvalNode>();
        evalNode->id = node.id;
        return evalNode;
    }

    // 未知のノードタイプ
    return nullptr;
}

Pipeline PipelineBuilder::build(
    const std::vector<GraphNode>& nodes,
    const std::vector<GraphConnection>& connections,
    const std::map<int, ViewPort>& imageLibrary) {

    Pipeline pipeline;

    // 1. 全ノードのEvaluationNodeを生成
    std::map<std::string, EvaluationNode*> nodeMap;

    for (const auto& node : nodes) {
        auto evalNode = createEvalNode(node, imageLibrary);
        if (evalNode) {
            // 出力ノードを記録
            if (node.type == "output") {
                pipeline.outputNode = evalNode.get();
            }
            nodeMap[node.id] = evalNode.get();
            pipeline.nodes.push_back(std::move(evalNode));
        }
    }

    if (!pipeline.outputNode) {
        return pipeline;  // 無効なパイプライン
    }

    // 2. コネクションに基づいてポインタを接続
    for (const auto& node : nodes) {
        auto it = nodeMap.find(node.id);
        if (it == nodeMap.end()) continue;

        EvaluationNode* evalNode = it->second;

        if (node.type == "composite") {
            // 合成ノード: compositeInputs の順序で入力を接続
            for (const auto& input : node.compositeInputs) {
                for (const auto& conn : connections) {
                    if (conn.toNodeId == node.id && conn.toPort == input.id) {
                        auto fromIt = nodeMap.find(conn.fromNodeId);
                        if (fromIt != nodeMap.end()) {
                            evalNode->inputs.push_back(fromIt->second);
                        }
                        break;
                    }
                }
            }
        } else {
            // 単一入力ノード: "in" ポートへの接続を検索
            for (const auto& conn : connections) {
                if (conn.toNodeId == node.id && conn.toPort == "in") {
                    auto fromIt = nodeMap.find(conn.fromNodeId);
                    if (fromIt != nodeMap.end()) {
                        evalNode->inputs.push_back(fromIt->second);
                    }
                    break;
                }
            }
        }
    }

    return pipeline;
}

} // namespace ImageTransform
