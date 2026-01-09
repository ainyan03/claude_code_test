#include "renderer.h"
#include "operations/transform.h"
#include "operations/filters.h"
#include "operations/blend.h"
#include "pixel_format_registry.h"
#include <cstring>
#include <algorithm>
#include <cmath>

namespace FLEXIMG_NAMESPACE {

void Renderer::prepare() {
    if (!output_ || !output_->target().isValid()) return;

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
    const ViewPort& source = src->source();
    if (!source.isValid()) return RenderResult();

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
        return RenderResult(ImageBuffer(), Point2f(reqLeft, reqTop));
    }

    // 交差領域をコピー
    int srcX = static_cast<int>(interLeft - imgLeft);
    int srcY = static_cast<int>(interTop - imgTop);
    int interW = static_cast<int>(interRight - interLeft);
    int interH = static_cast<int>(interBottom - interTop);

    ImageBuffer result(interW, interH, source.formatID);
    ViewPort resultView = result.view();
    view_ops::copy(resultView, 0, 0, source, srcX, srcY, interW, interH);

    return RenderResult(std::move(result), Point2f(interLeft, interTop));
}

// ========================================================================
// TransformNode 評価
// ========================================================================

RenderResult Renderer::evaluateTransformNode(TransformNode* xform, const RenderRequest& request) {
    Node* upstream = xform->upstreamNode(0);
    if (!upstream) return RenderResult();

    const AffineMatrix& matrix = xform->matrix();

    // 逆行列を計算（入力要求範囲の算出に必要）
    float det = matrix.a * matrix.d - matrix.b * matrix.c;
    if (std::abs(det) < 1e-10f) {
        // 特異行列
        return RenderResult();
    }

    float invDet = 1.0f / det;
    float invA = matrix.d * invDet;
    float invB = -matrix.b * invDet;
    float invC = -matrix.c * invDet;
    float invD = matrix.a * invDet;
    float invTx = (-matrix.d * matrix.tx + matrix.b * matrix.ty) * invDet;
    float invTy = (matrix.c * matrix.tx - matrix.a * matrix.ty) * invDet;

    // 出力要求の4頂点を逆変換してAABBを算出
    float corners[4][2] = {
        {-request.originX, -request.originY},
        {request.width - request.originX, -request.originY},
        {-request.originX, request.height - request.originY},
        {request.width - request.originX, request.height - request.originY}
    };

    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (int i = 0; i < 4; i++) {
        float sx = invA * corners[i][0] + invB * corners[i][1] + invTx;
        float sy = invC * corners[i][0] + invD * corners[i][1] + invTy;
        minX = std::min(minX, sx);
        minY = std::min(minY, sy);
        maxX = std::max(maxX, sx);
        maxY = std::max(maxY, sy);
    }

    int reqLeft = static_cast<int>(std::floor(minX));
    int reqTop = static_cast<int>(std::floor(minY));
    int inputWidth = static_cast<int>(std::ceil(maxX) - std::floor(minX)) + 1;
    int inputHeight = static_cast<int>(std::ceil(maxY) - std::floor(minY)) + 1;

    RenderRequest inputReq;
    inputReq.width = inputWidth;
    inputReq.height = inputHeight;
    inputReq.originX = static_cast<float>(-reqLeft);
    inputReq.originY = static_cast<float>(-reqTop);

    // 上流を評価
    RenderResult inputResult = evaluateUpstream(upstream, inputReq);
    if (!inputResult.isValid()) {
        return RenderResult(ImageBuffer(), Point2f(-request.originX, -request.originY));
    }

    // 出力バッファを作成（ゼロ初期化済み）
    ImageBuffer output(request.width, request.height, inputResult.buffer.formatID());
    ViewPort outputView = output.view();

    // アフィン変換を適用
    ViewPort inputView = inputResult.view();
    transform::affine(outputView, request.originX, request.originY,
                      inputView,
                      -inputResult.origin.x, -inputResult.origin.y,
                      matrix);

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

    // 上流を評価
    RenderResult inputResult = evaluateUpstream(upstream, inputReq);
    if (!inputResult.isValid()) {
        return inputResult;
    }

    PixelFormatID inputFormatID = inputResult.buffer.formatID();
    bool needsConversion = (inputFormatID != PixelFormatIDs::RGBA8_Straight);

    // フィルタは8bit処理のため、必要に応じてRGBA8_Straightに変換
    ImageBuffer workBuffer;
    ViewPort workInputView;

    if (needsConversion) {
        // RGBA8_Straightに変換
        workBuffer = ImageBuffer(inputResult.buffer.width(), inputResult.buffer.height(),
                                 PixelFormatIDs::RGBA8_Straight);
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

    // 必要に応じて元のフォーマットに戻す
    ImageBuffer finalOutput;
    if (needsConversion) {
        finalOutput = ImageBuffer(output8bit.width(), output8bit.height(), inputFormatID);
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

        if (!canvasInitialized) {
            // 最初の非空入力 → 常に新しいキャンバスを作成
            // これにより、バッファ内基準点位置とcanvas.originが一貫する
            ImageBuffer canvasBuf(request.width, request.height,
                                  PixelFormatIDs::RGBA16_Premultiplied);
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
    }

    // 全ての入力が空だった場合
    if (!canvasInitialized) {
        return RenderResult(ImageBuffer(), Point2f(canvasOriginX, canvasOriginY));
    }

    return canvas;
}

} // namespace FLEXIMG_NAMESPACE
