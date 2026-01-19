#ifndef FLEXIMG_VERTICAL_BLUR_NODE_H
#define FLEXIMG_VERTICAL_BLUR_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>  // for int32_t, uint32_t
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
// - passes: ブラー適用回数（1-3、デフォルト1）
//
// パラメータ上限（32bit演算でのオーバーフロー防止）:
// - passes=1: radius上限なし（実用上問題なし）
// - passes=2: radius上限128
// - passes=3: radius上限19
//
// マルチパス処理（パイプライン方式）:
// - passes=3で3回垂直ブラーを適用（ガウシアン近似）
// - 各パスが独立したステージとして処理され、境界処理も独立に行われる
// - 「3パス×1ノード」と「1パス×3ノード直列」が同等の結果を得る
//
// メモリ消費量（概算）:
// - 各ステージ: (radius * 2 + 1) * width * 4 bytes + width * 16 bytes（列合計）
// - passes=3の場合: 3ステージ分のキャッシュが必要
// - 例: radius=19, passes=3, width=640 → 約192KB
// - 例: radius=19, passes=3, width=2048 → 約614KB
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

    // passes別のradius上限（32bit演算オーバーフロー防止）
    // kernelSum = (2*radius+1)^passes
    // maxSumRGB = 65025 * kernelSum < 2^32
    // passes=1: 199+, passes=2: 128, passes=3: 19
    static int maxRadiusForPasses(int passes) {
        return (passes <= 1) ? 199 : (passes == 2) ? 128 : 19;
    }

    void setRadius(int radius) {
        int maxR = maxRadiusForPasses(passes_);
        radius_ = (radius < 0) ? 0 : (radius > maxR) ? maxR : radius;
    }

    void setPasses(int passes) {
        passes_ = (passes < 1) ? 1 : (passes > 3) ? 3 : passes;
        // passesが変わるとradius上限も変わるので再適用
        int maxR = maxRadiusForPasses(passes_);
        if (radius_ > maxR) radius_ = maxR;
    }

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

        // passes>1の場合はパイプライン方式を使用
        if (passes_ > 1) {
            initializeStages(screenWidth_);
        } else {
            // passes=1の場合は従来方式
            initializeCache(screenWidth_);
            currentY_ = 0;
            cacheReady_ = false;
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        size_t cacheBytes;
        if (passes_ > 1) {
            // パイプライン方式: 各ステージ (radius*2+1)*width*4 + width*16
            cacheBytes = passes_ * (kernelSize() * cacheWidth_ * 4 + cacheWidth_ * 4 * sizeof(uint32_t));
        } else {
            cacheBytes = totalKernelSize() * cacheWidth_ * 4 + cacheWidth_ * 4 * sizeof(uint32_t);
        }
        PerfMetrics::instance().nodes[NodeType::VerticalBlur].recordAlloc(
            cacheBytes, cacheWidth_, passes_ > 1 ? kernelSize() * passes_ : totalKernelSize());
#endif
    }

    void finalize() override {
        // パイプラインステージをクリア
        for (auto& stage : stages_) {
            stage.clear();
        }
        stages_.clear();

        // 従来方式のキャッシュもクリア
        rowCache_.clear();
        rowOriginX_.clear();
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

        // キャッシュを初期化
        if (passes_ > 1) {
            // パイプライン方式: 各ステージのキャッシュを初期化
            initializeStages(pushInputWidth_);
            // 各ステージのpush状態をリセット
            for (auto& stage : stages_) {
                stage.pushInputY = 0;
                stage.pushOutputY = 0;
            }
        } else {
            // 従来方式
            initializeCache(pushInputWidth_);
        }

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

        // passes>1の場合はパイプライン方式
        if (passes_ > 1) {
            pushProcessPipeline(std::move(input), request);
            return;
        }

        // passes=1: 従来方式
        int cacheSize = kernelSize();

        // originを先に保存（bufferがムーブされる前に）
        Point inputOrigin = input.origin;

        if (!input.isValid()) {
            // 無効な入力でもキャッシュを更新（ゼロ行扱い）
            int slot = pushInputY_ % cacheSize;
            if (pushInputY_ >= kernelSize()) {
                updateColSum(slot, false);
            }
            std::memset(rowCache_[slot].view().data, 0, cacheWidth_ * 4);
            rowOriginX_[slot] = inputOrigin.x;
            updateColSum(slot, true);
            lastInputOriginY_ = inputOrigin.y;
            pushInputY_++;

            // radius行受け取った後から出力開始
            if (pushInputY_ > radius_) {
                emitBlurredLineSinglePass();
            }
            return;
        }

        // 入力をRGBA8_Straightに変換
        ImageBuffer converted = convertFormat(std::move(input.buffer),
                                               PixelFormatIDs::RGBA8_Straight);

        // キャッシュスロットを決定（リングバッファ）
        int slot = pushInputY_ % cacheSize;

        // 古い行を列合計から減算
        if (pushInputY_ >= kernelSize()) {
            updateColSum(slot, false);
        }

        // X方向のオフセットを計算
        int xOffset = from_fixed(inputOrigin.x - baseOriginX_);

        // 入力行をキャッシュに格納（オフセット考慮）
        storeInputRowToCache(converted, slot, xOffset);
        rowOriginX_[slot] = inputOrigin.x;

        // 新しい行を列合計に加算
        updateColSum(slot, true);

        // Y方向のorigin更新
        lastInputOriginY_ = inputOrigin.y;

        pushInputY_++;

        // radius行受け取った後から出力開始
        if (pushInputY_ > radius_) {
            emitBlurredLineSinglePass();
        }
    }

    // パイプライン方式のpush処理（passes>1用）
    void pushProcessPipeline(RenderResult&& input, const RenderRequest& /* request */) {
        Point inputOrigin = input.origin;
        int ks = kernelSize();

        // Stage 0に入力行を格納
        BlurStage& stage0 = stages_[0];
        int slot0 = stage0.pushInputY % ks;

        // 古い行を列合計から減算
        if (stage0.pushInputY >= ks) {
            updateStageColSum(stage0, slot0, false);
        }

        if (!input.isValid()) {
            std::memset(stage0.rowCache[slot0].view().data, 0, cacheWidth_ * 4);
        } else {
            ImageBuffer converted = convertFormat(std::move(input.buffer),
                                                   PixelFormatIDs::RGBA8_Straight);
            int xOffset = from_fixed(inputOrigin.x - baseOriginX_);
            storeInputRowToStageCache(stage0, converted, slot0, xOffset);
        }
        stage0.rowOriginX[slot0] = inputOrigin.x;

        // 新しい行を列合計に加算
        updateStageColSum(stage0, slot0, true);

        lastInputOriginY_ = inputOrigin.y;
        stage0.pushInputY++;

        // Stage 0がradius行蓄積後、後続ステージにデータを伝播
        if (stage0.pushInputY > radius_) {
            propagatePipelineStages();
        }
    }

    void pushFinalize() override {
        // radius=0の場合はスルー
        if (radius_ == 0) {
            Node::pushFinalize();
            return;
        }

        if (passes_ > 1) {
            pushFinalizePipeline();
        } else {
            pushFinalizeSinglePass();
        }

        Node::pushFinalize();
    }

    // 単一パスのpushFinalize
    void pushFinalizeSinglePass() {
        int cacheSize = kernelSize();

        // 残りの行を出力（下端はゼロパディング扱い）
        while (pushOutputY_ < pushOutputHeight_) {
            int slot = pushInputY_ % cacheSize;
            if (pushInputY_ >= kernelSize()) {
                updateColSum(slot, false);
            }
            std::memset(rowCache_[slot].view().data, 0, cacheWidth_ * 4);

            lastInputOriginY_ -= to_fixed(1);
            pushInputY_++;
            emitBlurredLineSinglePass();
        }
    }

    // パイプライン方式のpushFinalize
    void pushFinalizePipeline() {
        int ks = kernelSize();

        // 残りの行を出力（下端はゼロパディング扱い）
        while (pushOutputY_ < pushOutputHeight_) {
            // Stage 0にゼロ行を追加
            BlurStage& stage0 = stages_[0];
            int slot0 = stage0.pushInputY % ks;

            if (stage0.pushInputY >= ks) {
                updateStageColSum(stage0, slot0, false);
            }
            std::memset(stage0.rowCache[slot0].view().data, 0, cacheWidth_ * 4);

            lastInputOriginY_ -= to_fixed(1);
            stage0.pushInputY++;

            // 後続ステージに伝播
            propagatePipelineStages();
        }
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
    int passes_ = 1;  // 1-3の範囲、デフォルト1（従来互換）

    // スクリーン情報
    int screenWidth_ = 0;
    int screenHeight_ = 0;
    Point screenOrigin_;

    // ========================================
    // パイプラインステージ構造体
    // ========================================
    // 各ステージが独立したキャッシュと列合計を持つ
    // passes=3の場合、3つのステージがパイプライン接続される
    struct BlurStage {
        std::vector<ImageBuffer> rowCache;   // radius*2+1 行のキャッシュ
        std::vector<int_fixed> rowOriginX;   // 各キャッシュ行のorigin.x（push型用）
        std::vector<uint32_t> colSumR;       // 列合計（R×A）
        std::vector<uint32_t> colSumG;       // 列合計（G×A）
        std::vector<uint32_t> colSumB;       // 列合計（B×A）
        std::vector<uint32_t> colSumA;       // 列合計（A）
        int currentY = 0;                    // 現在のY座標（pull型用）
        bool cacheReady = false;             // キャッシュ初期化済みフラグ

        // push型用の状態
        int pushInputY = 0;                  // 入力行カウント
        int pushOutputY = 0;                 // 出力行カウント

        void clear() {
            rowCache.clear();
            rowOriginX.clear();
            colSumR.clear();
            colSumG.clear();
            colSumB.clear();
            colSumA.clear();
            currentY = 0;
            cacheReady = false;
            pushInputY = 0;
            pushOutputY = 0;
        }
    };

    // パイプラインステージ（passes個）
    std::vector<BlurStage> stages_;
    int cacheWidth_ = 0;

    // 後方互換用（単一パス用変数、段階的に移行）
    std::vector<ImageBuffer> rowCache_;
    std::vector<int_fixed> rowOriginX_;      // 各キャッシュ行のorigin.x（push型用）
    std::vector<uint32_t> colSumR_;
    std::vector<uint32_t> colSumG_;
    std::vector<uint32_t> colSumB_;
    std::vector<uint32_t> colSumA_;
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

    // マルチパス処理（パイプライン方式）
    // 各ステージが独立してボックスブラーを適用し、境界処理も独立に行う
    // これにより「3パス×1ノード」と「1パス×3ノード直列」が同等の結果になる
    RenderResult pullProcessMultiPass(Node* upstream, const RenderRequest& request) {
        int requestY = from_fixed(request.origin.y);
        // 注: 各ステージの初期化はupdateStageCache内で行われる

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
        auto& metrics = PerfMetrics::instance().nodes[NodeType::VerticalBlur];
        metrics.requestedPixels += static_cast<uint64_t>(request.width) * 1;
        metrics.usedPixels += static_cast<uint64_t>(request.width) * 1;
#endif

        // 最終ステージのキャッシュを更新（再帰的に前段ステージも更新される）
        updateStageCache(passes_ - 1, upstream, request, requestY);

        // 出力バッファを確保
        ImageBuffer output(request.width, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        metrics.recordAlloc(output.totalBytes(), output.width(), output.height());
#endif

        // 最終ステージの列合計から出力行を計算
        computeStageOutputRow(stages_[passes_ - 1], output, request.width);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(output), request.origin);
    }

    // ステージsのキャッシュを更新（requestYに対応する出力が得られるように）
    void updateStageCache(int stageIndex, Node* upstream, const RenderRequest& request, int newY) {
        BlurStage& stage = stages_[stageIndex];
        int ks = kernelSize();

        // このステージへの最初の呼び出し時、currentYを調整してキャッシュを完全に充填
        // newY - kernelSize() から開始することで、kernelSize()回のループでキャッシュが充填される
        if (!stage.cacheReady) {
            stage.currentY = newY - ks;
            stage.cacheReady = true;
        }

        if (stage.currentY == newY) return;

        int step = (stage.currentY < newY) ? 1 : -1;

        while (stage.currentY != newY) {
            int newSrcY = stage.currentY + step * (radius_ + 1);
            int slot = newSrcY % ks;
            if (slot < 0) slot += ks;

            // 古い行を列合計から減算
            updateStageColSum(stage, slot, false);

            // 新しい行を取得してキャッシュに格納
            if (stageIndex == 0) {
                // Stage 0: 上流から直接取得
                fetchRowToStageCache(stage, upstream, request, newSrcY, slot);
            } else {
                // Stage 1以降: 前段ステージから取得
                fetchRowFromPrevStage(stageIndex, upstream, request, newSrcY, slot);
            }

            // 新しい行を列合計に加算
            updateStageColSum(stage, slot, true);

            stage.currentY += step;
        }
    }

    // 上流から1行取得してステージのキャッシュに格納
    void fetchRowToStageCache(BlurStage& stage, Node* upstream, const RenderRequest& request,
                               int srcY, int cacheIndex) {
        RenderRequest upstreamReq;
        upstreamReq.width = request.width;
        upstreamReq.height = 1;
        upstreamReq.origin.x = request.origin.x;
        upstreamReq.origin.y = to_fixed(srcY);

        RenderResult result = upstream->pullProcess(upstreamReq);

        // キャッシュ行をゼロクリア
        ViewPort dstView = stage.rowCache[cacheIndex].view();
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

    // 前段ステージから1行取得して現ステージのキャッシュに格納
    void fetchRowFromPrevStage(int stageIndex, Node* upstream, const RenderRequest& request,
                                int srcY, int cacheIndex) {
        BlurStage& stage = stages_[stageIndex];
        BlurStage& prevStage = stages_[stageIndex - 1];

        // 前段ステージのキャッシュを更新
        updateStageCache(stageIndex - 1, upstream, request, srcY);

        // 前段ステージの列合計から1行を計算してキャッシュに格納
        ViewPort dstView = stage.rowCache[cacheIndex].view();
        uint8_t* dstRow = static_cast<uint8_t*>(dstView.data);

        int ks = kernelSize();
        for (int x = 0; x < cacheWidth_; x++) {
            int off = x * 4;
            if (prevStage.colSumA[x] > 0) {
                dstRow[off]     = static_cast<uint8_t>(prevStage.colSumR[x] / prevStage.colSumA[x]);
                dstRow[off + 1] = static_cast<uint8_t>(prevStage.colSumG[x] / prevStage.colSumA[x]);
                dstRow[off + 2] = static_cast<uint8_t>(prevStage.colSumB[x] / prevStage.colSumA[x]);
                dstRow[off + 3] = static_cast<uint8_t>(prevStage.colSumA[x] / ks);
            } else {
                dstRow[off] = dstRow[off + 1] = dstRow[off + 2] = dstRow[off + 3] = 0;
            }
        }
    }

    // ステージの列合計を更新（加算/減算）
    void updateStageColSum(BlurStage& stage, int cacheIndex, bool add) {
        const uint8_t* row = static_cast<const uint8_t*>(stage.rowCache[cacheIndex].view().data);
        int sign = add ? 1 : -1;
        for (int x = 0; x < cacheWidth_; x++) {
            int off = x * 4;
            int32_t a = row[off + 3] * sign;
            int32_t ra = row[off] * a;
            int32_t ga = row[off + 1] * a;
            int32_t ba = row[off + 2] * a;
            stage.colSumR[x] += ra;
            stage.colSumG[x] += ga;
            stage.colSumB[x] += ba;
            stage.colSumA[x] += a;
        }
    }

    // ステージの列合計から出力行を計算
    void computeStageOutputRow(BlurStage& stage, ImageBuffer& output, int width) {
        uint8_t* outRow = static_cast<uint8_t*>(output.view().data);
        int ks = kernelSize();
        for (int x = 0; x < width; x++) {
            int off = x * 4;
            if (stage.colSumA[x] > 0) {
                outRow[off]     = static_cast<uint8_t>(stage.colSumR[x] / stage.colSumA[x]);
                outRow[off + 1] = static_cast<uint8_t>(stage.colSumG[x] / stage.colSumA[x]);
                outRow[off + 2] = static_cast<uint8_t>(stage.colSumB[x] / stage.colSumA[x]);
                outRow[off + 3] = static_cast<uint8_t>(stage.colSumA[x] / ks);
            } else {
                outRow[off] = outRow[off + 1] = outRow[off + 2] = outRow[off + 3] = 0;
            }
        }
    }

    // ========================================
    // キャッシュ管理
    // ========================================

    // 単一ステージのキャッシュを初期化
    void initializeStage(BlurStage& stage, int width) {
        int cacheRows = kernelSize();  // radius*2+1
        stage.rowCache.resize(cacheRows);
        stage.rowOriginX.assign(cacheRows, 0);
        for (int i = 0; i < cacheRows; i++) {
            stage.rowCache[i] = ImageBuffer(width, 1, PixelFormatIDs::RGBA8_Straight,
                                            InitPolicy::Zero);
        }
        stage.colSumR.assign(width, 0);
        stage.colSumG.assign(width, 0);
        stage.colSumB.assign(width, 0);
        stage.colSumA.assign(width, 0);
        stage.currentY = 0;
        stage.cacheReady = false;
    }

    // 全ステージを初期化（パイプライン方式用）
    void initializeStages(int width) {
        cacheWidth_ = width;
        stages_.resize(passes_);
        for (int i = 0; i < passes_; i++) {
            initializeStage(stages_[i], width);
        }
    }

    // 後方互換: 従来のキャッシュ初期化（passes=1または従来方式用）
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

    // 出力行を計算して下流にpush（単一パス用）
    void emitBlurredLineSinglePass() {
        ImageBuffer output(cacheWidth_, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized);

        uint8_t* outRow = static_cast<uint8_t*>(output.view().data);
        writeOutputRowFromColSum(outRow, cacheWidth_);

        // origin計算
        int_fixed originX = baseOriginX_;
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

    // パイプラインステージの伝播処理（passes>1用）
    void propagatePipelineStages() {
        int ks = kernelSize();

        // Stage 0の出力を計算してStage 1以降に伝播
        for (int s = 1; s < passes_; s++) {
            BlurStage& prevStage = stages_[s - 1];
            BlurStage& stage = stages_[s];

            // 前段ステージの列合計から1行を計算
            ImageBuffer stageInput(cacheWidth_, 1, PixelFormatIDs::RGBA8_Straight,
                                   InitPolicy::Uninitialized);
            uint8_t* stageRow = static_cast<uint8_t*>(stageInput.view().data);

            for (int x = 0; x < cacheWidth_; x++) {
                int off = x * 4;
                if (prevStage.colSumA[x] > 0) {
                    stageRow[off]     = static_cast<uint8_t>(prevStage.colSumR[x] / prevStage.colSumA[x]);
                    stageRow[off + 1] = static_cast<uint8_t>(prevStage.colSumG[x] / prevStage.colSumA[x]);
                    stageRow[off + 2] = static_cast<uint8_t>(prevStage.colSumB[x] / prevStage.colSumA[x]);
                    stageRow[off + 3] = static_cast<uint8_t>(prevStage.colSumA[x] / ks);
                } else {
                    stageRow[off] = stageRow[off + 1] = stageRow[off + 2] = stageRow[off + 3] = 0;
                }
            }

            // 前段の出力行カウントを更新
            prevStage.pushOutputY++;

            // 現段ステージのキャッシュに格納
            int slot = stage.pushInputY % ks;

            // 古い行を列合計から減算
            if (stage.pushInputY >= ks) {
                updateStageColSum(stage, slot, false);
            }

            // 新しい行をキャッシュに格納
            ViewPort srcView = stageInput.view();
            ViewPort dstView = stage.rowCache[slot].view();
            std::memcpy(dstView.data, srcView.data, cacheWidth_ * 4);

            // 新しい行を列合計に加算
            updateStageColSum(stage, slot, true);

            stage.pushInputY++;

            // このステージがまだradius行蓄積していない場合は伝播終了
            if (stage.pushInputY <= radius_) {
                return;
            }
        }

        // 最終ステージが出力可能になったら下流にpush
        emitBlurredLinePipeline();
    }

    // パイプライン方式の出力（最終ステージから下流へpush）
    void emitBlurredLinePipeline() {
        BlurStage& lastStage = stages_[passes_ - 1];
        int ks = kernelSize();

        ImageBuffer output(cacheWidth_, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized);
        uint8_t* outRow = static_cast<uint8_t*>(output.view().data);

        for (int x = 0; x < cacheWidth_; x++) {
            int off = x * 4;
            if (lastStage.colSumA[x] > 0) {
                outRow[off]     = static_cast<uint8_t>(lastStage.colSumR[x] / lastStage.colSumA[x]);
                outRow[off + 1] = static_cast<uint8_t>(lastStage.colSumG[x] / lastStage.colSumA[x]);
                outRow[off + 2] = static_cast<uint8_t>(lastStage.colSumB[x] / lastStage.colSumA[x]);
                outRow[off + 3] = static_cast<uint8_t>(lastStage.colSumA[x] / ks);
            } else {
                outRow[off] = outRow[off + 1] = outRow[off + 2] = outRow[off + 3] = 0;
            }
        }

        lastStage.pushOutputY++;

        // origin計算
        int_fixed originX = baseOriginX_;
        int rowDiff = (stages_[0].pushInputY - 1) - pushOutputY_;
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

    // ステージのキャッシュに行を格納（push用）
    void storeInputRowToStageCache(BlurStage& stage, const ImageBuffer& input, int cacheIndex, int xOffset = 0) {
        ViewPort srcView = input.view();
        ViewPort dstView = stage.rowCache[cacheIndex].view();
        const uint8_t* srcData = static_cast<const uint8_t*>(srcView.data);
        uint8_t* dstData = static_cast<uint8_t*>(dstView.data);
        int srcWidth = static_cast<int>(srcView.width);

        // キャッシュをゼロクリア
        std::memset(dstData, 0, cacheWidth_ * 4);

        // コピー範囲の計算
        int dstStart = std::max(0, -xOffset);
        int srcStart = std::max(0, xOffset);
        int dstEnd = std::min(cacheWidth_, srcWidth - xOffset);
        int copyWidth = dstEnd - dstStart;

        if (copyWidth > 0 && srcStart < srcWidth) {
            std::memcpy(dstData + dstStart * 4, srcData + srcStart * 4, copyWidth * 4);
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_VERTICAL_BLUR_NODE_H
