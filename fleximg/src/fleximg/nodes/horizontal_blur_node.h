#ifndef FLEXIMG_HORIZONTAL_BLUR_NODE_H
#define FLEXIMG_HORIZONTAL_BLUR_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include <algorithm>  // for std::clamp
#include <cstring>    // for std::memcpy
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
// - passes: ブラー適用回数（1-5、デフォルト1）
//
// マルチパス処理:
// - passes=3で3回水平ブラーを適用（ガウシアン近似）
// - pull型: 上流にマージン付き要求、下流には元サイズで返却
// - push型: 入力を拡張して下流に配布
//
// pull型の動作:
// - 上流への要求: width + radius*2*passes（マージン確保）
// - 処理後に中央部分をクロップ
// - 下流への返却: 要求されたwidthそのまま
//
// push型の動作:
// - 入力を passes 回ブラー（各パスで width + radius*2 ずつ拡張）
// - 拡張された結果を下流に配布
//
// スキャンライン処理:
// - 1行完結の処理（キャッシュ不要）
// - スライディングウィンドウで水平方向のみブラー
//
// 使用例:
//   HorizontalBlurNode hblur;
//   hblur.setRadius(6);
//   hblur.setPasses(3);  // ガウシアン近似
//   src >> hblur >> sink;
//
// VerticalBlurNodeと組み合わせて2次元ガウシアン近似:
//   src >> hblur(r=6, p=3) >> vblur(r=6, p=3) >> sink;
//

class HorizontalBlurNode : public Node {
public:
    HorizontalBlurNode() {
        initPorts(1, 1);
    }

    // ========================================
    // パラメータ設定
    // ========================================

    void setRadius(int radius) { radius_ = radius; }
    void setPasses(int passes) { passes_ = std::clamp(passes, 1, 5); }

    int radius() const { return radius_; }
    int passes() const { return passes_; }
    int kernelSize() const { return radius_ * 2 + 1; }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "HorizontalBlurNode"; }

protected:
    int nodeTypeForMetrics() const override { return NodeType::HorizontalBlur; }

    // ========================================
    // pullProcess オーバーライド
    // ========================================

    RenderResult pullProcess(const RenderRequest& request) override {
        Node* upstream = upstreamNode(0);
        if (!upstream) return RenderResult();

        // radius=0またはpasses=0の場合は処理をスキップしてスルー出力
        if (radius_ == 0 || passes_ == 0) {
            return upstream->pullProcess(request);
        }

        // マージンを計算して上流への要求を拡大
        int totalMargin = radius_ * passes_;  // 片側のマージン
        RenderRequest inputReq;
        inputReq.width = request.width + totalMargin * 2;  // 両側にマージンを追加
        inputReq.height = 1;
        inputReq.origin.x = request.origin.x;
        inputReq.origin.y = request.origin.y;

        RenderResult input = upstream->pullProcess(inputReq);
        if (!input.isValid()) return input;

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
        auto& metrics = PerfMetrics::instance().nodes[NodeType::HorizontalBlur];
        metrics.requestedPixels += static_cast<uint64_t>(request.width) * 1;
        metrics.usedPixels += static_cast<uint64_t>(inputReq.width) * 1;
#endif

        // RGBA8_Straightに変換
        ImageBuffer buffer = convertFormat(std::move(input.buffer),
                                           PixelFormatIDs::RGBA8_Straight);

        // passes回、水平ブラーを適用（各パスで拡張）
        for (int pass = 0; pass < passes_; pass++) {
            ViewPort srcView = buffer.view();
            int inputWidth = srcView.width;
            int outputWidth = inputWidth + radius_ * 2;

#ifdef FLEXIMG_DEBUG_PERF_METRICS
            if (pass == 0) {
                metrics.recordAlloc(outputWidth * 4, outputWidth, 1);
            }
#endif

            // 出力バッファを確保
            ImageBuffer output(outputWidth, 1, PixelFormatIDs::RGBA8_Straight,
                              InitPolicy::Uninitialized);

            // 水平方向スライディングウィンドウでブラー処理
            // inputOffset = -radius (出力を左に拡張)
            applyHorizontalBlur(srcView, -radius_, output);

            buffer = std::move(output);
        }

        // 最終サイズ = (request.width + totalMargin*2) + radius*2*passes
        //            = request.width + totalMargin*4
        // これを request.width にクロップする
        int finalWidth = buffer.width();
        int cropOffset = totalMargin * 2;  // 左右から totalMargin*2 ずつ削る

        // 中央部分を切り出し
        ImageBuffer output(request.width, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized);
        const uint8_t* srcRow = static_cast<const uint8_t*>(buffer.view().data);
        uint8_t* dstRow = static_cast<uint8_t*>(output.view().data);
        std::memcpy(dstRow, srcRow + cropOffset * 4, request.width * 4);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(output), request.origin);
    }

    // ========================================
    // pushProcess オーバーライド
    // ========================================

    void pushProcess(RenderResult&& input, const RenderRequest& request) override {
        // radius=0またはpasses=0の場合はスルー
        if (radius_ == 0 || passes_ == 0) {
            Node* downstream = downstreamNode(0);
            if (downstream) {
                downstream->pushProcess(std::move(input), request);
            }
            return;
        }

        if (!input.isValid()) {
            Node* downstream = downstreamNode(0);
            if (downstream) {
                downstream->pushProcess(std::move(input), request);
            }
            return;
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
#endif

        // RGBA8_Straightに変換
        ImageBuffer buffer = convertFormat(std::move(input.buffer),
                                           PixelFormatIDs::RGBA8_Straight);
        Point currentOrigin = input.origin;

        // passes回、水平ブラーを適用
        for (int pass = 0; pass < passes_; pass++) {
            ViewPort srcView = buffer.view();
            int inputWidth = srcView.width;
            int outputWidth = inputWidth + radius_ * 2;

            // 出力バッファを確保
            ImageBuffer output(outputWidth, 1, PixelFormatIDs::RGBA8_Straight,
                              InitPolicy::Uninitialized);

            // 水平方向スライディングウィンドウでブラー処理
            // push型では inputOffset = -radius
            applyHorizontalBlur(srcView, -radius_, output);

            // origin.xをradius分大きくする（左に拡張するため）
            currentOrigin.x = currentOrigin.x + to_fixed(radius_);

            buffer = std::move(output);
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::HorizontalBlur];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        // 下流にpush
        Node* downstream = downstreamNode(0);
        if (downstream) {
            RenderRequest outReq = request;
            outReq.width = static_cast<int16_t>(buffer.width());
            downstream->pushProcess(RenderResult(std::move(buffer), currentOrigin), outReq);
        }
    }

private:
    int radius_ = 5;
    int passes_ = 1;  // 1-5の範囲、デフォルト1（従来互換）

    // ========================================
    // 水平方向ブラー処理（pull型用）
    // ========================================

    // 水平方向ブラー処理（共通）
    // inputOffset: 出力x=0に対応する入力のカーネル中心位置
    void applyHorizontalBlur(const ViewPort& srcView, int inputOffset, ImageBuffer& output) {
        const uint8_t* srcRow = static_cast<const uint8_t*>(srcView.data);
        uint8_t* dstRow = static_cast<uint8_t*>(output.view().data);
        int inputWidth = srcView.width;
        int outputWidth = output.width();

        // 初期ウィンドウの合計（出力x=0に対応）
        uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;

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
