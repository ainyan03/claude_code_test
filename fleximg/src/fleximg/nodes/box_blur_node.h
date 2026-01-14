#ifndef FLEXIMG_BOX_BLUR_NODE_H
#define FLEXIMG_BOX_BLUR_NODE_H

#include "filter_node_base.h"
#include "../operations/filters.h"
#include <vector>
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// BoxBlurNode - ボックスブラーフィルタノード（スキャンライン対応）
// ========================================================================
//
// 入力画像にボックスブラー（平均化フィルタ）を適用します。
// - radius: ブラー半径（カーネルサイズ = 2 * radius + 1）
//
// スキャンライン処理:
// - prepare()でキャッシュを確保
// - pullProcess()で行キャッシュと列合計を使用したスライディングウィンドウ処理
// - finalize()でキャッシュを破棄
//
// 使用例:
//   BoxBlurNode blur;
//   blur.setRadius(5);  // 半径5ピクセル
//   src >> blur >> sink;
//

class BoxBlurNode : public FilterNodeBase {
public:
    // ========================================
    // パラメータ設定
    // ========================================

    void setRadius(int radius) { radius_ = radius; }
    int radius() const { return radius_; }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "BoxBlurNode"; }

    // ========================================
    // 準備・終了処理
    // ========================================

    void prepare(const RenderRequest& screenInfo) override {
        screenWidth_ = screenInfo.width;
        screenHeight_ = screenInfo.height;
        screenOrigin_ = screenInfo.origin;

        // キャッシュサイズ: 幅は出力幅（横ブラー済み）、高さは radius*2+1
        int cacheHeight = radius_ * 2 + 1;
        cacheWidth_ = screenWidth_;  // 横ブラー済みデータなので出力幅

        // 行キャッシュを確保（横ブラー済みデータを保持）
        rowCache_.resize(cacheHeight);
        for (int i = 0; i < cacheHeight; i++) {
            rowCache_[i] = ImageBuffer(cacheWidth_, 1, PixelFormatIDs::RGBA8_Straight,
                                       InitPolicy::Zero);
        }

        // 列合計キャッシュを確保・ゼロ初期化（横ブラー済みデータの縦方向合計）
        colSumR_.assign(cacheWidth_, 0);
        colSumG_.assign(cacheWidth_, 0);
        colSumB_.assign(cacheWidth_, 0);
        colSumA_.assign(cacheWidth_, 0);

        cacheStartRow_ = 0;
        currentY_ = 0;
        cacheReady_ = false;

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        // キャッシュ確保をメトリクスに記録
        size_t cacheBytes = cacheHeight * cacheWidth_ * 4 + cacheWidth_ * 4 * sizeof(uint32_t);
        PerfMetrics::instance().nodes[NodeType::BoxBlur].recordAlloc(
            cacheBytes, cacheWidth_, cacheHeight);
#endif
    }

    void finalize() override {
        // キャッシュを破棄
        rowCache_.clear();
        colSumR_.clear();
        colSumG_.clear();
        colSumB_.clear();
        colSumA_.clear();
        cacheReady_ = false;
    }

protected:
    // BoxBlurはラインフィルタではないので独自のpullProcess()を持つ
    filters::LineFilterFunc getFilterFunc() const override { return nullptr; }
    int computeInputMargin() const override { return radius_; }
    int nodeTypeForMetrics() const override { return NodeType::BoxBlur; }

    // ========================================
    // pullProcess オーバーライド
    // ========================================

    RenderResult pullProcess(const RenderRequest& request) override {
        Node* upstream = upstreamNode(0);
        if (!upstream) return RenderResult();

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
        auto& metrics = PerfMetrics::instance().nodes[NodeType::BoxBlur];
        metrics.requestedPixels += static_cast<uint64_t>(request.width + radius_ * 2) * 1;
        metrics.usedPixels += static_cast<uint64_t>(request.width) * 1;
#endif

        int requestY = from_fixed8(request.origin.y);

        // 最初の呼び出し: キャッシュを埋める
        if (!cacheReady_) {
            initializeCache(upstream, request);
            cacheReady_ = true;
            currentY_ = requestY;
        } else {
            // 通常の呼び出し: キャッシュを更新
            updateCache(upstream, request, requestY);
            currentY_ = requestY;
        }

        // 出力バッファを確保
        ImageBuffer output(request.width, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        metrics.recordAlloc(output.totalBytes(), output.width(), output.height());
#endif

        // 水平方向スライディングウィンドウで出力行を計算
        computeOutputRow(output, request);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(output), request.origin);
    }

