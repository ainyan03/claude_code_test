#include "renderer.h"
#include "operations/transform.h"
#include "operations/filters.h"
#include "operations/blend.h"
#include "pixel_format_registry.h"
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <cmath>
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

void Renderer::prepare() {
    if (!output_ || !output_->target().isValid()) return;

    // メトリクスをリセット
    PerfMetrics::instance().reset();

    // コンテキスト初期化
    context_.canvasWidth = output_->canvasWidth();
    context_.canvasHeight = output_->canvasHeight();
    context_.originX = output_->originX();
    context_.originY = output_->originY();
    context_.tileConfig = tileConfig_;

    // 上流ノードのprepare()を呼ぶ（将来拡張用）
}

void Renderer::execute() {
    if (!output_ || !output_->target().isValid()) return;

    int tileCountX = context_.tileCountX();
    int tileCountY = context_.tileCountY();

    for (int ty = 0; ty < tileCountY; ++ty) {
        for (int tx = 0; tx < tileCountX; ++tx) {
            // デバッグ用チェッカーボード: 市松模様でタイルをスキップ
            if (debugCheckerboard_ && ((tx + ty) % 2 == 1)) {
                continue;
            }
            processTile(tx, ty);
        }
    }
}

void Renderer::finalize() {
    // 現時点では何もしない（将来の拡張用）
}

void Renderer::processTile(int tileX, int tileY) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    auto outputStart = std::chrono::high_resolution_clock::now();
#endif

    context_.tileX = tileX;
    context_.tileY = tileY;

    int tw = context_.effectiveTileWidth();
    int th = context_.effectiveTileHeight();
    int tileLeft = tileX * tw;
    int tileTop = tileY * th;

    // タイルサイズ（端の処理）
    int tileW = std::min(tw, context_.canvasWidth - tileLeft);
    int tileH = std::min(th, context_.canvasHeight - tileTop);

    // タイル用のRenderRequest
    RenderRequest request;
    request.width = tileW;
    request.height = tileH;
    request.originX = context_.originX - tileLeft;
    request.originY = context_.originY - tileTop;

    // 上流ノードを評価
    Node* upstream = output_->upstreamNode(0);
    if (!upstream) return;

    RenderResult result = evaluateUpstream(upstream, request);

    // 結果を出力先にコピー
    if (result.isValid()) {
        ViewPort& target = output_->target();
        ViewPort resultView = result.view();

        // 結果の配置位置を計算
        int dstX = tileLeft + static_cast<int>(result.origin.x + request.originX);
        int dstY = tileTop + static_cast<int>(result.origin.y + request.originY);

        // 結果バッファ内のソース開始位置（タイル境界に合わせる）
        int srcX = 0;
        int srcY = 0;
        if (dstX < tileLeft) {
            srcX = tileLeft - dstX;
            dstX = tileLeft;
        }
        if (dstY < tileTop) {
            srcY = tileTop - dstY;
            dstY = tileTop;
        }

        // コピーサイズをタイル境界に制限
        int copyW = std::min(resultView.width - srcX, tileW - (dstX - tileLeft));
        int copyH = std::min(resultView.height - srcY, tileH - (dstY - tileTop));

        if (copyW > 0 && copyH > 0) {
            view_ops::copy(target, dstX, dstY, resultView, srcX, srcY, copyW, copyH);
        }
    }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    auto& m = PerfMetrics::instance().nodes[NodeType::Output];
    m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - outputStart).count();
    m.count++;
#endif
}

RenderResult Renderer::evaluateUpstream(Node* node, const RenderRequest& request) {
    if (!node) return RenderResult();

    // SourceNodeの場合
    if (auto* src = dynamic_cast<SourceNode*>(node)) {
        return evaluateSourceNode(src, request);
    }

    // TransformNodeの場合
    if (auto* xform = dynamic_cast<TransformNode*>(node)) {
        return evaluateTransformNode(xform, request);
    }

    // FilterNodeの場合
    if (auto* filter = dynamic_cast<FilterNode*>(node)) {
        return evaluateFilterNode(filter, request);
    }

    // CompositeNodeの場合
    if (auto* composite = dynamic_cast<CompositeNode*>(node)) {
        return evaluateCompositeNode(composite, request);
    }

    // その他のノード（将来の拡張用）
    return RenderResult();
}

// ========================================================================
// SourceNode 評価
// ========================================================================

RenderResult Renderer::evaluateSourceNode(SourceNode* src, const RenderRequest& request) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    auto sourceStart = std::chrono::high_resolution_clock::now();
#endif

    const ViewPort& source = src->source();
    if (!source.isValid()) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& m = PerfMetrics::instance().nodes[NodeType::Source];
        m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - sourceStart).count();
        m.count++;
