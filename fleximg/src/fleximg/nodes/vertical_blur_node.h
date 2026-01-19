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
// - radius: ブラー半径（0-127、カーネルサイズ = 2 * radius + 1）
// - passes: ブラー適用回数（1-3、デフォルト1）
//
// マルチパス処理（パイプライン方式）:
// - passes=3で3回垂直ブラーを適用（ガウシアン近似）
// - 各パスが独立したステージとして処理され、境界処理も独立に行われる
// - 「3パス×1ノード」と「1パス×3ノード直列」が同等の結果を得る
//
// メモリ消費量（概算）:
// - 各ステージ: (radius * 2 + 1) * width * 4 bytes + width * 16 bytes（列合計）
// - 例: radius=50, passes=3, width=640 → 約500KB
// - 例: radius=127, passes=3, width=2048 → 約4MB
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

        // パイプライン方式でキャッシュを初期化（passes=1でもstages_[0]を使用）
        initializeStages(screenWidth_);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        // パイプライン方式: 各ステージ (radius*2+1)*width*4 + width*16
        size_t cacheBytes = static_cast<size_t>(passes_) * (static_cast<size_t>(kernelSize()) * static_cast<size_t>(cacheWidth_) * 4 + static_cast<size_t>(cacheWidth_) * 4 * sizeof(uint32_t));
        PerfMetrics::instance().nodes[NodeType::VerticalBlur].recordAlloc(
            cacheBytes, cacheWidth_, kernelSize() * passes_);
