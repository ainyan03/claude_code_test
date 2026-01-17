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
// 入力画像に垂直方向のStack Blur（三角形重み分布フィルタ）を適用します。
// - radius: ブラー半径（カーネルサイズ = 2 * radius + 1）
//
// Stack Blurアルゴリズム:
// - 三角形の重み分布を使用（中心が最も重く、端に向かって線形減衰）
// - ガウシアンブラーに近い自然な仕上がり
// - O(n)の計算量（半径に依存しない高速処理）
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
// HorizontalBlurNodeと組み合わせて2Dブラーを実現:
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
        colSumInR_.clear();
        colSumInG_.clear();
        colSumInB_.clear();
        colSumInA_.clear();
        colSumOutR_.clear();
        colSumOutG_.clear();
        colSumOutB_.clear();
        colSumOutA_.clear();
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
        // 出力高さ = 入力高さ（push型ではサイズを変えない、エッジはゼロパディング）
        pushOutputHeight_ = pushInputHeight_;
        baseOriginX_ = request.origin.x;  // 基準origin.x（BoxBlurNodeと同様）
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

        // originを先に保存（bufferがムーブされる前に）
        Point inputOrigin = input.origin;

        if (!input.isValid()) {
            // 無効な入力でもキャッシュを更新（ゼロ行扱い）
            int slot = pushInputY_ % kernelSize();
            if (pushInputY_ >= kernelSize()) {
                updateColSum(slot, false);
            }
            std::memset(rowCache_[slot].view().data, 0, cacheWidth_ * 4);
            rowOriginX_[slot] = inputOrigin.x;  // origin.xを保存
            updateColSum(slot, true);
            lastInputOriginY_ = inputOrigin.y;
            pushInputY_++;

            // radius行受け取った後から出力開始
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

        // X方向のオフセットを計算（基準origin.xからの差分）
        // origin.xが大きいほど左にずれる → キャッシュ内で右にシフトして格納
        int xOffset = from_fixed(inputOrigin.x - baseOriginX_);

        // 入力行をキャッシュに格納（オフセット考慮）
        storeInputRowToCache(converted, slot, xOffset);
        rowOriginX_[slot] = inputOrigin.x;  // origin.xを保存

        // 新しい行を列合計に加算
        updateColSum(slot, true);

        // Y方向のorigin更新
        lastInputOriginY_ = inputOrigin.y;

        pushInputY_++;

        // radius行受け取った後から出力開始（出力行 m は入力行 m を中心としたブラー）
        if (pushInputY_ > radius_) {
            emitBlurredLine();
        }
    }

    void pushFinalize() override {
        // radius=0の場合はスルー
        if (radius_ == 0) {
            Node::pushFinalize();
            return;
        }

        // 残りのradius行を出力（下端はゼロパディング扱い）
        while (pushOutputY_ < pushOutputHeight_) {
            // ゼロ行をキャッシュに追加して列合計を更新
            int slot = pushInputY_ % kernelSize();
            if (pushInputY_ >= kernelSize()) {
                updateColSum(slot, false);
            }
            // ゼロクリア済みのキャッシュ行を使用
            std::memset(rowCache_[slot].view().data, 0, cacheWidth_ * 4);
            // updateColSum(slot, true) は不要（ゼロなので）

            // 仮想的な入力行のorigin.yを更新（1行下に進むのでorigin.yは減少）
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

        int requestY = from_fixed(request.origin.y);

        // 最初の呼び出し: キャッシュ初期化のためcurrentY_を設定
        if (!cacheReady_) {
            currentY_ = requestY - kernelSize();
            cacheReady_ = true;
        }
        // updateCache内で上流をpullするため、計測はこの後から開始
        updateCache(upstream, request, requestY);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
        auto& metrics = PerfMetrics::instance().nodes[NodeType::VerticalBlur];
        metrics.requestedPixels += static_cast<uint64_t>(request.width) * 1;
        metrics.usedPixels += static_cast<uint64_t>(request.width) * 1;
#endif

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
    std::vector<int_fixed> rowOriginX_;      // 各キャッシュ行のorigin.x（push型用）

    // Stack Blur用の列合計（各列ごとに3つのスタックを管理）
    std::vector<uint64_t> colSumR_;          // 現在の重み付き合計
    std::vector<uint64_t> colSumG_;
    std::vector<uint64_t> colSumB_;
    std::vector<uint64_t> colSumA_;
    std::vector<uint64_t> colSumInR_;        // 入ってくる行の合計
    std::vector<uint64_t> colSumInG_;
    std::vector<uint64_t> colSumInB_;
    std::vector<uint64_t> colSumInA_;
    std::vector<uint64_t> colSumOutR_;       // 出ていく行の合計
    std::vector<uint64_t> colSumOutG_;
    std::vector<uint64_t> colSumOutB_;
    std::vector<uint64_t> colSumOutA_;

    int cacheWidth_ = 0;
    int currentY_ = 0;
    bool cacheReady_ = false;

    // push型処理用の状態
    int pushInputY_ = 0;
    int pushOutputY_ = 0;
    int pushInputWidth_ = 0;
    int pushInputHeight_ = 0;
    int pushOutputHeight_ = 0;
    int_fixed baseOriginX_ = 0;              // 基準origin.x（pushPrepareで設定）
    int_fixed pushInputOriginY_ = 0;
    int_fixed lastInputOriginY_ = 0;

    // ========================================
    // キャッシュ管理
    // ========================================

    void initializeCache(int width) {
        cacheWidth_ = width;
        rowCache_.resize(kernelSize());
        rowOriginX_.assign(kernelSize(), 0);  // 各行のorigin.xを初期化
        for (int i = 0; i < kernelSize(); i++) {
            rowCache_[i] = ImageBuffer(cacheWidth_, 1, PixelFormatIDs::RGBA8_Straight,
                                       InitPolicy::Zero);
        }

        // Stack Blur用の列合計を初期化
        colSumR_.assign(cacheWidth_, 0);
        colSumG_.assign(cacheWidth_, 0);
        colSumB_.assign(cacheWidth_, 0);
        colSumA_.assign(cacheWidth_, 0);
        colSumInR_.assign(cacheWidth_, 0);
        colSumInG_.assign(cacheWidth_, 0);
        colSumInB_.assign(cacheWidth_, 0);
        colSumInA_.assign(cacheWidth_, 0);
        colSumOutR_.assign(cacheWidth_, 0);
        colSumOutG_.assign(cacheWidth_, 0);
        colSumOutB_.assign(cacheWidth_, 0);
        colSumOutA_.assign(cacheWidth_, 0);
    }

    void updateCache(Node* upstream, const RenderRequest& request, int newY) {
        if (currentY_ == newY) return;

        // 初回呼び出し時：Stack Blurの初期化
        if (currentY_ + 1 == newY && !cacheReady_) {
            // 全カーネル範囲の行をロードしてStack Blurを初期化
            for (int ky = -radius_; ky <= radius_; ky++) {
                int srcY = newY + ky;
                int slot = srcY % kernelSize();
                if (slot < 0) slot += kernelSize();

                // 行を取得してキャッシュに格納
                fetchRowToCache(upstream, request, srcY, slot);

                // Stack Blurの重み計算
                int weight = radius_ + 1 - std::abs(ky);

                // 各行をスタックに追加
                if (ky < 0) {
                    addRowToColSumOut(slot);
                }
                if (ky > 0) {
                    addRowToColSumIn(slot);
                }

                // 重み付きで列合計に加算
                addRowToColSum(slot, weight);
            }

            cacheReady_ = true;
            currentY_ = newY;
            return;
        }

        // スライディング処理
        int step = (currentY_ < newY) ? 1 : -1;

        while (currentY_ != newY) {
            // Stack Blur スライディング更新
            // sum = sum - sumOut + sumIn
            for (int x = 0; x < cacheWidth_; x++) {
                colSumR_[x] = colSumR_[x] - colSumOutR_[x] + colSumInR_[x];
                colSumG_[x] = colSumG_[x] - colSumOutG_[x] + colSumInG_[x];
                colSumB_[x] = colSumB_[x] - colSumOutB_[x] + colSumInB_[x];
                colSumA_[x] = colSumA_[x] - colSumOutA_[x] + colSumInA_[x];
            }

            // 古い行（最も遠い行）
            int oldSrcY = currentY_ + step * (-radius_ - 1);
            int oldSlot = oldSrcY % kernelSize();
            if (oldSlot < 0) oldSlot += kernelSize();

            // 前の中心行
            int prevCenterY = currentY_;
            int prevCenterSlot = prevCenterY % kernelSize();
            if (prevCenterSlot < 0) prevCenterSlot += kernelSize();

            // sumOutから古い行を削除
            removeRowFromColSum(oldSlot, colSumOutR_, colSumOutG_, colSumOutB_, colSumOutA_);

            // 前の中心行をsumOutに追加
            addRowToColSum(prevCenterSlot, colSumOutR_, colSumOutG_, colSumOutB_, colSumOutA_);

            // sumInから前の中心行を削除
            removeRowFromColSum(prevCenterSlot, colSumInR_, colSumInG_, colSumInB_, colSumInA_);

            // 新しい行を取得
            int newSrcY = currentY_ + step * (radius_ + 1);
            int newSlot = newSrcY % kernelSize();
            if (newSlot < 0) newSlot += kernelSize();

            fetchRowToCache(upstream, request, newSrcY, newSlot);

            // sumInに新しい行を追加
            addRowToColSum(newSlot, colSumInR_, colSumInG_, colSumInB_, colSumInA_);

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

    // ========================================
    // Stack Blur用ヘルパー関数
    // ========================================

    // 指定行をsumOutに加算
    void addRowToColSumOut(int cacheIndex) {
        const uint8_t* row = static_cast<const uint8_t*>(rowCache_[cacheIndex].view().data);
        for (int x = 0; x < cacheWidth_; x++) {
            int off = x * 4;
            uint32_t a = row[off + 3];
            colSumOutR_[x] += row[off] * a;
            colSumOutG_[x] += row[off + 1] * a;
            colSumOutB_[x] += row[off + 2] * a;
            colSumOutA_[x] += a;
        }
    }

    // 指定行をsumInに加算
    void addRowToColSumIn(int cacheIndex) {
        const uint8_t* row = static_cast<const uint8_t*>(rowCache_[cacheIndex].view().data);
        for (int x = 0; x < cacheWidth_; x++) {
            int off = x * 4;
            uint32_t a = row[off + 3];
            colSumInR_[x] += row[off] * a;
            colSumInG_[x] += row[off + 1] * a;
            colSumInB_[x] += row[off + 2] * a;
            colSumInA_[x] += a;
        }
    }

    // 指定行を重み付きでsumに加算
    void addRowToColSum(int cacheIndex, int weight) {
        const uint8_t* row = static_cast<const uint8_t*>(rowCache_[cacheIndex].view().data);
        for (int x = 0; x < cacheWidth_; x++) {
            int off = x * 4;
            uint32_t a = row[off + 3];
            colSumR_[x] += row[off] * a * weight;
            colSumG_[x] += row[off + 1] * a * weight;
            colSumB_[x] += row[off + 2] * a * weight;
            colSumA_[x] += a * weight;
        }
    }

    // 指定行を指定したスタックから削除
    void removeRowFromColSum(int cacheIndex,
                             std::vector<uint64_t>& sumR, std::vector<uint64_t>& sumG,
                             std::vector<uint64_t>& sumB, std::vector<uint64_t>& sumA) {
        const uint8_t* row = static_cast<const uint8_t*>(rowCache_[cacheIndex].view().data);
        for (int x = 0; x < cacheWidth_; x++) {
            int off = x * 4;
            uint32_t a = row[off + 3];
            sumR[x] -= row[off] * a;
            sumG[x] -= row[off + 1] * a;
            sumB[x] -= row[off + 2] * a;
            sumA[x] -= a;
        }
    }

    // 指定行を指定したスタックに加算
    void addRowToColSum(int cacheIndex,
                        std::vector<uint64_t>& sumR, std::vector<uint64_t>& sumG,
                        std::vector<uint64_t>& sumB, std::vector<uint64_t>& sumA) {
        const uint8_t* row = static_cast<const uint8_t*>(rowCache_[cacheIndex].view().data);
        for (int x = 0; x < cacheWidth_; x++) {
            int off = x * 4;
            uint32_t a = row[off + 3];
            sumR[x] += row[off] * a;
            sumG[x] += row[off + 1] * a;
            sumB[x] += row[off + 2] * a;
            sumA[x] += a;
        }
    }

    // 縦方向の列合計から出力行を計算
    void computeOutputRow(ImageBuffer& output, const RenderRequest& request) {
        uint8_t* outRow = static_cast<uint8_t*>(output.view().data);
        writeOutputRowFromColSum(outRow, request.width);
    }

    // 列合計から出力行に書き込み（Stack Blur用）
    void writeOutputRowFromColSum(uint8_t* outRow, int width) {
        // Stack Blur: 重みの合計 = (radius+1)^2
        int div = (radius_ + 1) * (radius_ + 1);
        for (int x = 0; x < width; x++) {
            int off = x * 4;
            if (colSumA_[x] > 0) {
                outRow[off]     = static_cast<uint8_t>(colSumR_[x] / colSumA_[x]);
                outRow[off + 1] = static_cast<uint8_t>(colSumG_[x] / colSumA_[x]);
                outRow[off + 2] = static_cast<uint8_t>(colSumB_[x] / colSumA_[x]);
                outRow[off + 3] = static_cast<uint8_t>(colSumA_[x] / div);
            } else {
                outRow[off] = outRow[off + 1] = outRow[off + 2] = outRow[off + 3] = 0;
            }
        }
    }

    // push型用の簡易版updateColSum（Stack Blur非対応、互換性維持のため）
    // TODO: push型もStack Blurに完全対応させる
    void updateColSum(int cacheIndex, bool add) {
        const uint8_t* row = static_cast<const uint8_t*>(rowCache_[cacheIndex].view().data);
        int sign = add ? 1 : -1;
        int weight = radius_ + 1;  // 暫定的に最大重みを使用
        for (int x = 0; x < cacheWidth_; x++) {
            int off = x * 4;
            int64_t a = row[off + 3] * sign * weight;
            int64_t ra = row[off] * a;
            int64_t ga = row[off + 1] * a;
            int64_t ba = row[off + 2] * a;
            colSumR_[x] += ra;
            colSumG_[x] += ga;
            colSumB_[x] += ba;
            colSumA_[x] += a;
        }
    }

    // ========================================
    // push型用ヘルパー関数
    // ========================================

    // 入力行をキャッシュに格納（xOffsetで位置調整）
    // xOffset = inputOrigin.x - baseOriginX_
    // 入力pixel[N]のワールド座標 = N - inputOrigin.x
    // 基準座標系でのバッファ位置 = N - inputOrigin.x + baseOriginX_ = N - xOffset
    // つまり dstPos = srcPos - xOffset
    void storeInputRowToCache(const ImageBuffer& input, int cacheIndex, int xOffset = 0) {
        ViewPort srcView = input.view();
        ViewPort dstView = rowCache_[cacheIndex].view();
        const uint8_t* srcData = static_cast<const uint8_t*>(srcView.data);
        uint8_t* dstData = static_cast<uint8_t*>(dstView.data);
        int srcWidth = static_cast<int>(srcView.width);

        // キャッシュをゼロクリア
        std::memset(dstData, 0, cacheWidth_ * 4);

        // コピー範囲の計算
        // dstPos = srcPos - xOffset
        // srcPos = dstPos + xOffset
        int dstStart = std::max(0, -xOffset);
        int srcStart = std::max(0, xOffset);
        int dstEnd = std::min(cacheWidth_, srcWidth - xOffset);
        int copyWidth = dstEnd - dstStart;

        if (copyWidth > 0 && srcStart < srcWidth) {
            std::memcpy(dstData + dstStart * 4, srcData + srcStart * 4, copyWidth * 4);
        }
    }

    // 出力行を計算して下流にpush
    void emitBlurredLine() {
        ImageBuffer output(cacheWidth_, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized);

        uint8_t* outRow = static_cast<uint8_t*>(output.view().data);
        writeOutputRowFromColSum(outRow, cacheWidth_);

        // origin.xの計算: キャッシュはbaseOriginX_に揃えてアライメントされているので
        // 出力もbaseOriginX_を使用する
        int_fixed originX = baseOriginX_;

        // origin.yの計算（BoxBlurNodeと同様の方式）
        // lastInputOriginY_は入力行(pushInputY_-1)のorigin.y
        // 出力行pushOutputY_に対応する入力の中心行: pushOutputY_
        // 最後の入力行: pushInputY_ - 1
        // rowDiff = lastInputRow - centerInputRow = (pushInputY_ - 1) - pushOutputY_
        int rowDiff = (pushInputY_ - 1) - pushOutputY_;
        int_fixed originY = lastInputOriginY_ + to_fixed(rowDiff);

        RenderRequest outReq;
        outReq.width = static_cast<int16_t>(cacheWidth_);
        outReq.height = 1;
        outReq.origin.x = originX;
        outReq.origin.y = originY;

        pushOutputY_++;

        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushProcess(RenderResult(std::move(output), outReq.origin), outReq);
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_VERTICAL_BLUR_NODE_H
