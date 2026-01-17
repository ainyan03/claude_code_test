#ifndef FLEXIMG_HORIZONTAL_BLUR_NODE_H
#define FLEXIMG_HORIZONTAL_BLUR_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// HorizontalBlurNode - 水平方向ブラーフィルタノード
// ========================================================================
//
// 入力画像に水平方向のStack Blur（三角形重み分布フィルタ）を適用します。
// - radius: ブラー半径（カーネルサイズ = 2 * radius + 1）
//
// Stack Blurアルゴリズム:
// - 三角形の重み分布を使用（中心が最も重く、端に向かって線形減衰）
// - ガウシアンブラーに近い自然な仕上がり
// - O(n)の計算量（半径に依存しない高速処理）
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
// VerticalBlurNodeと組み合わせて2Dブラーを実現:
//   src >> hblur >> vblur >> sink;  // HorizontalBlur → VerticalBlur の順が効率的
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
    int radius() const { return radius_; }
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

        // radius=0の場合は処理をスキップしてスルー出力
        if (radius_ == 0) {
            return upstream->pullProcess(request);
        }

        // 上流への要求（マージン含む）
        RenderRequest inputReq;
        inputReq.width = request.width + radius_ * 2;
        inputReq.height = 1;
        inputReq.origin.x = request.origin.x + to_fixed(radius_);
        inputReq.origin.y = request.origin.y;

        RenderResult input = upstream->pullProcess(inputReq);
        if (!input.isValid()) return input;

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
        auto& metrics = PerfMetrics::instance().nodes[NodeType::HorizontalBlur];
        metrics.requestedPixels += static_cast<uint64_t>(request.width + radius_ * 2) * 1;
        metrics.usedPixels += static_cast<uint64_t>(request.width) * 1;
#endif

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
        // srcOffsetX = inputReq.origin.x - input.origin.x
        // 出力x=0のカーネル中心 = radius_ - srcOffsetX
        int srcOffsetX = from_fixed(inputReq.origin.x - input.origin.x);
        int inputOffset = radius_ - srcOffsetX;

        // 水平方向スライディングウィンドウでブラー処理
        applyHorizontalBlur(srcView, inputOffset, output);

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
        // radius=0の場合はスルー
        if (radius_ == 0) {
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
        ImageBuffer converted = convertFormat(std::move(input.buffer),
                                               PixelFormatIDs::RGBA8_Straight);
        ViewPort srcView = converted.view();

        // 出力サイズ = 入力サイズ + radius*2（左右に拡張）
        int inputWidth = srcView.width;
        int outputWidth = inputWidth + radius_ * 2;

        // 出力バッファを確保
        ImageBuffer output(outputWidth, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized);

        // 水平方向スライディングウィンドウでブラー処理
        // push型では inputOffset = -radius
        // 出力x=radius のカーネル中心 = -radius + radius = 0（入力x=0）
        // これにより出力x=radiusのブラー結果が入力x=0を中心としたものになり、
        // origin.x + radius と整合性が取れる
        applyHorizontalBlur(srcView, -radius_, output);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::HorizontalBlur];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        // 下流にpush
        // origin.xをradius分大きくする（左に拡張するため）
        Node* downstream = downstreamNode(0);
        if (downstream) {
            RenderRequest outReq = request;
            outReq.width = static_cast<int16_t>(outputWidth);
            Point outOrigin;
            outOrigin.x = input.origin.x + to_fixed(radius_);
            outOrigin.y = input.origin.y;
            downstream->pushProcess(RenderResult(std::move(output), outOrigin), outReq);
        }
    }