#endif
    }

    void finalize() override {
        // パイプラインステージをクリア
        for (auto& stage : stages_) {
            stage.clear();
        }
        stages_.clear();
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

        // パイプライン方式でキャッシュを初期化（passes=1でもstages_[0]を使用）
        initializeStages(pushInputWidth_);
        // 各ステージのpush状態をリセット
        for (auto& stage : stages_) {
            stage.pushInputY = 0;
            stage.pushOutputY = 0;
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

    void pushProcess(RenderResult&& input, const RenderRequest& /* request */) override {
        // radius=0の場合はスルー
        if (radius_ == 0) {
            Node* downstream = downstreamNode(0);
            if (downstream) {
                downstream->pushProcess(std::move(input), RenderRequest());
            }
            return;
        }

        // パイプライン方式で処理（passes=1でもstages_[0]を使用）
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
            std::memset(stage0.rowCache[static_cast<size_t>(slot0)].view().data, 0, static_cast<size_t>(cacheWidth_) * 4);
        } else {
            ImageBuffer converted = convertFormat(std::move(input.buffer),
                                                   PixelFormatIDs::RGBA8_Straight);
            int xOffset = from_fixed(inputOrigin.x - baseOriginX_);
            storeInputRowToStageCache(stage0, converted, slot0, xOffset);
        }
        stage0.rowOriginX[static_cast<size_t>(slot0)] = inputOrigin.x;

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

        // パイプライン方式で残りの行を出力（passes=1でもstages_[0]を使用）
        int ks = kernelSize();

        // 残りの行を出力（下端はゼロパディング扱い）
        while (pushOutputY_ < pushOutputHeight_) {
            // Stage 0にゼロ行を追加
            BlurStage& stage0 = stages_[0];
            int slot0 = stage0.pushInputY % ks;

            if (stage0.pushInputY >= ks) {
                updateStageColSum(stage0, slot0, false);
            }
            std::memset(stage0.rowCache[static_cast<size_t>(slot0)].view().data, 0, static_cast<size_t>(cacheWidth_) * 4);

            lastInputOriginY_ -= to_fixed(1);
            stage0.pushInputY++;

            // 後続ステージに伝播
            propagatePipelineStages();
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

        // パイプライン方式で処理（passes=1でもstages_[0]を使用）
        return pullProcessPipeline(upstream, request);
    }

private:
    int radius_ = 5;
    int passes_ = 1;  // 1-3の範囲、デフォルト1

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

    // パイプラインステージ（passes個、passes=1でもstages_[0]を使用）
    std::vector<BlurStage> stages_;
    int cacheWidth_ = 0;

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
    // パイプライン処理
    // ========================================
    // 各ステージが独立してボックスブラーを適用し、境界処理も独立に行う
    // これにより「3パス×1ノード」と「1パス×3ノード直列」が同等の結果になる
    // passes=1の場合もstages_[0]を使用して処理を統一
    RenderResult pullProcessPipeline(Node* upstream, const RenderRequest& request) {
        int requestY = from_fixed(request.origin.y);
        // 注: 各ステージの初期化はupdateStageCache内で行われる

        // 最終ステージのキャッシュを更新（再帰的に前段ステージも更新される）
        // updateStageCache内で上流をpullするため、計測はこの後から開始
        updateStageCache(passes_ - 1, upstream, request, requestY);

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

        // 最終ステージの列合計から出力行を計算
        computeStageOutputRow(stages_[static_cast<size_t>(passes_ - 1)], output, request.width);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(output), request.origin);
    }

    // ステージsのキャッシュを更新（requestYに対応する出力が得られるように）
    void updateStageCache(int stageIndex, Node* upstream, const RenderRequest& request, int newY) {
        BlurStage& stage = stages_[static_cast<size_t>(stageIndex)];
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
        ViewPort dstView = stage.rowCache[static_cast<size_t>(cacheIndex)].view();
        std::memset(dstView.data, 0, static_cast<size_t>(cacheWidth_) * 4);

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
            std::memcpy(static_cast<uint8_t*>(dstView.data) + dstStartX * 4, srcPtr, static_cast<size_t>(copyWidth) * 4);
        }
    }

    // 前段ステージから1行取得して現ステージのキャッシュに格納
    void fetchRowFromPrevStage(int stageIndex, Node* upstream, const RenderRequest& request,
                                int srcY, int cacheIndex) {
        BlurStage& stage = stages_[static_cast<size_t>(stageIndex)];
        BlurStage& prevStage = stages_[static_cast<size_t>(stageIndex - 1)];

        // 前段ステージのキャッシュを更新
        updateStageCache(stageIndex - 1, upstream, request, srcY);

        // 前段ステージの列合計から1行を計算してキャッシュに格納
        ViewPort dstView = stage.rowCache[static_cast<size_t>(cacheIndex)].view();
        uint8_t* dstRow = static_cast<uint8_t*>(dstView.data);

        int ks = kernelSize();
        for (size_t x = 0; x < static_cast<size_t>(cacheWidth_); x++) {
            size_t off = x * 4;
            if (prevStage.colSumA[x] > 0) {
                dstRow[off]     = static_cast<uint8_t>(prevStage.colSumR[x] / prevStage.colSumA[x]);
                dstRow[off + 1] = static_cast<uint8_t>(prevStage.colSumG[x] / prevStage.colSumA[x]);
                dstRow[off + 2] = static_cast<uint8_t>(prevStage.colSumB[x] / prevStage.colSumA[x]);
                dstRow[off + 3] = static_cast<uint8_t>(prevStage.colSumA[x] / static_cast<uint32_t>(ks));
            } else {
                dstRow[off] = dstRow[off + 1] = dstRow[off + 2] = dstRow[off + 3] = 0;
            }
        }
    }

    // ステージの列合計を更新（加算/減算）
    void updateStageColSum(BlurStage& stage, int cacheIndex, bool add) {
        const uint8_t* row = static_cast<const uint8_t*>(stage.rowCache[static_cast<size_t>(cacheIndex)].view().data);
        int sign = add ? 1 : -1;
        for (size_t x = 0; x < static_cast<size_t>(cacheWidth_); x++) {
            size_t off = x * 4;
            int32_t a = row[off + 3] * sign;
            int32_t ra = row[off] * a;
            int32_t ga = row[off + 1] * a;
            int32_t ba = row[off + 2] * a;
            stage.colSumR[x] += static_cast<uint32_t>(ra);
            stage.colSumG[x] += static_cast<uint32_t>(ga);
            stage.colSumB[x] += static_cast<uint32_t>(ba);
            stage.colSumA[x] += static_cast<uint32_t>(a);
        }
    }

    // ステージの列合計から出力行を計算
    void computeStageOutputRow(BlurStage& stage, ImageBuffer& output, int width) {
        uint8_t* outRow = static_cast<uint8_t*>(output.view().data);
        int ks = kernelSize();
        for (size_t x = 0; x < static_cast<size_t>(width); x++) {
            size_t off = x * 4;
            if (stage.colSumA[x] > 0) {
                outRow[off]     = static_cast<uint8_t>(stage.colSumR[x] / stage.colSumA[x]);
                outRow[off + 1] = static_cast<uint8_t>(stage.colSumG[x] / stage.colSumA[x]);
                outRow[off + 2] = static_cast<uint8_t>(stage.colSumB[x] / stage.colSumA[x]);
                outRow[off + 3] = static_cast<uint8_t>(stage.colSumA[x] / static_cast<uint32_t>(ks));
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
        size_t cacheRows = static_cast<size_t>(kernelSize());  // radius*2+1
        stage.rowCache.resize(cacheRows);
        stage.rowOriginX.assign(cacheRows, 0);
        for (size_t i = 0; i < cacheRows; i++) {
            stage.rowCache[i] = ImageBuffer(width, 1, PixelFormatIDs::RGBA8_Straight,
                                            InitPolicy::Zero);
        }
        stage.colSumR.assign(static_cast<size_t>(width), 0);
        stage.colSumG.assign(static_cast<size_t>(width), 0);
        stage.colSumB.assign(static_cast<size_t>(width), 0);
        stage.colSumA.assign(static_cast<size_t>(width), 0);
        stage.currentY = 0;
        stage.cacheReady = false;
    }

    // 全ステージを初期化
    void initializeStages(int width) {
        cacheWidth_ = width;
        stages_.resize(static_cast<size_t>(passes_));
        for (size_t i = 0; i < static_cast<size_t>(passes_); i++) {
            initializeStage(stages_[i], width);
        }
    }

    // ========================================
    // push型用ヘルパー関数
    // ========================================

    // パイプラインステージの伝播処理
    void propagatePipelineStages() {
        int ks = kernelSize();

        // Stage 0の出力を計算してStage 1以降に伝播
        for (int s = 1; s < passes_; s++) {
            BlurStage& prevStage = stages_[static_cast<size_t>(s - 1)];
            BlurStage& stage = stages_[static_cast<size_t>(s)];

            // 前段ステージの列合計から1行を計算
            ImageBuffer stageInput(cacheWidth_, 1, PixelFormatIDs::RGBA8_Straight,
                                   InitPolicy::Uninitialized);
            uint8_t* stageRow = static_cast<uint8_t*>(stageInput.view().data);

            for (size_t x = 0; x < static_cast<size_t>(cacheWidth_); x++) {
                size_t off = x * 4;
                if (prevStage.colSumA[x] > 0) {
                    stageRow[off]     = static_cast<uint8_t>(prevStage.colSumR[x] / prevStage.colSumA[x]);
                    stageRow[off + 1] = static_cast<uint8_t>(prevStage.colSumG[x] / prevStage.colSumA[x]);
                    stageRow[off + 2] = static_cast<uint8_t>(prevStage.colSumB[x] / prevStage.colSumA[x]);
                    stageRow[off + 3] = static_cast<uint8_t>(prevStage.colSumA[x] / static_cast<uint32_t>(ks));
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
            ViewPort dstView = stage.rowCache[static_cast<size_t>(slot)].view();
            std::memcpy(dstView.data, srcView.data, static_cast<size_t>(cacheWidth_) * 4);

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
        BlurStage& lastStage = stages_[static_cast<size_t>(passes_ - 1)];
        int ks = kernelSize();

        ImageBuffer output(cacheWidth_, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized);
        uint8_t* outRow = static_cast<uint8_t*>(output.view().data);

        for (size_t x = 0; x < static_cast<size_t>(cacheWidth_); x++) {
            size_t off = x * 4;
            if (lastStage.colSumA[x] > 0) {
                outRow[off]     = static_cast<uint8_t>(lastStage.colSumR[x] / lastStage.colSumA[x]);
                outRow[off + 1] = static_cast<uint8_t>(lastStage.colSumG[x] / lastStage.colSumA[x]);
                outRow[off + 2] = static_cast<uint8_t>(lastStage.colSumB[x] / lastStage.colSumA[x]);
                outRow[off + 3] = static_cast<uint8_t>(lastStage.colSumA[x] / static_cast<uint32_t>(ks));
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
        ViewPort dstView = stage.rowCache[static_cast<size_t>(cacheIndex)].view();
        const uint8_t* srcData = static_cast<const uint8_t*>(srcView.data);
        uint8_t* dstData = static_cast<uint8_t*>(dstView.data);
        int srcWidth = static_cast<int>(srcView.width);

        // キャッシュをゼロクリア
        std::memset(dstData, 0, static_cast<size_t>(cacheWidth_) * 4);

        // コピー範囲の計算
        int dstStart = std::max(0, -xOffset);
        int srcStart = std::max(0, xOffset);
        int dstEnd = std::min(cacheWidth_, srcWidth - xOffset);
        int copyWidth = dstEnd - dstStart;

        if (copyWidth > 0 && srcStart < srcWidth) {
            std::memcpy(dstData + dstStart * 4, srcData + srcStart * 4, static_cast<size_t>(copyWidth) * 4);
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_VERTICAL_BLUR_NODE_H