#endif
        return RenderResult();
    }

    // ソース画像の基準相対座標範囲
    float srcOriginX = src->originX();
    float srcOriginY = src->originY();
    float imgLeft = -srcOriginX;
    float imgTop = -srcOriginY;
    float imgRight = imgLeft + source.width;
    float imgBottom = imgTop + source.height;

    // 要求範囲の基準相対座標
    float reqLeft = -request.originX;
    float reqTop = -request.originY;
    float reqRight = reqLeft + request.width;
    float reqBottom = reqTop + request.height;

    // 交差領域
    float interLeft = std::max(imgLeft, reqLeft);
    float interTop = std::max(imgTop, reqTop);
    float interRight = std::min(imgRight, reqRight);
    float interBottom = std::min(imgBottom, reqBottom);

    if (interLeft >= interRight || interTop >= interBottom) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& m = PerfMetrics::instance().nodes[NodeType::Source];
        m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - sourceStart).count();
        m.count++;
#endif
        return RenderResult(ImageBuffer(), Point2f(reqLeft, reqTop));
    }

    // 交差領域をコピー
    int srcX = static_cast<int>(interLeft - imgLeft);
    int srcY = static_cast<int>(interTop - imgTop);
    int interW = static_cast<int>(interRight - interLeft);
    int interH = static_cast<int>(interBottom - interTop);

    ImageBuffer result(interW, interH, source.formatID);
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    PerfMetrics::instance().nodes[NodeType::Source].recordAlloc(
        result.totalBytes(), result.width(), result.height());
#endif
    ViewPort resultView = result.view();
    view_ops::copy(resultView, 0, 0, source, srcX, srcY, interW, interH);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    auto& m = PerfMetrics::instance().nodes[NodeType::Source];
    m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - sourceStart).count();
    m.count++;
#endif
    return RenderResult(std::move(result), Point2f(interLeft, interTop));
}

// ========================================================================
// TransformNode 評価
// ========================================================================

RenderResult Renderer::evaluateTransformNode(TransformNode* xform, const RenderRequest& request) {
    Node* upstream = xform->upstreamNode(0);
    if (!upstream) return RenderResult();

    const AffineMatrix& matrix = xform->matrix();

    // 固定小数点逆行列を事前計算
    auto invMatrix = transform::FixedPointInverseMatrix::fromMatrix(matrix);
    if (!invMatrix.valid) {
        // 特異行列
        return RenderResult();
    }

    // 出力要求の4頂点を固定小数点逆行列で逆変換してAABBを算出
    // 固定小数点演算で範囲計算（DDAと同じ精度を保証）
    int32_t corners[4][2] = {
        {static_cast<int32_t>(-request.originX), static_cast<int32_t>(-request.originY)},
        {static_cast<int32_t>(request.width - request.originX), static_cast<int32_t>(-request.originY)},
        {static_cast<int32_t>(-request.originX), static_cast<int32_t>(request.height - request.originY)},
        {static_cast<int32_t>(request.width - request.originX), static_cast<int32_t>(request.height - request.originY)}
    };

    int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
    for (int i = 0; i < 4; i++) {
        // 固定小数点演算: result = (a * x + b * y + tx) >> FIXED_POINT_BITS
        int64_t sx64 = static_cast<int64_t>(invMatrix.a) * corners[i][0]
                     + static_cast<int64_t>(invMatrix.b) * corners[i][1]
                     + invMatrix.tx;
        int64_t sy64 = static_cast<int64_t>(invMatrix.c) * corners[i][0]
                     + static_cast<int64_t>(invMatrix.d) * corners[i][1]
                     + invMatrix.ty;
        int32_t sx = static_cast<int32_t>(sx64 >> transform::FIXED_POINT_BITS);
        int32_t sy = static_cast<int32_t>(sy64 >> transform::FIXED_POINT_BITS);
        minX = std::min(minX, sx);
        minY = std::min(minY, sy);
        maxX = std::max(maxX, sx);
        maxY = std::max(maxY, sy);
    }

    // マージンを追加（固定小数点の丸め誤差対策）
    int reqLeft = minX - 1;
    int reqTop = minY - 1;
    int inputWidth = maxX - minX + 3;
    int inputHeight = maxY - minY + 3;

    RenderRequest inputReq;
    inputReq.width = inputWidth;
    inputReq.height = inputHeight;
    inputReq.originX = static_cast<float>(-reqLeft);
    inputReq.originY = static_cast<float>(-reqTop);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    // ピクセル効率計測: 上流に要求したピクセル数
    auto& mTrans = PerfMetrics::instance().nodes[NodeType::Transform];
    mTrans.requestedPixels += static_cast<uint64_t>(inputReq.width) * inputReq.height;
    mTrans.usedPixels += static_cast<uint64_t>(request.width) * request.height;
#endif

    // 上流を評価
    RenderResult inputResult = evaluateUpstream(upstream, inputReq);
    if (!inputResult.isValid()) {
        return RenderResult(ImageBuffer(), Point2f(-request.originX, -request.originY));
    }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    auto transformStart = std::chrono::high_resolution_clock::now();
#endif

    // 出力バッファを作成（ゼロ初期化済み）
    ImageBuffer output(request.width, request.height, inputResult.buffer.formatID());
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    PerfMetrics::instance().nodes[NodeType::Transform].recordAlloc(
        output.totalBytes(), output.width(), output.height());
#endif
    ViewPort outputView = output.view();

    // アフィン変換を適用（事前計算した固定小数点逆行列を使用）
    ViewPort inputView = inputResult.view();
    transform::affine(outputView, request.originX, request.originY,
                      inputView,
                      -inputResult.origin.x, -inputResult.origin.y,
                      invMatrix);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    auto& mTransEnd = PerfMetrics::instance().nodes[NodeType::Transform];
    mTransEnd.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - transformStart).count();
    mTransEnd.count++;
