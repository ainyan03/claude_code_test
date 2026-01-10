#ifndef FLEXIMG_FILTER_NODE_H
#define FLEXIMG_FILTER_NODE_H

#include "../node.h"
#include "../image_buffer.h"
#include "../operations/filters.h"
#include "../pixel_format_registry.h"
#include "../perf_metrics.h"
#include <string>
#include <vector>
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// FilterType - フィルタ種別
// ========================================================================

enum class FilterType {
    None,
    Brightness,
    Grayscale,
    BoxBlur,
    Alpha
};

// ========================================================================
// FilterNode - フィルタノード
// ========================================================================
//
// 入力画像に対してフィルタ処理を適用します。
// - 入力: 1ポート
// - 出力: 1ポート
//
// 使用例:
//   FilterNode blur;
//   blur.setBoxBlur(5);
//   src >> blur >> sink;
//

class FilterNode : public Node {
public:
    FilterNode() {
        initPorts(1, 1);  // 入力1、出力1
    }

    // ========================================
    // フィルタ設定
    // ========================================

    FilterType filterType() const { return filterType_; }

    // 明るさ調整
    void setBrightness(float amount) {
        filterType_ = FilterType::Brightness;
        param1_ = amount;
    }
    float brightnessAmount() const { return param1_; }

    // グレースケール
    void setGrayscale() {
        filterType_ = FilterType::Grayscale;
    }

    // ボックスブラー
    void setBoxBlur(int radius) {
        filterType_ = FilterType::BoxBlur;
        param1_ = static_cast<float>(radius);
    }
    int blurRadius() const { return static_cast<int>(param1_); }

    // アルファ調整
    void setAlpha(float scale) {
        filterType_ = FilterType::Alpha;
        param1_ = scale;
    }
    float alphaScale() const { return param1_; }

    // カーネル半径（ブラー用、入力要求計算に使用）
    int kernelRadius() const {
        if (filterType_ == FilterType::BoxBlur) {
            return static_cast<int>(param1_);
        }
        return 0;
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "FilterNode"; }

    // ========================================
    // プル型インターフェース
    // ========================================

    // ブラーの場合は入力要求を拡大するため、pullProcess()を直接オーバーライド
    RenderResult pullProcess(const RenderRequest& request) override {
        Node* upstream = upstreamNode(0);
        if (!upstream) return RenderResult();

        // 入力要求を計算（ブラーの場合は拡大）
        int margin = kernelRadius();
        RenderRequest inputReq = request.expand(margin);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        // ピクセル効率計測
        auto& mFilt = PerfMetrics::instance().nodes[NodeType::Filter];
        mFilt.requestedPixels += static_cast<uint64_t>(inputReq.width) * inputReq.height;
        mFilt.usedPixels += static_cast<uint64_t>(request.width) * request.height;
#endif

        // 上流を評価（新APIを使用）
        RenderResult inputResult = upstream->pullProcess(inputReq);
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
        switch (filterType_) {
            case FilterType::Brightness:
                filters::brightness(outputView, workInputView, param1_);
                break;
            case FilterType::Grayscale:
                filters::grayscale(outputView, workInputView);
                break;
            case FilterType::BoxBlur:
                filters::boxBlur(outputView, workInputView, static_cast<int>(param1_));
                break;
            case FilterType::Alpha:
                filters::alpha(outputView, workInputView, param1_);
                break;
            case FilterType::None:
            default:
                view_ops::copy(outputView, 0, 0, workInputView, 0, 0,
                              workInputView.width, workInputView.height);
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

            if (startX >= 0 && startY >= 0 &&
                startX + request.width <= finalOutput.width() &&
                startY + request.height <= finalOutput.height()) {
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

private:
    FilterType filterType_ = FilterType::None;
    float param1_ = 0.0f;
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_FILTER_NODE_H
