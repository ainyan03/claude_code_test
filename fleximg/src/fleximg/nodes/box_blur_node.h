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
    int kernelSize() const { return radius_ * 2 + 1; }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "BoxBlurNode"; }

    // ========================================
    // 準備・終了処理（pull型用）
    // ========================================

    void prepare(const RenderRequest& screenInfo) override {
        screenWidth_ = screenInfo.width;
        screenHeight_ = screenInfo.height;
        screenOrigin_ = screenInfo.origin;

        // radius=0の場合はキャッシュ不要
        if (radius_ == 0) return;

        // キャッシュを初期化（幅は出力幅 = 横ブラー済みデータ）
        initializeCache(screenWidth_);

        currentY_ = 0;
        cacheReady_ = false;

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        // キャッシュ確保をメトリクスに記録
        size_t cacheBytes = kernelSize() * cacheWidth_ * 4 + cacheWidth_ * 4 * sizeof(uint32_t);
        PerfMetrics::instance().nodes[NodeType::BoxBlur].recordAlloc(
            cacheBytes, cacheWidth_, kernelSize());
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

    // ========================================
    // push型インターフェース
    // ========================================

    bool pushPrepare(const PrepareRequest& request) override {
        // 循環参照検出（Node::pushPrepareと同じロジック）
        if (pushPrepareState_ == PrepareState::Preparing) {
            pushPrepareState_ = PrepareState::CycleError;
            return false;
        }
        if (pushPrepareState_ == PrepareState::Prepared) {
            return true;
        }
        if (pushPrepareState_ == PrepareState::CycleError) {
            return false;
        }
        pushPrepareState_ = PrepareState::Preparing;

        // radius=0の場合はスルー（キャッシュ不要）
        if (radius_ == 0) {
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

        // push用状態を初期化
        pushInputY_ = 0;
        pushOutputY_ = 0;
        pushInputWidth_ = request.width;
        pushInputHeight_ = request.height;
        // 出力サイズ = 入力サイズ + radius*2（pull型と対称）
        pushOutputWidth_ = pushInputWidth_ + radius_ * 2;
        pushOutputHeight_ = pushInputHeight_ + radius_ * 2;
        pushInputOriginX_ = 0;
        pushInputOriginY_ = 0;
        pushInputOriginSet_ = false;

        // キャッシュを初期化（出力幅 = 入力幅 + radius*2）
        initializeCache(pushOutputWidth_);

        // 下流へ伝播（prepare()は呼ばない - push専用初期化済み）
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

        // 最初の入力のoriginを保存
        if (!pushInputOriginSet_) {
            pushInputOriginX_ = input.origin.x;
            pushInputOriginY_ = input.origin.y;
            pushInputOriginSet_ = true;
        }

        if (!input.isValid()) {
            pushInputY_++;
            // 入力が無効でも出力タイミングなら出力（ゼロ行として扱う）
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

        // 古い行を列合計から減算（kernelSize行以上受け取った後）
        if (pushInputY_ >= kernelSize()) {
            updateColSum(slot, false);
        }

        // 入力行を横ブラーしてキャッシュに格納（出力幅の中央に配置）
        storeInputRowToCache(converted, slot);

        // 新しい行を列合計に加算
        updateColSum(slot, true);

        pushInputY_++;

        // 各入力行に対して1行出力（出力Y = pushInputY_ - 1）
        // 上端パディング: 列合計にはまだ全行揃っていないが、そのまま出力
        emitBlurredLine();
    }

    void pushFinalize() override {
        // radius=0の場合はスルー
        if (radius_ == 0) {
            Node::pushFinalize();
            return;
        }

        // 残りの行を出力（下端はゼロ扱い、出力サイズは入力+radius*2）
        while (pushOutputY_ < pushOutputHeight_) {
            // ゼロ行をキャッシュに追加して列合計を更新
            int slot = pushInputY_ % kernelSize();
            if (pushInputY_ >= kernelSize()) {
                updateColSum(slot, false);
            }
            // ゼロクリア済みのキャッシュ行を使用（加算しても影響なし）
            std::memset(rowCache_[slot].view().data, 0, cacheWidth_ * 4);
            // updateColSum(slot, true) は不要（ゼロなので）

            pushInputY_++;
            emitBlurredLine();
        }

        Node::pushFinalize();
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

        // radius=0の場合は処理をスキップしてスルー出力
        if (radius_ == 0) {
            return upstream->pullProcess(request);
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
        auto& metrics = PerfMetrics::instance().nodes[NodeType::BoxBlur];
        metrics.requestedPixels += static_cast<uint64_t>(request.width + radius_ * 2) * 1;
        metrics.usedPixels += static_cast<uint64_t>(request.width) * 1;
#endif

        int requestY = from_fixed8(request.origin.y);

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
    int currentY_ = 0;                       // 現在の出力Y座標
    bool cacheReady_ = false;                // キャッシュ初期化済みフラグ

    // push型処理用の状態
    int pushInputY_ = 0;                     // 受け取った入力行数
    int pushOutputY_ = 0;                    // 出力した行数
    int pushInputWidth_ = 0;                 // 入力幅
    int pushInputHeight_ = 0;                // 入力高さ
    int pushOutputWidth_ = 0;                // 出力幅（入力幅 + radius*2）
    int pushOutputHeight_ = 0;               // 出力高さ（入力高さ + radius*2）
    int_fixed8 pushInputOriginX_ = 0;        // 入力のorigin.x
    int_fixed8 pushInputOriginY_ = 0;        // 入力のorigin.y
    bool pushInputOriginSet_ = false;        // origin設定済みフラグ

    // ========================================
    // キャッシュ管理（push/pull共通）
    // ========================================

    // キャッシュを初期化（push/pull共通）
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

    // キャッシュを更新（初期化時はinitializeCache()でゼロ初期化済みのため減算は影響なし）
    void updateCache(Node* upstream, const RenderRequest& request, int newY) {
        if (currentY_ == newY) return;

        int step = (currentY_ < newY) ? 1 : -1;

        while (currentY_ != newY) {
            // 新しい行のY座標とスロット位置を計算
            // 注: 出ていく行と入ってくる行はkernelSize離れているため同じスロットになる
            int newSrcY = currentY_ + step * (radius_ + 1);
            int slot = newSrcY % kernelSize();
            if (slot < 0) slot += kernelSize();

            // 古い行を列合計から減算（初期化時はゼロなので影響なし）
            updateColSum(slot, false);

            // 新しい行を取得して格納
            fetchRowToCache(upstream, request, newSrcY, slot);

            // 新しい行を列合計に加算
            updateColSum(slot, true);

            currentY_ += step;
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
        // pull型: inputOffset = radius（入力[0, kernelSize)で初期ウィンドウ）
        applyHorizontalBlurWithPadding(inputRow.data(), inputWidth,
                                       static_cast<uint8_t*>(dstView.data), outputWidth,
                                       radius_);
    }

    // 横方向スライディングウィンドウブラー（push/pull共通）
    // inputOffset: 出力x=0に対応するカーネル中心の入力座標
    //   - pull型: inputOffset = radius（入力[0]がカーネル左端に対応）
    //   - push型: inputOffset = -radius（出力[radius]のカーネル中心が入力[0]に対応）
    // 入力範囲外はゼロ扱い
    void applyHorizontalBlurWithPadding(
        const uint8_t* input, int inputWidth,
        uint8_t* output, int outputWidth,
        int inputOffset
    ) {
        // 初期ウィンドウの合計（出力x=0に対応）
        // カーネル範囲: 入力[inputOffset - radius, inputOffset + radius]
        uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;
        for (int kx = -radius_; kx <= radius_; kx++) {
            int srcX = inputOffset + kx;
            if (srcX >= 0 && srcX < inputWidth) {
                int off = srcX * 4;
                uint32_t a = input[off + 3];
                sumR += input[off] * a;
                sumG += input[off + 1] * a;
                sumB += input[off + 2] * a;
                sumA += a;
            }
        }
        writeBlurredPixel(output, 0, sumR, sumG, sumB, sumA, kernelSize());

        // スライディング: x = 1 to outputWidth-1
        for (int x = 1; x < outputWidth; x++) {
            // 出ていくピクセル: 入力[inputOffset + x - 1 - radius]
            int oldSrcX = inputOffset + x - 1 - radius_;
            if (oldSrcX >= 0 && oldSrcX < inputWidth) {
                int off = oldSrcX * 4;
                uint32_t a = input[off + 3];
                sumR -= input[off] * a;
                sumG -= input[off + 1] * a;
                sumB -= input[off + 2] * a;
                sumA -= a;
            }

            // 入ってくるピクセル: 入力[inputOffset + x + radius]
            int newSrcX = inputOffset + x + radius_;
            if (newSrcX >= 0 && newSrcX < inputWidth) {
                int off = newSrcX * 4;
                uint32_t a = input[off + 3];
                sumR += input[off] * a;
                sumG += input[off + 1] * a;
                sumB += input[off + 2] * a;
                sumA += a;
            }

            writeBlurredPixel(output, x, sumR, sumG, sumB, sumA, kernelSize());
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

    // 指定行を列合計に加算/減算（α加重平均のため、RGB×αで蓄積）
    void updateColSum(int cacheIndex, bool add) {
        const uint8_t* row = static_cast<const uint8_t*>(rowCache_[cacheIndex].view().data);
        for (int x = 0; x < cacheWidth_; x++) {
            int off = x * 4;
            uint32_t a = row[off + 3];
            uint32_t ra = static_cast<uint32_t>(row[off]) * a;
            uint32_t ga = static_cast<uint32_t>(row[off + 1]) * a;
            uint32_t ba = static_cast<uint32_t>(row[off + 2]) * a;
            if (add) {
                colSumR_[x] += ra;
                colSumG_[x] += ga;
                colSumB_[x] += ba;
                colSumA_[x] += a;
            } else {
                colSumR_[x] -= ra;
                colSumG_[x] -= ga;
                colSumB_[x] -= ba;
                colSumA_[x] -= a;
            }
        }
    }

    // 縦方向の列合計から出力行を計算（push/pull共通）
    void computeBlurredRow(uint8_t* outRow, int width) {
        for (int x = 0; x < width; x++) {
            writeBlurredPixel(outRow, x, colSumR_[x], colSumG_[x], colSumB_[x], colSumA_[x], kernelSize());
        }
    }

    // pull型用ラッパー（既存インターフェース互換）
    void computeOutputRow(ImageBuffer& output, const RenderRequest& request) {
        computeBlurredRow(static_cast<uint8_t*>(output.view().data), request.width);
    }

    // ========================================
    // push型用ヘルパー関数
    // ========================================

    // 入力行を横ブラーしてキャッシュに格納
    // 出力幅 = 入力幅 + radius*2、入力は中央に配置
    void storeInputRowToCache(const ImageBuffer& input, int cacheIndex) {
        ViewPort srcView = input.view();
        ViewPort dstView = rowCache_[cacheIndex].view();
        const uint8_t* srcRow = static_cast<const uint8_t*>(srcView.data);
        uint8_t* dstRow = static_cast<uint8_t*>(dstView.data);
        int outputWidth = cacheWidth_;           // 出力幅 = 入力幅 + radius*2
        // 実際の入力バッファ幅を使用（pushInputWidth_と異なる可能性あり）
        int actualInputWidth = srcView.width;

        // 横方向スライディングウィンドウでブラー処理
        // push型: inputOffset = -radius（出力[radius]のカーネル中心が入力[0]に対応）
        applyHorizontalBlurWithPadding(srcRow, actualInputWidth,
                                       dstRow, outputWidth,
                                       -radius_);
    }

    // 出力行を計算して下流にpush
    void emitBlurredLine() {
        // 出力バッファを確保（出力幅 = 入力幅 + radius*2）
        ImageBuffer output(cacheWidth_, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized);

        // 列合計から出力行を計算（push/pull共通関数を使用）
        computeBlurredRow(static_cast<uint8_t*>(output.view().data), cacheWidth_);

        // 出力リクエストを構築
        // 出力はradius分拡張されるので、originもradius分オフセット
        // origin.x: 入力位置からradius分左（xが大きいほど左）
        // origin.y: 入力位置からradius分上、そこから出力行分下
        RenderRequest outReq;
        outReq.width = static_cast<int16_t>(cacheWidth_);
        outReq.height = 1;
        outReq.origin.x = pushInputOriginX_ + to_fixed8(radius_);
        outReq.origin.y = pushInputOriginY_ + to_fixed8(radius_ - pushOutputY_);

        pushOutputY_++;

        // 下流にpush
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushProcess(RenderResult(std::move(output), outReq.origin), outReq);
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_BOX_BLUR_NODE_H