#endif

    return RenderResult(std::move(output), Point2f(-request.originX, -request.originY));
}

// ========================================================================
// FilterNode 評価
// ========================================================================

RenderResult Renderer::evaluateFilterNode(FilterNode* filter, const RenderRequest& request) {
    Node* upstream = filter->upstreamNode(0);
    if (!upstream) return RenderResult();

    // 入力要求を計算（ブラーの場合は拡大）
    int margin = filter->kernelRadius();
    RenderRequest inputReq = request.expand(margin);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    // ピクセル効率計測: 上流に要求したピクセル数
    auto& mFilt = PerfMetrics::instance().nodes[NodeType::Filter];
    mFilt.requestedPixels += static_cast<uint64_t>(inputReq.width) * inputReq.height;
    mFilt.usedPixels += static_cast<uint64_t>(request.width) * request.height;
#endif

    // 上流を評価
    RenderResult inputResult = evaluateUpstream(upstream, inputReq);
    if (!inputResult.isValid()) {
        return inputResult;
    }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    auto filterStart = std::chrono::high_resolution_clock::now();
#endif

    PixelFormatID inputFormatID = inputResult.buffer.formatID();
    bool needsConversion = (inputFormatID != PixelFormatIDs::RGBA8_Straight);

    // フィルタは8bit処理のため、必要に応じてRGBA8_Straightに変換
    ImageBuffer workBuffer;
    ViewPort workInputView;

    if (needsConversion) {
        // RGBA8_Straightに変換
        workBuffer = ImageBuffer(inputResult.buffer.width(), inputResult.buffer.height(),
                                 PixelFormatIDs::RGBA8_Straight);
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::Filter].recordAlloc(
            workBuffer.totalBytes(), workBuffer.width(), workBuffer.height());
#endif
        int pixelCount = workBuffer.width() * workBuffer.height();
        PixelFormatRegistry::getInstance().convert(
            inputResult.buffer.data(), inputFormatID,
            workBuffer.data(), PixelFormatIDs::RGBA8_Straight,
            pixelCount);
        workInputView = workBuffer.view();
    } else {
        workInputView = inputResult.view();
    }

    // 出力バッファを作成（8bit）
    ImageBuffer output8bit(inputResult.buffer.width(), inputResult.buffer.height(),
                           PixelFormatIDs::RGBA8_Straight);
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    PerfMetrics::instance().nodes[NodeType::Filter].recordAlloc(
        output8bit.totalBytes(), output8bit.width(), output8bit.height());
#endif
    ViewPort outputView = output8bit.view();

    // フィルタを適用（8bit処理）
    switch (filter->filterType()) {
        case FilterType::Brightness:
            filters::brightness(outputView, workInputView, filter->brightnessAmount());
            break;
        case FilterType::Grayscale:
            filters::grayscale(outputView, workInputView);
            break;
        case FilterType::BoxBlur:
            filters::boxBlur(outputView, workInputView, filter->blurRadius());
            break;
        case FilterType::Alpha:
            filters::alpha(outputView, workInputView, filter->alphaScale());
            break;
        case FilterType::None:
        default:
            // パススルー
            view_ops::copy(outputView, 0, 0, workInputView, 0, 0, workInputView.width, workInputView.height);
            break;
    }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    auto& mFiltEnd = PerfMetrics::instance().nodes[NodeType::Filter];
    mFiltEnd.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - filterStart).count();
    mFiltEnd.count++;