private:
    int radius_ = 5;

    // スクリーン情報（prepare時に記録）
    int screenWidth_ = 0;
    int screenHeight_ = 0;
    Point screenOrigin_;

    // スキャンライン処理用キャッシュ
    std::vector<ImageBuffer> rowCache_;      // radius*2+1 行分（リングバッファ）
    std::vector<uint32_t> colSumR_;          // 列合計（R * A）
    std::vector<uint32_t> colSumG_;          // 列合計（G * A）
    std::vector<uint32_t> colSumB_;          // 列合計（B * A）
    std::vector<uint32_t> colSumA_;          // 列合計（A）
    int cacheWidth_ = 0;                     // キャッシュ幅
    int cacheStartRow_ = 0;                  // リングバッファの先頭インデックス
    int currentY_ = 0;                       // 現在の出力Y座標
    bool cacheReady_ = false;                // キャッシュ初期化済みフラグ

    // ========================================
    // キャッシュ管理
    // ========================================

    // キャッシュを初期化（最初のpullProcess呼び出し時）
    void initializeCache(Node* upstream, const RenderRequest& request) {
        int requestY = from_fixed8(request.origin.y);
        int kernelSize = radius_ * 2 + 1;

        // 列合計をリセット
        std::fill(colSumR_.begin(), colSumR_.end(), 0);
        std::fill(colSumG_.begin(), colSumG_.end(), 0);
        std::fill(colSumB_.begin(), colSumB_.end(), 0);
        std::fill(colSumA_.begin(), colSumA_.end(), 0);

        // radius*2+1 行分を上流から取得してキャッシュに格納
        for (int i = 0; i < kernelSize; i++) {
            int srcY = requestY - radius_ + i;
            fetchRowToCache(upstream, request, srcY, i);
            addRowToColSum(i);
        }
        cacheStartRow_ = 0;
    }

    // キャッシュを更新（2回目以降のpullProcess呼び出し時）
    void updateCache(Node* upstream, const RenderRequest& request, int newY) {
        int kernelSize = radius_ * 2 + 1;

        // Y座標が増加する方向（昇順）
        while (currentY_ < newY) {
            // 古い行（上端）を列合計から減算
            subtractRowFromColSum(cacheStartRow_);

            // 新しい行（下端）を取得
            int newSrcY = currentY_ + radius_ + 1;
            fetchRowToCache(upstream, request, newSrcY, cacheStartRow_);

            // 新しい行を列合計に加算
            addRowToColSum(cacheStartRow_);

            // リングバッファを進める
            cacheStartRow_ = (cacheStartRow_ + 1) % kernelSize;
            currentY_++;
        }

        // Y座標が減少する方向（降順）- RendererNodeの座標系対応
        while (currentY_ > newY) {
            // 古い行（下端）のインデックスを計算
            int oldIndex = (cacheStartRow_ + kernelSize - 1) % kernelSize;

            // 古い行を列合計から減算
            subtractRowFromColSum(oldIndex);

            // 新しい行（上端）を取得
            int newSrcY = currentY_ - radius_ - 1;

            // リングバッファを逆方向に進める（先に進める）
            cacheStartRow_ = (cacheStartRow_ + kernelSize - 1) % kernelSize;

            // 新しい行をcacheStartRow_に格納
            fetchRowToCache(upstream, request, newSrcY, cacheStartRow_);

            // 新しい行を列合計に加算
            addRowToColSum(cacheStartRow_);

            currentY_--;
        }
    }

    // 上流から1行取得し、横方向ブラー処理してキャッシュに格納
    void fetchRowToCache(Node* upstream, const RenderRequest& request, int srcY, int cacheIndex) {
        int inputWidth = request.width + radius_ * 2;  // 入力幅（マージン含む）
        int outputWidth = request.width;               // 出力幅（ブラー後）

        // 上流への要求を作成（1ライン、マージン含む幅）
        // RendererNodeの座標系: origin.xが大きいほど左
        RenderRequest upstreamReq;
        upstreamReq.width = inputWidth;
        upstreamReq.height = 1;
        upstreamReq.origin.x = request.origin.x + to_fixed8(radius_);
        upstreamReq.origin.y = to_fixed8(srcY);

        RenderResult result = upstream->pullProcess(upstreamReq);

        // キャッシュ行をゼロクリア
        ViewPort dstView = rowCache_[cacheIndex].view();
        std::memset(dstView.data, 0, outputWidth * 4);

        if (!result.isValid()) {
            return;  // 無効な場合は透明行として扱う
        }

        ImageBuffer converted = convertFormat(std::move(result.buffer),
                                               PixelFormatIDs::RGBA8_Straight);
        ViewPort srcView = converted.view();

        // 入力データを一時バッファにコピー（オフセット考慮）
        // 座標系: origin.x が大きいほど左
        // srcOffsetX > 0: 結果は要求より右側から始まる → inputRowの右側にコピー
        std::vector<uint8_t> inputRow(inputWidth * 4, 0);
        int srcOffsetX = from_fixed8(upstreamReq.origin.x - result.origin.x);
        int dstStartX = std::max(0, srcOffsetX);
        int srcStartX = std::max(0, -srcOffsetX);
        int copyWidth = std::min(srcView.width - srcStartX, inputWidth - dstStartX);
        if (copyWidth > 0) {
            const uint8_t* srcPtr = static_cast<const uint8_t*>(srcView.data) + srcStartX * 4;
            std::memcpy(inputRow.data() + dstStartX * 4, srcPtr, copyWidth * 4);
        }

        // 横方向スライディングウィンドウでブラー処理
        applyHorizontalBlur(inputRow.data(), inputWidth,
                           static_cast<uint8_t*>(dstView.data), outputWidth);
    }

    // 横方向スライディングウィンドウブラー
    void applyHorizontalBlur(const uint8_t* input, int inputWidth,
                             uint8_t* output, int outputWidth) {
        int kernelSize = radius_ * 2 + 1;

        // 初期ウィンドウの合計（出力x=0に対応）
        uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;
        for (int nx = 0; nx < kernelSize; nx++) {
            int off = nx * 4;
            uint32_t a = input[off + 3];
            sumR += input[off] * a;
            sumG += input[off + 1] * a;
            sumB += input[off + 2] * a;
            sumA += a;
        }

        // x=0 の出力
        writeBlurredPixel(output, 0, sumR, sumG, sumB, sumA, kernelSize);

        // スライディング: x = 1 to outputWidth-1
        for (int x = 1; x < outputWidth; x++) {
            int oldX = x - 1;                    // 出ていくピクセル
            int newX = x + radius_ * 2;          // 入ってくるピクセル

            // 出ていくピクセルを減算
            {
                int off = oldX * 4;
                uint32_t a = input[off + 3];
                sumR -= input[off] * a;
                sumG -= input[off + 1] * a;
                sumB -= input[off + 2] * a;
                sumA -= a;
            }

            // 入ってくるピクセルを加算
            if (newX < inputWidth) {
                int off = newX * 4;
                uint32_t a = input[off + 3];
                sumR += input[off] * a;
                sumG += input[off + 1] * a;
                sumB += input[off + 2] * a;
                sumA += a;
            }

            writeBlurredPixel(output, x, sumR, sumG, sumB, sumA, kernelSize);
        }
    }

    // ブラー済みピクセルを書き込み（横方向用）
    void writeBlurredPixel(uint8_t* row, int x, uint32_t sumR, uint32_t sumG,
                           uint32_t sumB, uint32_t sumA, int count) {
        int off = x * 4;
        if (sumA > 0) {
            row[off]     = static_cast<uint8_t>(sumR / sumA);
            row[off + 1] = static_cast<uint8_t>(sumG / sumA);
            row[off + 2] = static_cast<uint8_t>(sumB / sumA);
            row[off + 3] = static_cast<uint8_t>(sumA / count);
        } else {
            row[off] = row[off + 1] = row[off + 2] = row[off + 3] = 0;
        }
    }

    // 指定行を列合計に加算（α加重平均のため、RGB×αで蓄積）
    void addRowToColSum(int cacheIndex) {
        const uint8_t* row = static_cast<const uint8_t*>(rowCache_[cacheIndex].view().data);
        for (int x = 0; x < cacheWidth_; x++) {
            int off = x * 4;
            uint32_t a = row[off + 3];
            colSumR_[x] += row[off] * a;
            colSumG_[x] += row[off + 1] * a;
            colSumB_[x] += row[off + 2] * a;
            colSumA_[x] += a;
        }
    }

    // 指定行を列合計から減算（α加重平均のため、RGB×αで減算）
    void subtractRowFromColSum(int cacheIndex) {
        const uint8_t* row = static_cast<const uint8_t*>(rowCache_[cacheIndex].view().data);
        for (int x = 0; x < cacheWidth_; x++) {
            int off = x * 4;
            uint32_t a = row[off + 3];
            colSumR_[x] -= row[off] * a;
            colSumG_[x] -= row[off + 1] * a;
            colSumB_[x] -= row[off + 2] * a;
            colSumA_[x] -= a;
        }
    }

    // 縦方向の列合計から出力行を計算（α加重平均）
    void computeOutputRow(ImageBuffer& output, const RenderRequest& request) {
        uint8_t* outRow = static_cast<uint8_t*>(output.view().data);
        int kernelSize = radius_ * 2 + 1;

        for (int x = 0; x < request.width; x++) {
            int off = x * 4;
            uint32_t sumA = colSumA_[x];
            if (sumA > 0) {
                outRow[off]     = static_cast<uint8_t>(colSumR_[x] / sumA);
                outRow[off + 1] = static_cast<uint8_t>(colSumG_[x] / sumA);
                outRow[off + 2] = static_cast<uint8_t>(colSumB_[x] / sumA);
                outRow[off + 3] = static_cast<uint8_t>(sumA / kernelSize);
            } else {
                outRow[off] = outRow[off + 1] = outRow[off + 2] = outRow[off + 3] = 0;
            }
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_BOX_BLUR_NODE_H
