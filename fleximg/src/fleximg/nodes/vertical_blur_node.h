#ifndef FLEXIMG_VERTICAL_BLUR_NODE_H
#define FLEXIMG_VERTICAL_BLUR_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>  // for int32_t, uint32_t

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

    // getDataRange: 垂直ブラーはX範囲に影響しないので上流をそのまま返す
    DataRange getDataRange(const RenderRequest& request) const override {
        Node* upstream = upstreamNode(0);
        if (upstream) {
            return upstream->getDataRange(request);
        }
        return DataRange();
    }

    // 準備・終了処理（pull型用）
    void prepare(const RenderRequest& screenInfo) override;
    void finalize() override;

    // Template Method フック
    PrepareResponse onPullPrepare(const PrepareRequest& request) override;
    PrepareResponse onPushPrepare(const PrepareRequest& request) override;
    void onPushProcess(RenderResponse&& input, const RenderRequest& request) override;
    void onPushFinalize() override;

protected:
    int nodeTypeForMetrics() const override { return NodeType::VerticalBlur; }
    RenderResponse onPullProcess(const RenderRequest& request) override;

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

    // 内部実装（宣言のみ）
    RenderResponse pullProcessPipeline(Node* upstream, const RenderRequest& request);
    void updateStageCache(int stageIndex, Node* upstream, const RenderRequest& request, int newY);
    void fetchRowToStageCache(BlurStage& stage, Node* upstream, const RenderRequest& request, int srcY, int cacheIndex);
    void fetchRowFromPrevStage(int stageIndex, Node* upstream, const RenderRequest& request, int srcY, int cacheIndex);
    void updateStageColSum(BlurStage& stage, int cacheIndex, bool add);
    void computeStageOutputRow(BlurStage& stage, ImageBuffer& output, int width);
    void initializeStage(BlurStage& stage, int width);
    void initializeStages(int width);
    void propagatePipelineStages();
    void emitBlurredLinePipeline();
    void storeInputRowToStageCache(BlurStage& stage, const ImageBuffer& input, int cacheIndex, int xOffset = 0);
};

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {

// ========================================
// 準備・終了処理
// ========================================

void VerticalBlurNode::prepare(const RenderRequest& screenInfo) {
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

void VerticalBlurNode::finalize() {
    // パイプラインステージをクリア
    for (auto& stage : stages_) {
        stage.clear();
    }
    stages_.clear();
}

// ========================================
// Template Method フック
// ========================================

PrepareResponse VerticalBlurNode::onPullPrepare(const PrepareRequest& request) {
    // 上流へ伝播
    Node* upstream = upstreamNode(0);
    if (!upstream) {
        // 上流なし: サイズ0を返す
        PrepareResponse result;
        result.status = PrepareStatus::Prepared;
        return result;
    }

    PrepareResponse upstreamResult = upstream->pullPrepare(request);
    if (!upstreamResult.ok()) {
        return upstreamResult;
    }

    // 準備処理
    RenderRequest screenInfo;
    screenInfo.width = request.width;
    screenInfo.height = request.height;
    screenInfo.origin = request.origin;
    prepare(screenInfo);

    // radius=0の場合はパススルー
    if (radius_ == 0) {
        return upstreamResult;
    }

    // 垂直ぼかしはY方向に radius * passes 分拡張する
    // AABBの高さを拡張し、originのYをシフト
    int expansion = radius_ * passes_;
    upstreamResult.height = static_cast<int16_t>(upstreamResult.height + expansion * 2);
    upstreamResult.origin.y = upstreamResult.origin.y + to_fixed(expansion);

    return upstreamResult;
}

PrepareResponse VerticalBlurNode::onPushPrepare(const PrepareRequest& request) {
    // 下流へ先に伝播してサイズ情報を取得
    Node* downstream = downstreamNode(0);
    PrepareResponse downstreamResult;
    if (downstream) {
        downstreamResult = downstream->pushPrepare(request);
        if (!downstreamResult.ok()) {
            return downstreamResult;
        }
    } else {
        // 下流なし: 有効なデータがないのでサイズ0を返す
        downstreamResult.status = PrepareStatus::Prepared;
        // width/height/originはデフォルト値（0）のまま
        return downstreamResult;
    }

    // radius=0の場合はスルー（キャッシュ初期化不要）
    if (radius_ == 0) {
        return downstreamResult;
    }

    // push用状態を初期化（下流から取得したサイズを使用）
    pushInputY_ = 0;
    pushOutputY_ = 0;
    pushInputWidth_ = downstreamResult.width;
    pushInputHeight_ = downstreamResult.height;
    // 出力高さ = 入力高さ（push型ではサイズを変えない、エッジはゼロパディング）
    pushOutputHeight_ = pushInputHeight_;
    baseOriginX_ = downstreamResult.origin.x;  // 基準origin.x
    pushInputOriginY_ = downstreamResult.origin.y;
    lastInputOriginY_ = downstreamResult.origin.y;

    // パイプライン方式でキャッシュを初期化（passes=1でもstages_[0]を使用）
    initializeStages(pushInputWidth_);
    // 各ステージのpush状態をリセット
    for (auto& stage : stages_) {
        stage.pushInputY = 0;
        stage.pushOutputY = 0;
    }

    return downstreamResult;
}

void VerticalBlurNode::onPushProcess(RenderResponse&& input, const RenderRequest& request) {
    // radius=0の場合はスルー
    if (radius_ == 0) {
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushProcess(std::move(input), request);
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

void VerticalBlurNode::onPushFinalize() {
    // radius=0の場合はデフォルト動作
    if (radius_ == 0) {
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushFinalize();
        }
        finalize();
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

    // デフォルト動作: 下流へ伝播し、finalize()を呼び出す
    Node* downstream = downstreamNode(0);
    if (downstream) {
        downstream->pushFinalize();
    }
    finalize();
}

RenderResponse VerticalBlurNode::onPullProcess(const RenderRequest& request) {
    Node* upstream = upstreamNode(0);
    if (!upstream) return RenderResponse();

    // radius=0の場合は処理をスキップしてスルー出力
    if (radius_ == 0) {
        return upstream->pullProcess(request);
    }

    // パイプライン方式で処理（passes=1でもstages_[0]を使用）
    return pullProcessPipeline(upstream, request);
}

// ========================================
// パイプライン処理
// ========================================

RenderResponse VerticalBlurNode::pullProcessPipeline(Node* upstream, const RenderRequest& request) {
    int requestY = from_fixed(request.origin.y);
    // 注: 各ステージの初期化はupdateStageCache内で行われる

    // 最終ステージのキャッシュを更新（再帰的に前段ステージも更新される）
    // updateStageCache内で上流をpullするため、計測はこの後から開始
    updateStageCache(passes_ - 1, upstream, request, requestY);

    FLEXIMG_METRICS_SCOPE(NodeType::VerticalBlur);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
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

    return RenderResponse(std::move(output), request.origin);
}

void VerticalBlurNode::updateStageCache(int stageIndex, Node* upstream, const RenderRequest& request, int newY) {
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

void VerticalBlurNode::fetchRowToStageCache(BlurStage& stage, Node* upstream, const RenderRequest& request,
                                             int srcY, int cacheIndex) {
    RenderRequest upstreamReq;
    upstreamReq.width = request.width;
    upstreamReq.height = 1;
    upstreamReq.origin.x = request.origin.x;
    upstreamReq.origin.y = to_fixed(srcY);

    RenderResponse result = upstream->pullProcess(upstreamReq);

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

void VerticalBlurNode::fetchRowFromPrevStage(int stageIndex, Node* upstream, const RenderRequest& request,
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

void VerticalBlurNode::updateStageColSum(BlurStage& stage, int cacheIndex, bool add) {
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

void VerticalBlurNode::computeStageOutputRow(BlurStage& stage, ImageBuffer& output, int width) {
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

void VerticalBlurNode::initializeStage(BlurStage& stage, int width) {
    size_t cacheRows = static_cast<size_t>(kernelSize());  // radius*2+1
    stage.rowCache.resize(cacheRows);
    stage.rowOriginX.assign(cacheRows, 0);
    for (size_t i = 0; i < cacheRows; i++) {
        stage.rowCache[i] = ImageBuffer(width, 1, PixelFormatIDs::RGBA8_Straight,
                                        InitPolicy::Zero, allocator_);
    }
    stage.colSumR.assign(static_cast<size_t>(width), 0);
    stage.colSumG.assign(static_cast<size_t>(width), 0);
    stage.colSumB.assign(static_cast<size_t>(width), 0);
    stage.colSumA.assign(static_cast<size_t>(width), 0);
    stage.currentY = 0;
    stage.cacheReady = false;
}

void VerticalBlurNode::initializeStages(int width) {
    cacheWidth_ = width;
    stages_.resize(static_cast<size_t>(passes_));
    for (size_t i = 0; i < static_cast<size_t>(passes_); i++) {
        initializeStage(stages_[i], width);
    }
}

// ========================================
// push型用ヘルパー関数
// ========================================

void VerticalBlurNode::propagatePipelineStages() {
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

void VerticalBlurNode::emitBlurredLinePipeline() {
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
        downstream->pushProcess(RenderResponse(std::move(output), outReq.origin), outReq);
    }
}

void VerticalBlurNode::storeInputRowToStageCache(BlurStage& stage, const ImageBuffer& input, int cacheIndex, int xOffset) {
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

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_VERTICAL_BLUR_NODE_H
