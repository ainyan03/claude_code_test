#include "evaluation_node.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// ImageEvalNode 実装
// ========================================================================

EvalResult ImageEvalNode::evaluate(const RenderRequest& request,
                                   const RenderContext& context) {
    (void)context;

    if (!imageData || !imageData->isValid()) {
        return EvalResult();  // 空の結果
    }

    // 画像の基準相対座標範囲
    // srcOriginX/Y は 9点セレクタ (0=左上, 0.5=中央, 1=右下)
    // 例: 100x100画像、中央基準(0.5) → imgLeft = -50
    float imgLeft = -srcOriginX * imageData->width;
    float imgTop = -srcOriginY * imageData->height;
    float imgRight = imgLeft + imageData->width;
    float imgBottom = imgTop + imageData->height;

    // 要求範囲の基準相対座標範囲
    // バッファ位置0 は基準相対座標 -originX に対応
    float reqLeft = -request.originX;
    float reqTop = -request.originY;
    float reqRight = reqLeft + request.width;
    float reqBottom = reqTop + request.height;

    // 交差領域を計算（基準相対座標）
    float interLeft = std::max(imgLeft, reqLeft);
    float interTop = std::max(imgTop, reqTop);
    float interRight = std::min(imgRight, reqRight);
    float interBottom = std::min(imgBottom, reqBottom);

    // 交差領域がない場合は空の結果を返却
    if (interLeft >= interRight || interTop >= interBottom) {
        return EvalResult(ImageBuffer(), Point2f(reqLeft, reqTop));
    }

    // 交差領域の画像内ピクセル座標
    int imgX = static_cast<int>(interLeft - imgLeft);
    int imgY = static_cast<int>(interTop - imgTop);
    int interWidth = static_cast<int>(interRight - interLeft);
    int interHeight = static_cast<int>(interBottom - interTop);

    // 交差領域をコピーして新しいImageBufferを作成
    ImageBuffer result(interWidth, interHeight, imageData->formatID);
    size_t bytesPerPixel = imageData->getBytesPerPixel();

    for (int y = 0; y < interHeight; y++) {
        const void* srcRow = imageData->getPixelAddress(imgX, imgY + y);
        void* dstRow = result.getPixelAddress(0, y);
        std::memcpy(dstRow, srcRow, interWidth * bytesPerPixel);
    }

    // origin は「基準点から見た画像左上の相対座標」
    return EvalResult(std::move(result), Point2f(interLeft, interTop));
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

    prepared_ = true;
}

EvalResult FilterEvalNode::evaluate(const RenderRequest& request,
                                    const RenderContext& context) {
    if (inputs.empty()) {
        return EvalResult();  // 空の結果
    }

    // 1. 入力要求を計算（ブラー等では拡大される）
    RenderRequest inputReq = computeInputRequest(request);

    // 2. 上流ノードを評価
    EvalResult inputResult = inputs[0]->evaluate(inputReq, context);

    // 空入力の場合は早期リターン
    if (!inputResult.isValid()) {
        return inputResult;  // origin情報を保持したまま返す
    }

    // 3. フィルタ処理を適用
    if (op) {
        // OperatorInputを構築
        OperatorInput opInput(inputResult);

        EvalResult processed = op->apply({opInput}, request);

        // 4. 要求範囲を切り出す（ブラー等で入力が拡大されている場合）
        // 要求の基準相対座標の左上
        float reqLeft = -request.originX;
        float reqTop = -request.originY;

        // processed バッファ内での要求開始位置
        int startX = static_cast<int>(reqLeft - processed.origin.x);
        int startY = static_cast<int>(reqTop - processed.origin.y);

        // 切り出しが必要かチェック（入力が拡大されていない場合は不要）
        if (startX == 0 && startY == 0 &&
            processed.buffer.width == request.width && processed.buffer.height == request.height) {
            // 切り出し不要 - そのまま返す
            return processed;
        }

        // 範囲チェック
        if (startX < 0 || startY < 0 ||
            startX + request.width > processed.buffer.width ||
            startY + request.height > processed.buffer.height) {
            // 要求範囲が処理結果の範囲外（エラーケース）
            // 安全のため processed をそのまま返す
            return processed;
        }

        // 新しいImageBufferを作成して要求範囲をコピー
        ImageBuffer resultBuf(request.width, request.height, processed.buffer.formatID);
        size_t bytesPerPixel = processed.buffer.getBytesPerPixel();

        for (int y = 0; y < request.height; y++) {
            const void* srcRow = processed.buffer.getPixelAddress(startX, startY + y);
            void* dstRow = resultBuf.getPixelAddress(0, y);
            std::memcpy(dstRow, srcRow, request.width * bytesPerPixel);
        }

        return EvalResult(std::move(resultBuf), Point2f(reqLeft, reqTop));
    }

    // オペレーターがない場合はパススルー
    return inputResult;
}