#endif

    // 必要に応じて元のフォーマットに戻す
    ImageBuffer finalOutput;
    if (needsConversion) {
        finalOutput = ImageBuffer(output8bit.width(), output8bit.height(), inputFormatID);
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::Filter].recordAlloc(
            finalOutput.totalBytes(), finalOutput.width(), finalOutput.height());
#endif
        int pixelCount = finalOutput.width() * finalOutput.height();
        PixelFormatRegistry::getInstance().convert(
            output8bit.data(), PixelFormatIDs::RGBA8_Straight,
            finalOutput.data(), inputFormatID,
            pixelCount);
    } else {
        finalOutput = std::move(output8bit);
    }

    // ブラーの場合は要求範囲を切り出す
    if (margin > 0) {
        float reqLeft = -request.originX;
        float reqTop = -request.originY;
        int startX = static_cast<int>(reqLeft - inputResult.origin.x);
        int startY = static_cast<int>(reqTop - inputResult.origin.y);

        // 範囲チェック
        if (startX >= 0 && startY >= 0 &&
            startX + request.width <= finalOutput.width() &&
            startY + request.height <= finalOutput.height()) {
            // 要求範囲を切り出し
            ImageBuffer cropped(request.width, request.height, finalOutput.formatID());
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            PerfMetrics::instance().nodes[NodeType::Filter].recordAlloc(
                cropped.totalBytes(), cropped.width(), cropped.height());
#endif
            ViewPort croppedView = cropped.view();
            ViewPort finalView = finalOutput.view();
            view_ops::copy(croppedView, 0, 0, finalView, startX, startY,
                          request.width, request.height);
            return RenderResult(std::move(cropped), Point2f(reqLeft, reqTop));
        }
    }

    return RenderResult(std::move(finalOutput), inputResult.origin);
}

// ========================================================================
// CompositeNode 評価
// ========================================================================

RenderResult Renderer::evaluateCompositeNode(CompositeNode* composite, const RenderRequest& request) {
    int inputCount = composite->inputCount();
    if (inputCount == 0) return RenderResult();

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    uint32_t compositeTime = 0;
    int compositeCount = 0;
#endif

    RenderResult canvas;
    bool canvasInitialized = false;
    float canvasOriginX = -request.originX;
    float canvasOriginY = -request.originY;

    // 逐次合成: 入力を1つずつ評価して合成
    for (int i = 0; i < inputCount; i++) {
        Node* upstream = composite->upstreamNode(i);
        if (!upstream) continue;

        RenderResult inputResult = evaluateUpstream(upstream, request);

        // 空入力はスキップ
        if (!inputResult.isValid()) continue;

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto compStart = std::chrono::high_resolution_clock::now();
#endif

        if (!canvasInitialized) {
            // 最初の非空入力 → 常に新しいキャンバスを作成
            // これにより、バッファ内基準点位置とcanvas.originが一貫する
            ImageBuffer canvasBuf(request.width, request.height,
                                  PixelFormatIDs::RGBA16_Premultiplied);
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            PerfMetrics::instance().nodes[NodeType::Composite].recordAlloc(
                canvasBuf.totalBytes(), canvasBuf.width(), canvasBuf.height());
#endif
            ViewPort canvasView = canvasBuf.view();
            ViewPort inputView = inputResult.view();

            blend::first(canvasView, request.originX, request.originY,
                        inputView, -inputResult.origin.x, -inputResult.origin.y);

            canvas = RenderResult(std::move(canvasBuf),
                                 Point2f(canvasOriginX, canvasOriginY));
            canvasInitialized = true;
        } else {
            // 2枚目以降 → ブレンド処理
            ViewPort canvasView = canvas.view();
            ViewPort inputView = inputResult.view();

            blend::onto(canvasView, -canvas.origin.x, -canvas.origin.y,
                       inputView, -inputResult.origin.x, -inputResult.origin.y);
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        compositeTime += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - compStart).count();
        compositeCount++;
#endif
    }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    if (compositeCount > 0) {
        auto& mComp = PerfMetrics::instance().nodes[NodeType::Composite];
        mComp.time_us += compositeTime;
        mComp.count += compositeCount;
    }
#endif

    // 全ての入力が空だった場合
    if (!canvasInitialized) {
        return RenderResult(ImageBuffer(), Point2f(canvasOriginX, canvasOriginY));
    }

    return canvas;
}

} // namespace FLEXIMG_NAMESPACE