private:
    int radius_ = 5;

    // ========================================
    // 水平方向ブラー処理（Stack Blur方式）
    // ========================================

    // 水平方向Stack Blur処理（共通）
    // inputOffset: 出力x=0に対応する入力のカーネル中心位置
    void applyHorizontalBlur(const ViewPort& srcView, int inputOffset, ImageBuffer& output) {
        const uint8_t* srcRow = static_cast<const uint8_t*>(srcView.data);
        uint8_t* dstRow = static_cast<uint8_t*>(output.view().data);
        int inputWidth = srcView.width;
        int outputWidth = output.width();

        // Stack Blur: 重みの合計 = (radius+1)^2
        int div = (radius_ + 1) * (radius_ + 1);

        // Stack用の累積値（α加重）
        uint64_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;
        uint64_t sumInR = 0, sumInG = 0, sumInB = 0, sumInA = 0;
        uint64_t sumOutR = 0, sumOutG = 0, sumOutB = 0, sumOutA = 0;

        // 初期化：最初のカーネル位置（出力x=0）の重み付き合計を計算
        for (int kx = -radius_; kx <= radius_; kx++) {
            int srcX = inputOffset + kx;
            uint32_t r = 0, g = 0, b = 0, a = 0;

            if (srcX >= 0 && srcX < inputWidth) {
                int off = srcX * 4;
                a = srcRow[off + 3];
                r = srcRow[off] * a;
                g = srcRow[off + 1] * a;
                b = srcRow[off + 2] * a;
            }

            // Stack Blurの重み付け
            int weight = radius_ + 1 - std::abs(kx);

            // 左側（kx <= 0）はsumOutに蓄積
            if (kx < 0) {
                sumOutR += r;
                sumOutG += g;
                sumOutB += b;
                sumOutA += a;
            }
            // 右側（kx > 0）はsumInに蓄積
            if (kx > 0) {
                sumInR += r;
                sumInG += g;
                sumInB += b;
                sumInA += a;
            }

            // 全体の合計に重み付きで加算
            sumR += r * weight;
            sumG += g * weight;
            sumB += b * weight;
            sumA += a * weight;
        }

        // 最初のピクセルを出力
        writeBlurredPixel(dstRow, 0, sumR, sumG, sumB, sumA, div);

        // スライディング：x = 1 to outputWidth-1
        for (int x = 1; x < outputWidth; x++) {
            // 出ていくピクセル（左端）
            int oldSrcX = inputOffset + x - 1 - radius_;
            uint32_t oldR = 0, oldG = 0, oldB = 0, oldA = 0;
            if (oldSrcX >= 0 && oldSrcX < inputWidth) {
                int off = oldSrcX * 4;
                oldA = srcRow[off + 3];
                oldR = srcRow[off] * oldA;
                oldG = srcRow[off + 1] * oldA;
                oldB = srcRow[off + 2] * oldA;
            }

            // 入ってくるピクセル（右端）
            int newSrcX = inputOffset + x + radius_;
            uint32_t newR = 0, newG = 0, newB = 0, newA = 0;
            if (newSrcX >= 0 && newSrcX < inputWidth) {
                int off = newSrcX * 4;
                newA = srcRow[off + 3];
                newR = srcRow[off] * newA;
                newG = srcRow[off + 1] * newA;
                newB = srcRow[off + 2] * newA;
            }

            // Stack Blur スライディング更新
            sumR = sumR - sumOutR + sumInR;
            sumG = sumG - sumOutG + sumInG;
            sumB = sumB - sumOutB + sumInB;
            sumA = sumA - sumOutA + sumInA;

            // sumOutの更新
            sumOutR = sumOutR - oldR;
            sumOutG = sumOutG - oldG;
            sumOutB = sumOutB - oldB;
            sumOutA = sumOutA - oldA;

            // 前の中心ピクセルをsumOutに追加
            int prevCenterX = inputOffset + x - 1;
            uint32_t prevCenterR = 0, prevCenterG = 0, prevCenterB = 0, prevCenterA = 0;
            if (prevCenterX >= 0 && prevCenterX < inputWidth) {
                int off = prevCenterX * 4;
                prevCenterA = srcRow[off + 3];
                prevCenterR = srcRow[off] * prevCenterA;
                prevCenterG = srcRow[off + 1] * prevCenterA;
                prevCenterB = srcRow[off + 2] * prevCenterA;
            }
            sumOutR += prevCenterR;
            sumOutG += prevCenterG;
            sumOutB += prevCenterB;
            sumOutA += prevCenterA;

            // sumInの更新（新しいピクセルを追加、前の中心を削除）
            sumInR = sumInR + newR - prevCenterR;
            sumInG = sumInG + newG - prevCenterG;
            sumInB = sumInB + newB - prevCenterB;
            sumInA = sumInA + newA - prevCenterA;

            writeBlurredPixel(dstRow, x, sumR, sumG, sumB, sumA, div);
        }
    }

    // ブラー済みピクセルを書き込み（Stack Blur用）
    void writeBlurredPixel(uint8_t* row, int x, uint64_t sumR, uint64_t sumG,
                           uint64_t sumB, uint64_t sumA, int div) {
        int off = x * 4;
        if (sumA > 0) {
            row[off]     = static_cast<uint8_t>(sumR / sumA);
            row[off + 1] = static_cast<uint8_t>(sumG / sumA);
            row[off + 2] = static_cast<uint8_t>(sumB / sumA);
            row[off + 3] = static_cast<uint8_t>(sumA / div);
        } else {
            row[off] = row[off + 1] = row[off + 2] = row[off + 3] = 0;
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_HORIZONTAL_BLUR_NODE_H
