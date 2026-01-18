#ifndef FLEXIMG_VERTICAL_BLUR_NODE_H
#define FLEXIMG_VERTICAL_BLUR_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>  // for uint64_t
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
// - passes: ブラー適用回数（1-5、デフォルト1）
//
// マルチパス処理:
// - passes=3で3回垂直ブラーを適用（ガウシアン近似）
// - キャッシュサイズ: radius * 2 * passes + 1
//
// スキャンライン処理:
// - prepare()でキャッシュを確保
// - pullProcess()で行キャッシュと列合計を使用したスライディングウィンドウ処理
// - finalize()でキャッシュを破棄
//
// 使用例:
//   VerticalBlurNode vblur;
//   vblur.setRadius(6);
//   vblur.setPasses(3);  // ガウシアン近似
//   src >> vblur >> sink;
//
// HorizontalBlurNodeと組み合わせて2次元ガウシアン近似:
//   src >> hblur(r=6, p=3) >> vblur(r=6, p=3) >> sink;
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
    void setPasses(int passes) { passes_ = std::clamp(passes, 1, 5); }

    int radius() const { return radius_; }
    int passes() const { return passes_; }
    int kernelSize() const { return radius_ * 2 + 1; }
    int totalKernelSize() const { return radius_ * 2 * passes_ + 1; }

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

        // radius=0またはpasses=0の場合はキャッシュ不要
        if (radius_ == 0 || passes_ == 0) return;

        // キャッシュを初期化（passes回分のキャッシュが必要）
        initializeCache(screenWidth_);

        currentY_ = 0;
        cacheReady_ = false;

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        size_t cacheBytes = totalKernelSize() * cacheWidth_ * 4 + cacheWidth_ * 4 * sizeof(uint32_t);
        PerfMetrics::instance().nodes[NodeType::VerticalBlur].recordAlloc(
            cacheBytes, cacheWidth_, totalKernelSize());
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

        int effectiveRadius = (passes_ == 1) ? radius_ : (radius_ * passes_);
        int cacheSize = (passes_ == 1) ? kernelSize() : totalKernelSize();

        // originを先に保存（bufferがムーブされる前に）
        Point inputOrigin = input.origin;

        if (!input.isValid()) {
            // 無効な入力でもキャッシュを更新（ゼロ行扱い）
            int slot = pushInputY_ % cacheSize;
            if (passes_ == 1 && pushInputY_ >= kernelSize()) {
                updateColSum(slot, false);
            }
            std::memset(rowCache_[slot].view().data, 0, cacheWidth_ * 4);
            rowOriginX_[slot] = inputOrigin.x;  // origin.xを保存
            if (passes_ == 1) {
                updateColSum(slot, true);
            }
            lastInputOriginY_ = inputOrigin.y;
            pushInputY_++;

            // effectiveRadius行受け取った後から出力開始
            if (pushInputY_ > effectiveRadius) {
                emitBlurredLine();
            }
            return;
        }

        // 入力をRGBA8_Straightに変換
        ImageBuffer converted = convertFormat(std::move(input.buffer),
                                               PixelFormatIDs::RGBA8_Straight);

        // キャッシュスロットを決定（リングバッファ）
        int slot = pushInputY_ % cacheSize;

        // 古い行を列合計から減算（単一パスのみ）
        if (passes_ == 1 && pushInputY_ >= kernelSize()) {
            updateColSum(slot, false);
        }

        // X方向のオフセットを計算（基準origin.xからの差分）
        // origin.xが大きいほど左にずれる → キャッシュ内で右にシフトして格納
        int xOffset = from_fixed(inputOrigin.x - baseOriginX_);

        // 入力行をキャッシュに格納（オフセット考慮）
        storeInputRowToCache(converted, slot, xOffset);
        rowOriginX_[slot] = inputOrigin.x;  // origin.xを保存

        // 新しい行を列合計に加算（単一パスのみ）
        if (passes_ == 1) {
            updateColSum(slot, true);
        }

        // Y方向のorigin更新
        lastInputOriginY_ = inputOrigin.y;

        pushInputY_++;

        // effectiveRadius行受け取った後から出力開始（出力行 m は入力行 m を中心としたブラー）
        if (pushInputY_ > effectiveRadius) {
            emitBlurredLine();
        }
    }

    void pushFinalize() override {
        // radius=0の場合はスルー
        if (radius_ == 0) {
            Node::pushFinalize();
            return;
        }

        int cacheSize = (passes_ == 1) ? kernelSize() : totalKernelSize();

        // 残りの行を出力（下端はゼロパディング扱い）
        while (pushOutputY_ < pushOutputHeight_) {
            // ゼロ行をキャッシュに追加
            int slot = pushInputY_ % cacheSize;
            if (passes_ == 1 && pushInputY_ >= kernelSize()) {
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

        // passes=1の場合は従来の単一パス処理
        if (passes_ == 1) {
            return pullProcessSinglePass(upstream, request);
        }

        // passes>1の場合は複数パス処理（マルチパスカーネルを使用）
        return pullProcessMultiPass(upstream, request);
    }

private:
    int radius_ = 5;
    int passes_ = 1;  // 1-5の範囲、デフォルト1（従来互換）

    // スクリーン情報
    int screenWidth_ = 0;
    int screenHeight_ = 0;
    Point screenOrigin_;

    // スキャンライン処理用キャッシュ
    std::vector<ImageBuffer> rowCache_;
    std::vector<int_fixed> rowOriginX_;      // 各キャッシュ行のorigin.x（push型用）
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
    int_fixed baseOriginX_ = 0;              // 基準origin.x（pushPrepareで設定）
    int_fixed pushInputOriginY_ = 0;
    int_fixed lastInputOriginY_ = 0;

    // ========================================
    // マルチパス処理用ヘルパー
    // ========================================

    // 単一パス処理（従来の実装）
    RenderResult pullProcessSinglePass(Node* upstream, const RenderRequest& request) {
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

    // マルチパス処理（重み付きカーネルを使用）
    RenderResult pullProcessMultiPass(Node* upstream, const RenderRequest& request) {
        int requestY = from_fixed(request.origin.y);
        int totalKernelRows = totalKernelSize();
        int effectiveRadius = radius_ * passes_;

        // 最初の呼び出し: キャッシュ初期化のためcurrentY_を設定
        if (!cacheReady_) {
            currentY_ = requestY - effectiveRadius;
            cacheReady_ = true;
        }

        // 必要な行範囲をキャッシュに読み込む
        updateCacheMultiPass(upstream, request, requestY, effectiveRadius);

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

        // マルチパスカーネルの重みを計算
        std::vector<int64_t> kernel = computeMultiPassKernel(radius_, passes_);
        int64_t kernelSum = 0;
        for (int64_t w : kernel) kernelSum += w;

        // 重み付きカーネルを使って出力行を計算
        computeOutputRowWeighted(output, request, kernel, kernelSum, effectiveRadius);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(output), request.origin);
    }

    // マルチパス用キャッシュ更新
    void updateCacheMultiPass(Node* upstream, const RenderRequest& request, int newY, int effectiveRadius) {
        if (currentY_ == newY) return;

        int step = (currentY_ < newY) ? 1 : -1;

        while (currentY_ != newY) {
            int newSrcY = currentY_ + step * (effectiveRadius + 1);
            int slot = newSrcY % totalKernelSize();
            if (slot < 0) slot += totalKernelSize();

            // 新しい行を取得して格納
            fetchRowToCache(upstream, request, newSrcY, slot);

            currentY_ += step;
        }
    }

    // マルチパスボックスブラーの畳み込みカーネルを計算
    // passes回のボックスブラー (サイズ2*radius+1) の畳み込み結果
    std::vector<int64_t> computeMultiPassKernel(int radius, int passes) {
        int singleSize = 2 * radius + 1;

        // 1パス目: ボックスフィルタ [1,1,1,...,1]
        std::vector<int64_t> kernel(singleSize, 1);

        // 2パス目以降: 畳み込みを繰り返す
        for (int p = 1; p < passes; p++) {
            std::vector<int64_t> newKernel(kernel.size() + singleSize - 1, 0);

            // 畳み込み: kernel * box
            for (size_t i = 0; i < kernel.size(); i++) {
                for (int j = 0; j < singleSize; j++) {
                    newKernel[i + j] += kernel[i];
                }
            }

            kernel = std::move(newKernel);
        }

        return kernel;
    }

    // 重み付きカーネルを使って出力行を計算
    void computeOutputRowWeighted(ImageBuffer& output, const RenderRequest& request,
                                   const std::vector<int64_t>& kernel, int64_t kernelSum,
                                   int effectiveRadius) {
        uint8_t* outRow = static_cast<uint8_t*>(output.view().data);
        int kernelSize = kernel.size();

        for (int x = 0; x < request.width; x++) {
            uint64_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;

            // カーネルの各位置に対応する行から加重和を計算
            for (int k = 0; k < kernelSize; k++) {
                int srcY = currentY_ - effectiveRadius + k;
                int slot = srcY % totalKernelSize();
                if (slot < 0) slot += totalKernelSize();

                const uint8_t* rowData = static_cast<const uint8_t*>(rowCache_[slot].view().data);
                int off = x * 4;
                int64_t weight = kernel[k];

                int a = rowData[off + 3];
                sumR += rowData[off] * a * weight;
                sumG += rowData[off + 1] * a * weight;
                sumB += rowData[off + 2] * a * weight;
                sumA += a * weight;
            }

            int off = x * 4;
            if (sumA > 0) {
                outRow[off]     = static_cast<uint8_t>(sumR / sumA);
                outRow[off + 1] = static_cast<uint8_t>(sumG / sumA);
                outRow[off + 2] = static_cast<uint8_t>(sumB / sumA);
                outRow[off + 3] = static_cast<uint8_t>(sumA / kernelSum);
            } else {
                outRow[off] = outRow[off + 1] = outRow[off + 2] = outRow[off + 3] = 0;
            }
        }
    }

    // ========================================
    // キャッシュ管理
    // ========================================

    void initializeCache(int width) {
        cacheWidth_ = width;
        int cacheRows = totalKernelSize();  // passes回分のキャッシュ
        rowCache_.resize(cacheRows);
        rowOriginX_.assign(cacheRows, 0);  // 各行のorigin.xを初期化
        for (int i = 0; i < cacheRows; i++) {
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
        writeOutputRowFromColSum(outRow, request.width);
    }

    // 列合計から出力行に書き込み（共通ヘルパー）
    void writeOutputRowFromColSum(uint8_t* outRow, int width) {
        int ks = kernelSize();
        for (int x = 0; x < width; x++) {
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

        if (passes_ == 1) {
            // 単一パス: 列合計から計算
            writeOutputRowFromColSum(outRow, cacheWidth_);
        } else {
            // マルチパス: 重み付きカーネルを使用
            std::vector<int64_t> kernel = computeMultiPassKernel(radius_, passes_);
            int64_t kernelSum = 0;
            for (int64_t w : kernel) kernelSum += w;
            writeOutputRowWeightedPush(outRow, cacheWidth_, kernel, kernelSum);
        }

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

    // push型用: 重み付きカーネルから出力行を計算
    void writeOutputRowWeightedPush(uint8_t* outRow, int width,
                                     const std::vector<int64_t>& kernel, int64_t kernelSum) {
        int kernelSize = kernel.size();
        int effectiveRadius = radius_ * passes_;

        for (int x = 0; x < width; x++) {
            uint64_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;

            // カーネルの各位置に対応する行から加重和を計算
            // 中心行は pushOutputY_, キャッシュされている最新の入力行は pushInputY_ - 1
            int centerInputRow = pushOutputY_;
            for (int k = 0; k < kernelSize; k++) {
                int srcRow = centerInputRow - effectiveRadius + k;
                int slot = srcRow % totalKernelSize();
                if (slot < 0) slot += totalKernelSize();

                const uint8_t* rowData = static_cast<const uint8_t*>(rowCache_[slot].view().data);
                int off = x * 4;
                int64_t weight = kernel[k];

                int a = rowData[off + 3];
                sumR += rowData[off] * a * weight;
                sumG += rowData[off + 1] * a * weight;
                sumB += rowData[off + 2] * a * weight;
                sumA += a * weight;
            }

            int off = x * 4;
            if (sumA > 0) {
                outRow[off]     = static_cast<uint8_t>(sumR / sumA);
                outRow[off + 1] = static_cast<uint8_t>(sumG / sumA);
                outRow[off + 2] = static_cast<uint8_t>(sumB / sumA);
                outRow[off + 3] = static_cast<uint8_t>(sumA / kernelSum);
            } else {
                outRow[off] = outRow[off + 1] = outRow[off + 2] = outRow[off + 3] = 0;
            }
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_VERTICAL_BLUR_NODE_H
