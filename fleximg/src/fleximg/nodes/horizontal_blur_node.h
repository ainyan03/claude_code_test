#ifndef FLEXIMG_HORIZONTAL_BLUR_NODE_H
#define FLEXIMG_HORIZONTAL_BLUR_NODE_H

#include "filter_node_base.h"
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// HorizontalBlurNode - 水平方向ブラーフィルタノード
// ========================================================================
//
// 入力画像に水平方向のボックスブラー（平均化フィルタ）を適用します。
// - radius: ブラー半径（カーネルサイズ = 2 * radius + 1）
//
// スキャンライン処理:
// - 1行完結の処理（キャッシュ不要）
// - 入力マージン: radius（左右に拡張）
// - スライディングウィンドウで水平方向のみブラー
//
// 使用例:
//   HorizontalBlurNode hblur;
//   hblur.setRadius(5);
//   src >> hblur >> sink;
//
// VerticalBlurNodeと組み合わせてボックスブラーを実現:
//   src >> hblur >> vblur >> sink;  // HorizontalBlur → VerticalBlur の順が効率的
//

class HorizontalBlurNode : public FilterNodeBase {
public:
    // ========================================
    // パラメータ設定
    // ========================================

    void setRadius(int radius) { radius_ = radius; }
    int radius() const { return radius_; }
    int kernelSize() const { return radius_ * 2 + 1; }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "HorizontalBlurNode"; }

protected:
    // FilterNodeBaseは使用しない（独自のprocess実装）
    filters::LineFilterFunc getFilterFunc() const override { return nullptr; }
    int computeInputMargin() const override { return radius_; }
    int nodeTypeForMetrics() const override { return NodeType::HorizontalBlur; }

    // ========================================
    // pullProcess オーバーライド
    // ========================================

    RenderResult pullProcess(const RenderRequest& request) override {
        Node* upstream = upstreamNode(0);
        if (!upstream) return RenderResult();

        // radius=0の場合は処理をスキップしてスルー出力
        if (radius_ == 0) {
            return upstream->pullProcess(request);
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
        auto& metrics = PerfMetrics::instance().nodes[NodeType::HorizontalBlur];
        metrics.requestedPixels += static_cast<uint64_t>(request.width + radius_ * 2) * 1;
        metrics.usedPixels += static_cast<uint64_t>(request.width) * 1;
#endif

        // 上流への要求（マージン含む）
        RenderRequest inputReq;
        inputReq.width = request.width + radius_ * 2;
        inputReq.height = 1;
        inputReq.origin.x = request.origin.x + to_fixed(radius_);
        inputReq.origin.y = request.origin.y;

        RenderResult input = upstream->pullProcess(inputReq);
        if (!input.isValid()) return input;

        // RGBA8_Straightに変換
        ImageBuffer converted = convertFormat(std::move(input.buffer),
                                               PixelFormatIDs::RGBA8_Straight);
        ViewPort srcView = converted.view();

        // 出力バッファを確保
        ImageBuffer output(request.width, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        metrics.recordAlloc(output.totalBytes(), output.width(), output.height());
#endif

        // オフセット計算
        int srcOffsetX = from_fixed(inputReq.origin.x - input.origin.x);

        // 水平方向スライディングウィンドウでブラー処理
        applyHorizontalBlur(srcView, srcOffsetX, output);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(output), request.origin);
    }

    // ========================================
    // process オーバーライド（push型用）
    // ========================================

    RenderResult process(RenderResult&& input,
                        const RenderRequest& request) override {
        // radius=0の場合はスルー
        if (radius_ == 0) {
            return std::move(input);
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
#endif

        // RGBA8_Straightに変換
        ImageBuffer converted = convertFormat(std::move(input.buffer),
                                               PixelFormatIDs::RGBA8_Straight);
        ViewPort srcView = converted.view();

        // 出力バッファを確保
        ImageBuffer output(request.width, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized);

        // オフセット計算（push型では通常0）
        int srcOffsetX = 0;

        // 水平方向スライディングウィンドウでブラー処理
        applyHorizontalBlur(srcView, srcOffsetX, output);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::HorizontalBlur];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(output), request.origin);
    }

private:
    int radius_ = 5;

    // ========================================
    // 水平方向ブラー処理
    // ========================================

    void applyHorizontalBlur(const ViewPort& srcView, int srcOffsetX, ImageBuffer& output) {
        const uint8_t* srcRow = static_cast<const uint8_t*>(srcView.data);
        uint8_t* dstRow = static_cast<uint8_t*>(output.view().data);
        int inputWidth = srcView.width;
        int outputWidth = output.width();

        // 初期ウィンドウの合計（出力x=0に対応）
        // カーネル範囲: 入力[srcOffsetX + radius_ - radius_, srcOffsetX + radius_ + radius_]
        //             = 入力[srcOffsetX, srcOffsetX + 2*radius_]
        uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;
        int inputOffset = srcOffsetX + radius_;  // 出力x=0に対応するカーネル中心

        for (int kx = -radius_; kx <= radius_; kx++) {
            int srcX = inputOffset + kx;
            if (srcX >= 0 && srcX < inputWidth) {
                int off = srcX * 4;
                uint32_t a = srcRow[off + 3];
                sumR += srcRow[off] * a;
                sumG += srcRow[off + 1] * a;
                sumB += srcRow[off + 2] * a;
                sumA += a;
            }
        }
        writeBlurredPixel(dstRow, 0, sumR, sumG, sumB, sumA);

        // スライディング: x = 1 to outputWidth-1
        for (int x = 1; x < outputWidth; x++) {
            // 出ていくピクセル
            int oldSrcX = inputOffset + x - 1 - radius_;
            if (oldSrcX >= 0 && oldSrcX < inputWidth) {
                int off = oldSrcX * 4;
                uint32_t a = srcRow[off + 3];
                sumR -= srcRow[off] * a;
                sumG -= srcRow[off + 1] * a;
                sumB -= srcRow[off + 2] * a;
                sumA -= a;
            }

            // 入ってくるピクセル
            int newSrcX = inputOffset + x + radius_;
            if (newSrcX >= 0 && newSrcX < inputWidth) {
                int off = newSrcX * 4;
                uint32_t a = srcRow[off + 3];
                sumR += srcRow[off] * a;
                sumG += srcRow[off + 1] * a;
                sumB += srcRow[off + 2] * a;
                sumA += a;
            }

            writeBlurredPixel(dstRow, x, sumR, sumG, sumB, sumA);
        }
    }

    // ブラー済みピクセルを書き込み
    void writeBlurredPixel(uint8_t* row, int x, uint32_t sumR, uint32_t sumG,
                           uint32_t sumB, uint32_t sumA) {
        int off = x * 4;
        int ks = kernelSize();
        if (sumA > 0) {
            row[off]     = static_cast<uint8_t>(sumR / sumA);
            row[off + 1] = static_cast<uint8_t>(sumG / sumA);
            row[off + 2] = static_cast<uint8_t>(sumB / sumA);
            row[off + 3] = static_cast<uint8_t>(sumA / ks);
        } else {
            row[off] = row[off + 1] = row[off + 2] = row[off + 3] = 0;
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_HORIZONTAL_BLUR_NODE_H
