#ifndef FLEXIMG_VERTICAL_BLUR_NODE_H
#define FLEXIMG_VERTICAL_BLUR_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include <vector>
#include <algorithm>
#include <cstring>
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// VerticalBlurNode - 垂直方向ブラーフィルタノード（スキャンライン対応）
// ========================================================================
//
// 入力画像に垂直方向のボックスブラー（平均化フィルタ）を適用します。
// - radius: ブラー半径（カーネルサイズ = 2 * radius + 1）
//
// スキャンライン処理:
// - prepare()でキャッシュを確保
// - pullProcess()で行キャッシュと列合計を使用したスライディングウィンドウ処理
// - finalize()でキャッシュを破棄
// - 入力マージン: 0（幅方向は拡張不要）
//
// 使用例:
//   VerticalBlurNode vblur;
//   vblur.setRadius(5);
//   src >> vblur >> sink;
//
// HorizontalBlurNodeと組み合わせてボックスブラーを実現:
//   src >> hblur >> vblur >> sink;  // HorizontalBlur → VerticalBlur の順が効率的
//

class VerticalBlurNode : public Node {
public:
    VerticalBlurNode() {
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

    const char* name() const override { return "VerticalBlurNode"; }

    // ========================================
    // 準備・終了処理（pull型用）
    // ========================================

    void prepare(const RenderRequest& screenInfo) override {
        screenWidth_ = screenInfo.width;
        screenHeight_ = screenInfo.height;
        screenOrigin_ = screenInfo.origin;

        // radius=0の場合はキャッシュ不要
        if (radius_ == 0) return;

        // キャッシュを初期化（幅は出力幅 = 入力幅）
        initializeCache(screenWidth_);

        currentY_ = 0;
        cacheReady_ = false;

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        size_t cacheBytes = kernelSize() * cacheWidth_ * 4 + cacheWidth_ * 4 * sizeof(uint32_t);
        PerfMetrics::instance().nodes[NodeType::VerticalBlur].recordAlloc(
            cacheBytes, cacheWidth_, kernelSize());
#endif
    }

    void finalize() override {
        rowCache_.clear();
        colSumR_.clear();
        colSumG_.clear();
        colSumB_.clear();
        colSumA_.clear();
        cacheReady_ = false;
    }

    // ========================================
    // push型インターフェース
    // ========================================

    bool pushPrepare(const PrepareRequest& request) override {
        bool shouldContinue;
        if (!checkPrepareState(pushPrepareState_, shouldContinue)) {
            return false;
        }
        if (!shouldContinue) {
            return true;
        }

        // radius=0の場合はスルー
        if (radius_ == 0) {
            Node* downstream = downstreamNode(0);
            if (downstream) {
                if (!downstream->pushPrepare(request)) {
                    pushPrepareState_ = PrepareState::CycleError;
                    return false;
                }
            }
            pushPrepareState_ = PrepareState::Prepared;
            return true;
        }

        // push用状態を初期化
        pushInputY_ = 0;
        pushOutputY_ = 0;
        pushInputWidth_ = request.width;
        pushInputHeight_ = request.height;
        // 出力高さ = 入力高さ + radius*2
        pushOutputHeight_ = pushInputHeight_ + radius_ * 2;
        pushInputOriginX_ = request.origin.x;
        pushInputOriginY_ = request.origin.y;
        lastInputOriginY_ = request.origin.y;

        // キャッシュを初期化（幅 = 入力幅、横方向拡張なし）
        initializeCache(pushInputWidth_);

        // 下流へ伝播
        Node* downstream = downstreamNode(0);
        if (downstream) {
            if (!downstream->pushPrepare(request)) {
                pushPrepareState_ = PrepareState::CycleError;
                return false;
            }
        }

        pushPrepareState_ = PrepareState::Prepared;
        return true;
    }

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
            pushInputY_++;
            if (pushInputY_ > radius_) {
                emitBlurredLine();
            }
            return;
        }

        // 入力をRGBA8_Straightに変換
        ImageBuffer converted = convertFormat(std::move(input.buffer),
                                               PixelFormatIDs::RGBA8_Straight);

        // キャッシュスロットを決定（リングバッファ）
        int slot = pushInputY_ % kernelSize();

        // 古い行を列合計から減算
        if (pushInputY_ >= kernelSize()) {
            updateColSum(slot, false);
        }

        // 入力行をそのままキャッシュに格納（横方向処理なし）
        storeInputRowToCache(converted, slot);

        // 新しい行を列合計に加算
        updateColSum(slot, true);

        // Y方向のorigin更新
        lastInputOriginY_ = input.origin.y;