RenderRequest FilterEvalNode::computeInputRequest(
    const RenderRequest& outputRequest) const {
    // オペレーターに入力要求計算を委譲
    // （ブラーフィルタ等はカーネル半径分拡大、他はそのまま返す）
    return op ? op->computeInputRequest(outputRequest) : outputRequest;
}

// ========================================================================
// AffineEvalNode 実装
// ========================================================================

void AffineEvalNode::prepare(const RenderContext& context) {
    (void)context;

    // 逆行列を計算
    float det = matrix.a * matrix.d - matrix.b * matrix.c;
    if (std::abs(det) < 1e-10f) {
        prepared_ = false;
        return;
    }

    float invDet = 1.0f / det;
    float invA = matrix.d * invDet;
    float invB = -matrix.b * invDet;
    float invC = -matrix.c * invDet;
    float invD = matrix.a * invDet;
    float invTx = (-matrix.d * matrix.tx + matrix.b * matrix.ty) * invDet;
    float invTy = (matrix.c * matrix.tx - matrix.a * matrix.ty) * invDet;

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

EvalResult AffineEvalNode::evaluate(const RenderRequest& request,
                                    const RenderContext& context) {
    if (inputs.empty() || !prepared_) {
        return EvalResult();  // 空の結果
    }

    // 1. 入力要求を計算
    RenderRequest inputReq = computeInputRequest(request);

    // 2. 上流ノードを評価
    EvalResult inputResult = inputs[0]->evaluate(inputReq, context);

    // 空入力の場合は早期リターン
    if (!inputResult.isValid()) {
        return inputResult;  // origin情報を保持したまま返す
    }

    // 3. フォーマット変換（必要な場合）
    if (inputResult.buffer.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
        ViewPort inputView = inputResult.buffer.view();
        ImageBuffer converted = inputView.toImageBuffer(PixelFormatIDs::RGBA16_Premultiplied);
        inputResult = EvalResult(std::move(converted), inputResult.origin);
    }

    // 4. アフィン変換を適用
    // 入力の基準相対座標（例: -50 は基準点から見て画像左上が左に50px）
    float inputSrcOriginX = inputResult.origin.x;
    float inputSrcOriginY = inputResult.origin.y;

    // 出力バッファ内での基準点位置（例: 64 はバッファ内で基準点がx=64の位置）
    float outputOriginX = request.originX;
    float outputOriginY = request.originY;

    // AffineOperatorに渡すオフセット
    // outputOffsetX = outputOriginX - inputSrcOriginX
    float outputOffsetX = outputOriginX - inputSrcOriginX;
    float outputOffsetY = outputOriginY - inputSrcOriginY;

    auto affineOp = OperatorFactory::createAffineOperator(
        matrix, outputOffsetX, outputOffsetY, request.width, request.height);

    // OperatorInputを構築
    OperatorInput opInput(inputResult);

    return affineOp->apply({opInput}, request);
}

RenderRequest AffineEvalNode::computeInputRequest(
    const RenderRequest& outputRequest) const {
    if (!prepared_) {
        return outputRequest;
    }

    // 出力要求の4頂点を逆変換してAABBを算出
    // originX/Y はバッファ相対座標なので、バッファ内座標で計算
    constexpr int FIXED_POINT_BITS = 16;
    constexpr int32_t FIXED_POINT_SCALE = 1 << FIXED_POINT_BITS;

    // バッファ内の4頂点を基準点からの相対座標で表現
    float corners[4][2] = {
        {-outputRequest.originX, -outputRequest.originY},
        {outputRequest.width - outputRequest.originX, -outputRequest.originY},
        {-outputRequest.originX, outputRequest.height - outputRequest.originY},
        {outputRequest.width - outputRequest.originX, outputRequest.height - outputRequest.originY}
    };

    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;

    for (int i = 0; i < 4; i++) {
        int32_t relX = std::lround(corners[i][0] * FIXED_POINT_SCALE);
        int32_t relY = std::lround(corners[i][1] * FIXED_POINT_SCALE);

        int64_t srcX = ((int64_t)fixedInvA * relX + (int64_t)fixedInvB * relY) >> FIXED_POINT_BITS;
        int64_t srcY = ((int64_t)fixedInvC * relX + (int64_t)fixedInvD * relY) >> FIXED_POINT_BITS;

        srcX += fixedInvTx;
        srcY += fixedInvTy;

        float sx = srcX / (float)FIXED_POINT_SCALE;
        float sy = srcY / (float)FIXED_POINT_SCALE;

        minX = std::min(minX, sx);
        minY = std::min(minY, sy);
        maxX = std::max(maxX, sx);
        maxY = std::max(maxY, sy);
    }

    // 要求領域の左上座標（基準相対座標）
    int reqLeft = static_cast<int>(std::floor(minX));
    int reqTop = static_cast<int>(std::floor(minY));

    return RenderRequest{
        static_cast<int>(std::ceil(maxX) - std::floor(minX)) + 1,  // width
        static_cast<int>(std::ceil(maxY) - std::floor(minY)) + 1,  // height
        // originX = バッファ内での基準点位置
        // バッファの x=0 が基準相対座標 reqLeft に対応するので、
        // 基準相対座標 0 はバッファの x=-reqLeft に対応
        static_cast<float>(-reqLeft),
        static_cast<float>(-reqTop)
    };
}

// ========================================================================
// CompositeEvalNode 実装（逐次合成方式）
// メモリ効率: O(n) → O(2) （canvas + 現在の入力1つ）
// ========================================================================

EvalResult CompositeEvalNode::evaluate(const RenderRequest& request,
                                       const RenderContext& context) {
    if (inputs.empty()) {
        return EvalResult();  // 空の結果
    }

    EvalResult canvas;
    bool canvasInitialized = false;
    float canvasOriginX = -request.originX;
    float canvasOriginY = -request.originY;

    // 逐次合成: 入力を1つずつ評価して合成
    for (size_t i = 0; i < inputs.size(); i++) {
        EvalResult inputResult = inputs[i]->evaluate(request, context);

        // 空入力はスキップ
        if (!inputResult.isValid()) {
            continue;
        }

        // フォーマット変換（必要な場合）
        if (inputResult.buffer.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
            ViewPort inputView = inputResult.buffer.view();
            ImageBuffer converted = inputView.toImageBuffer(PixelFormatIDs::RGBA16_Premultiplied);
            inputResult = EvalResult(std::move(converted), inputResult.origin);
        }

        // アルファ適用
        if (i < alphas.size() && alphas[i] != 1.0f) {
            uint16_t alphaU16 = static_cast<uint16_t>(alphas[i] * 65535);
            for (int y = 0; y < inputResult.buffer.height; y++) {
                uint16_t* row = static_cast<uint16_t*>(inputResult.buffer.getPixelAddress(0, y));
                for (int x = 0; x < inputResult.buffer.width * 4; x++) {
                    row[x] = (row[x] * alphaU16) >> 16;
                }
            }
        }

        if (!canvasInitialized) {
            // 最初の非空入力
            OperatorInput opInput(inputResult);
            if (CompositeOperator::coversFullRequest(opInput, request)) {
                // 完全カバー → moveでキャンバスに（メモリ確保なし）
                canvas = std::move(inputResult);
                canvas.origin = Point2f(canvasOriginX, canvasOriginY);
            } else {
                // 部分オーバーラップ → 透明キャンバス確保 + memcpyでコピー
                canvas = CompositeOperator::createCanvas(request);
                ViewPort canvasView = canvas.buffer.view();
                CompositeOperator::blendFirst(canvasView, canvas.origin.x, canvas.origin.y,
                                              inputResult.buffer.view(), inputResult.origin.x, inputResult.origin.y);
            }
            canvasInitialized = true;
        } else {
            // 2枚目以降 → ブレンド処理
            ViewPort canvasView = canvas.buffer.view();
            CompositeOperator::blendOnto(canvasView, canvas.origin.x, canvas.origin.y,
                                        inputResult.buffer.view(), inputResult.origin.x, inputResult.origin.y);
        }
        // inputResult はここでスコープを抜けて解放される
    }

    // 全ての入力が空だった場合
    if (!canvasInitialized) {
        return EvalResult(ImageBuffer(), Point2f(canvasOriginX, canvasOriginY));
    }

    return canvas;
}

RenderRequest CompositeEvalNode::computeInputRequest(
    const RenderRequest& outputRequest) const {
    // 合成ノードは入力要求をそのまま伝播
    return outputRequest;
}

// ========================================================================
// OutputEvalNode 実装
// ========================================================================

EvalResult OutputEvalNode::evaluate(const RenderRequest& request,
                                    const RenderContext& context) {
    if (inputs.empty()) {
        return EvalResult();  // 空の結果
    }

    // 上流ノードを評価して結果を返す（空の結果もそのまま伝播）
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
    const std::map<int, ImageBuffer>& imageLibrary) {

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
    const std::map<int, ImageBuffer>& imageLibrary) {

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

} // namespace FLEXIMG_NAMESPACE
