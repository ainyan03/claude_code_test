#ifndef FLEXIMG_HORIZONTAL_BLUR_NODE_H
#define FLEXIMG_HORIZONTAL_BLUR_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include <algorithm>  // for std::min, std::max
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
// - radius: ブラー半径（0-127、カーネルサイズ = 2 * radius + 1）
// - passes: ブラー適用回数（1-3、デフォルト1）
//
// マルチパス処理:
// - passes=3で3回水平ブラーを適用（ガウシアン近似）
// - 各パスは独立してブラー処理を行う（パイプライン方式）
// - pull型: 上流にマージン付き要求、下流には元サイズで返却
// - push型: 入力を拡張して下流に配布
//
// メモリ消費量:
// - 水平ブラーはスキャンライン処理のため、メモリ消費は少ない
// - 1行分のバッファ: width * 4 bytes 程度
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

    // パラメータ上限
    static constexpr int kMaxRadius = 127;  // 実用上十分、メモリ消費も許容範囲
    static constexpr int kMaxPasses = 3;    // ガウシアン近似に十分

    void setRadius(int radius) {
        radius_ = (radius < 0) ? 0 : (radius > kMaxRadius) ? kMaxRadius : radius;
    }

    void setPasses(int passes) {
        passes_ = (passes < 1) ? 1 : (passes > kMaxPasses) ? kMaxPasses : passes;
    }

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
        inputReq.origin.x = request.origin.x + to_fixed(totalMargin);  // 左側にマージン分拡張
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

        // 上流から返されたoriginを保存
        Point currentOrigin = input.origin;

        // passes回、水平ブラーを適用（各パスで拡張＋origin調整）
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

            // origin.xを左に拡張した分だけ増やす（固定小数点）
            currentOrigin.x = currentOrigin.x + to_fixed(radius_);

            buffer = std::move(output);
        }

        // origin座標を基準にクロップ位置を計算
        // currentOrigin.x が現在のbufferの左端、request.origin.x が欲しい位置
        // cropOffset = currentOrigin.x - request.origin.x（固定小数点での差分）
        // cropOffset > 0: bufferは左側にある → buffer[cropOffset]がrequest.origin.xに対応
        int_fixed offsetX = currentOrigin.x - request.origin.x;
        int cropOffset = from_fixed(offsetX);

        // 出力バッファを確保（ゼロ初期化）
        ImageBuffer output(request.width, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Zero);
        const uint8_t* srcRow = static_cast<const uint8_t*>(buffer.view().data);
        uint8_t* dstRow = static_cast<uint8_t*>(output.view().data);

        // クロップ範囲を計算（境界チェック付き）
        int srcStartX = std::max(0, cropOffset);
        int dstStartX = std::max(0, -cropOffset);
        int copyWidth = std::min(static_cast<int>(buffer.width()) - srcStartX,
                                 request.width - dstStartX);

        // 有効な範囲をコピー（範囲外は既にゼロ初期化済み）
        if (copyWidth > 0) {
            std::memcpy(dstRow + dstStartX * 4,
                       srcRow + srcStartX * 4,
                       copyWidth * 4);
        }

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