        pushInputY_++;

        // 各入力行に対して1行出力
        emitBlurredLine();
    }

    void pushFinalize() override {
        // radius=0の場合はスルー
        if (radius_ == 0) {
            Node::pushFinalize();
            return;
        }

        // 残りの行を出力
        while (pushOutputY_ < pushOutputHeight_) {
            int slot = pushInputY_ % kernelSize();
            if (pushInputY_ >= kernelSize()) {
                updateColSum(slot, false);
            }
            // ゼロクリア済みのキャッシュ行を使用
            std::memset(rowCache_[slot].view().data, 0, cacheWidth_ * 4);

            lastInputOriginY_ -= to_fixed(1);
            pushInputY_++;
            emitBlurredLine();
        }

        Node::pushFinalize();
    }

protected:
    int nodeTypeForMetrics() const override { return NodeType::VerticalBlur; }

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
        auto& metrics = PerfMetrics::instance().nodes[NodeType::VerticalBlur];
        metrics.requestedPixels += static_cast<uint64_t>(request.width) * 1;
        metrics.usedPixels += static_cast<uint64_t>(request.width) * 1;
#endif

        int requestY = from_fixed(request.origin.y);

        // 最初の呼び出し: キャッシュ初期化のためcurrentY_を設定
        if (!cacheReady_) {
            currentY_ = requestY - kernelSize();
            cacheReady_ = true;
        }
        updateCache(upstream, request, requestY);

        // 出力バッファを確保
        ImageBuffer output(request.width, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        metrics.recordAlloc(output.totalBytes(), output.width(), output.height());
#endif

        // 縦方向の列合計から出力行を計算
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

    // スクリーン情報
    int screenWidth_ = 0;
    int screenHeight_ = 0;
    Point screenOrigin_;

    // スキャンライン処理用キャッシュ
    std::vector<ImageBuffer> rowCache_;
    std::vector<uint32_t> colSumR_;
    std::vector<uint32_t> colSumG_;
    std::vector<uint32_t> colSumB_;
    std::vector<uint32_t> colSumA_;
    int cacheWidth_ = 0;
    int currentY_ = 0;
    bool cacheReady_ = false;

    // push型処理用の状態
    int pushInputY_ = 0;
    int pushOutputY_ = 0;
    int pushInputWidth_ = 0;
    int pushInputHeight_ = 0;
    int pushOutputHeight_ = 0;
    int_fixed pushInputOriginX_ = 0;
    int_fixed pushInputOriginY_ = 0;
    int_fixed lastInputOriginY_ = 0;

    // ========================================
    // キャッシュ管理
    // ========================================

    void initializeCache(int width) {
        cacheWidth_ = width;
        rowCache_.resize(kernelSize());
        for (int i = 0; i < kernelSize(); i++) {
            rowCache_[i] = ImageBuffer(cacheWidth_, 1, PixelFormatIDs::RGBA8_Straight,
                                       InitPolicy::Zero);
        }
        colSumR_.assign(cacheWidth_, 0);
        colSumG_.assign(cacheWidth_, 0);
        colSumB_.assign(cacheWidth_, 0);
        colSumA_.assign(cacheWidth_, 0);
    }

    void updateCache(Node* upstream, const RenderRequest& request, int newY) {
        if (currentY_ == newY) return;

        int step = (currentY_ < newY) ? 1 : -1;

        while (currentY_ != newY) {
            int newSrcY = currentY_ + step * (radius_ + 1);
            int slot = newSrcY % kernelSize();
            if (slot < 0) slot += kernelSize();

            // 古い行を列合計から減算
            updateColSum(slot, false);

            // 新しい行を取得して格納
            fetchRowToCache(upstream, request, newSrcY, slot);

            // 新しい行を列合計に加算
            updateColSum(slot, true);

            currentY_ += step;
        }
    }

    // 上流から1行取得してキャッシュに格納（横方向処理なし）
    void fetchRowToCache(Node* upstream, const RenderRequest& request, int srcY, int cacheIndex) {
        // 上流への要求（幅はそのまま、横方向拡張なし）
        RenderRequest upstreamReq;
        upstreamReq.width = request.width;
        upstreamReq.height = 1;
        upstreamReq.origin.x = request.origin.x;
        upstreamReq.origin.y = to_fixed(srcY);

        RenderResult result = upstream->pullProcess(upstreamReq);

        // キャッシュ行をゼロクリア
        ViewPort dstView = rowCache_[cacheIndex].view();
        std::memset(dstView.data, 0, cacheWidth_ * 4);

        if (!result.isValid()) {
            return;
        }

        ImageBuffer converted = convertFormat(std::move(result.buffer),
                                               PixelFormatIDs::RGBA8_Straight);
        ViewPort srcView = converted.view();

        // 入力データをキャッシュにコピー（オフセット考慮）
        int srcOffsetX = from_fixed(upstreamReq.origin.x - result.origin.x);
        int dstStartX = std::max(0, srcOffsetX);
        int srcStartX = std::max(0, -srcOffsetX);
        int copyWidth = std::min(static_cast<int>(srcView.width) - srcStartX, cacheWidth_ - dstStartX);
        if (copyWidth > 0) {
            const uint8_t* srcPtr = static_cast<const uint8_t*>(srcView.data) + srcStartX * 4;
            std::memcpy(static_cast<uint8_t*>(dstView.data) + dstStartX * 4, srcPtr, copyWidth * 4);
        }
    }

    // 指定行を列合計に加算/減算
    void updateColSum(int cacheIndex, bool add) {
        const uint8_t* row = static_cast<const uint8_t*>(rowCache_[cacheIndex].view().data);
        int sign = add ? 1 : -1;
        for (int x = 0; x < cacheWidth_; x++) {
            int off = x * 4;
            int32_t a = row[off + 3] * sign;
            int32_t ra = row[off] * a;
            int32_t ga = row[off + 1] * a;
            int32_t ba = row[off + 2] * a;
            colSumR_[x] += ra;
            colSumG_[x] += ga;
            colSumB_[x] += ba;
            colSumA_[x] += a;
        }
    }

    // 縦方向の列合計から出力行を計算
    void computeOutputRow(ImageBuffer& output, const RenderRequest& request) {
        uint8_t* outRow = static_cast<uint8_t*>(output.view().data);
        int ks = kernelSize();
        for (int x = 0; x < request.width; x++) {
            int off = x * 4;
            if (colSumA_[x] > 0) {
                outRow[off]     = static_cast<uint8_t>(colSumR_[x] / colSumA_[x]);
                outRow[off + 1] = static_cast<uint8_t>(colSumG_[x] / colSumA_[x]);
                outRow[off + 2] = static_cast<uint8_t>(colSumB_[x] / colSumA_[x]);
                outRow[off + 3] = static_cast<uint8_t>(colSumA_[x] / ks);
            } else {
                outRow[off] = outRow[off + 1] = outRow[off + 2] = outRow[off + 3] = 0;
            }
        }
    }

    // ========================================
    // push型用ヘルパー関数
    // ========================================

    // 入力行をそのままキャッシュに格納
    void storeInputRowToCache(const ImageBuffer& input, int cacheIndex) {
        ViewPort srcView = input.view();
        ViewPort dstView = rowCache_[cacheIndex].view();
        int copyWidth = std::min(static_cast<int>(srcView.width), cacheWidth_);
        if (copyWidth > 0) {
            std::memcpy(dstView.data, srcView.data, copyWidth * 4);
        }
        // 残りの部分をゼロクリア
        if (copyWidth < cacheWidth_) {
            std::memset(static_cast<uint8_t*>(dstView.data) + copyWidth * 4, 0,
                       (cacheWidth_ - copyWidth) * 4);
        }
    }

    // 出力行を計算して下流にpush
    void emitBlurredLine() {
        ImageBuffer output(cacheWidth_, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized);

        uint8_t* outRow = static_cast<uint8_t*>(output.view().data);
        int ks = kernelSize();
        for (int x = 0; x < cacheWidth_; x++) {
            int off = x * 4;
            if (colSumA_[x] > 0) {
                outRow[off]     = static_cast<uint8_t>(colSumR_[x] / colSumA_[x]);
                outRow[off + 1] = static_cast<uint8_t>(colSumG_[x] / colSumA_[x]);
                outRow[off + 2] = static_cast<uint8_t>(colSumB_[x] / colSumA_[x]);
                outRow[off + 3] = static_cast<uint8_t>(colSumA_[x] / ks);
            } else {
                outRow[off] = outRow[off + 1] = outRow[off + 2] = outRow[off + 3] = 0;
            }
        }

        RenderRequest outReq;
        outReq.width = static_cast<int16_t>(cacheWidth_);
        outReq.height = 1;
        outReq.origin.x = pushInputOriginX_;
        int rowDiff = (pushInputY_ - 1) - (pushOutputY_ - radius_);
        outReq.origin.y = lastInputOriginY_ + to_fixed(rowDiff);

        pushOutputY_++;

        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushProcess(RenderResult(std::move(output), outReq.origin), outReq);
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_VERTICAL_BLUR_NODE_H
