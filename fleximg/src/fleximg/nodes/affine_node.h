#ifndef FLEXIMG_AFFINE_NODE_H
#define FLEXIMG_AFFINE_NODE_H

#include "../core/node.h"
#include "../core/common.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include "../operations/transform.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstring>
#include <cmath>
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// AffineNode - アフィン変換ノード
// ========================================================================
//
// 入力画像に対してアフィン変換（回転・拡縮・平行移動）を適用します。
// - 入力: 1ポート
// - 出力: 1ポート
//
// 特徴:
// - tx/ty を Q16.16 固定小数点で保持し、サブピクセル精度の平行移動に対応
// - 回転・拡縮時に tx/ty の小数成分が DDA に正しく反映される
// - アフィン伝播により、実際のDDA処理はSourceNode/SinkNodeで実行される
//
// 使用例:
//   AffineNode affine;
//   affine.setMatrix(AffineMatrix::rotation(0.5f));
//   src >> affine >> sink;
//

// ========================================================================
// InputRegion - 入力要求領域の情報
// ========================================================================
//
// computeInputRequest() の結果を詳細に保持。
// 分割効率の見積もりに使用。
//
// [DEPRECATED] アフィン伝播により、DDA処理はSourceNode/SinkNodeで実行される。
// この構造体はフォールバック用に残されているが、将来削除予定。
//

struct InputRegion {
    // 4頂点座標（入力空間、Q16.16）
    // corners[0]: 左上, corners[1]: 右上, corners[2]: 左下, corners[3]: 右下
    int_fixed corners_x[4];
    int_fixed corners_y[4];

    // AABB（入力空間、整数）
    int aabbLeft, aabbTop, aabbRight, aabbBottom;

    // 面積情報
    int64_t aabbPixels;          // AABB のピクセル数
    int64_t parallelogramPixels; // 平行四辺形の面積（理論最小値）
    int64_t outputPixels;        // 出力タイルのピクセル数

    // 効率（0.0〜1.0）
    float currentEfficiency() const {
        return aabbPixels > 0 ? static_cast<float>(parallelogramPixels) / static_cast<float>(aabbPixels) : 1.0f;
    }
};

// ========================================================================
// AffineResult - applyAffine の結果（実際に書き込んだ範囲）
// ========================================================================
//
// DDA ループ内で追跡した有効ピクセル範囲を返す。
// AABB フィット出力やタイル分割最適化に活用可能。
//
// [DEPRECATED] アフィン伝播により、DDA処理はSourceNode/SinkNodeで実行される。
// この構造体はフォールバック用に残されているが、将来削除予定。
//

struct AffineResult {
    int minX = INT_MAX;
    int maxX = INT_MIN;
    int minY = INT_MAX;
    int maxY = INT_MIN;

    bool isEmpty() const { return minX > maxX || minY > maxY; }
    int width() const { return isEmpty() ? 0 : maxX - minX + 1; }
    int height() const { return isEmpty() ? 0 : maxY - minY + 1; }
};

class AffineNode : public Node {
public:
    AffineNode() {
        initPorts(1, 1);  // 入力1、出力1
    }

    // ========================================
    // 変換設定
    // ========================================

    void setMatrix(const AffineMatrix& m) { matrix_ = m; }
    const AffineMatrix& matrix() const { return matrix_; }

    // 便利なセッター（各セッターは担当要素のみを変更し、他の要素は維持）

    // 回転を設定（a,b,c,dのみ変更、tx,tyは維持）
    void setRotation(float radians) {
        float c = std::cosf(radians);
        float s = std::sinf(radians);
        matrix_.a = c;  matrix_.b = -s;
        matrix_.c = s;  matrix_.d = c;
    }

    // スケールを設定（a,b,c,dのみ変更、tx,tyは維持）
    void setScale(float sx, float sy) {
        matrix_.a = sx; matrix_.b = 0;
        matrix_.c = 0;  matrix_.d = sy;
    }

    // 平行移動を設定（tx,tyのみ変更、a,b,c,dは維持）
    void setTranslation(float tx, float ty) {
        matrix_.tx = tx; matrix_.ty = ty;
    }

    // 回転+スケールを設定（a,b,c,dのみ変更、tx,tyは維持）
    void setRotationScale(float radians, float sx, float sy) {
        float c = std::cosf(radians);
        float s = std::sinf(radians);
        matrix_.a = c * sx;  matrix_.b = -s * sy;
        matrix_.c = s * sx;  matrix_.d = c * sy;
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "AffineNode"; }

    // ========================================
    // 準備処理
    // ========================================

    void prepare(const RenderRequest& screenInfo) override {
        (void)screenInfo;

        // 逆行列を事前計算（2x2部分のみ、tx/tyは別途管理）
        invMatrix_ = inverseFixed16(matrix_);

        // 順変換行列を事前計算（AABB分割・プッシュモード用）
        fwdMatrix_ = toFixed16(matrix_);

        // tx/ty を Q16.16 固定小数点で保持（サブピクセル精度）
        txFixed_ = float_to_fixed(matrix_.tx);
        tyFixed_ = float_to_fixed(matrix_.ty);
    }

    // ========================================
    // PrepareRequest対応（循環検出+アフィン伝播）
    // ========================================
    //
    // アフィン行列を上流に伝播し、SourceNodeで一括実行する。
    // 複数のAffineNodeがある場合は行列を合成する。
    //

    bool pullPrepare(const PrepareRequest& request) override {
        bool shouldContinue;
        if (!checkPrepareState(pullPrepareState_, shouldContinue)) {
            return false;
        }
        if (!shouldContinue) {
            return true;  // DAG共有ノード: スキップ
        }

        // 上流に渡すためのコピーを作成し、自身の行列を累積
        PrepareRequest upstreamRequest = request;
        if (upstreamRequest.hasAffine) {
            // 既存の行列（下流側）に自身の行列（上流側）を後から掛ける
            // result = existing * self（dst = M_downstream * M_upstream * src）
            upstreamRequest.affineMatrix = upstreamRequest.affineMatrix * matrix_;
        } else {
            upstreamRequest.affineMatrix = matrix_;
            upstreamRequest.hasAffine = true;
        }
        hasAffinePropagated_ = true;

        // 上流へ伝播
        Node* upstream = upstreamNode(0);
        if (upstream) {
            if (!upstream->pullPrepare(upstreamRequest)) {
                pullPrepareState_ = PrepareState::CycleError;
                return false;
            }
        }

        // 準備処理（アフィン伝播済みの場合でも、フォールバック用に計算しておく）
        RenderRequest screenInfo;
        screenInfo.width = request.width;
        screenInfo.height = request.height;
        screenInfo.origin = request.origin;
        prepare(screenInfo);

        pullPrepareState_ = PrepareState::Prepared;
        return true;
    }

    // ========================================
    // プッシュ型準備（行列伝播）
    // ========================================
    //
    // アフィン行列を下流に伝播し、SinkNodeで一括実行する。
    // 複数のAffineNodeがある場合は行列を合成する。
    //

    bool pushPrepare(const PrepareRequest& request) override {
        bool shouldContinue;
        if (!checkPrepareState(pushPrepareState_, shouldContinue)) {
            return false;
        }
        if (!shouldContinue) {
            return true;  // DAG共有ノード: スキップ
        }

        // 準備処理
        RenderRequest screenInfo;
        screenInfo.width = request.width;
        screenInfo.height = request.height;
        screenInfo.origin = request.origin;
        prepare(screenInfo);

        // 下流に渡すためのコピーを作成し、自身の行列を累積
        PrepareRequest downstreamRequest = request;
        if (downstreamRequest.hasPushAffine) {
            // 既存の行列（上流側）に自身の行列（下流側）を後から掛ける
            // result = existing * self（プッシュ型は上流から下流へ）
            downstreamRequest.pushAffineMatrix = downstreamRequest.pushAffineMatrix * matrix_;
        } else {
            downstreamRequest.pushAffineMatrix = matrix_;
            downstreamRequest.hasPushAffine = true;
        }
        hasPushAffinePropagated_ = true;

        // 下流へ伝播
        Node* downstream = downstreamNode(0);
        if (downstream) {
            if (!downstream->pushPrepare(downstreamRequest)) {
                pushPrepareState_ = PrepareState::CycleError;
                return false;
            }
        }

        pushPrepareState_ = PrepareState::Prepared;
        return true;
    }

    // ========================================
    // プル型インターフェース
    // ========================================

    RenderResult pullProcess(const RenderRequest& request) override {
        // 循環エラー状態ならスキップ（無限再帰防止）
        if (pullPrepareState_ != PrepareState::Prepared) {
            return RenderResult();
        }

        Node* upstream = upstreamNode(0);
        if (!upstream) return RenderResult();

        // アフィン伝播済みの場合はパススルー（SourceNodeでDDA処理済み）
        if (hasAffinePropagated_) {
            return upstream->pullProcess(request);
        }

        // ----------------------------------------------------------------
        // [DEPRECATED] 以下はフォールバック処理（アフィン伝播が無効な場合のみ）
        // 通常はSourceNodeでDDA処理されるため、このパスは使用されない。
        // 将来削除予定。
        // ----------------------------------------------------------------

        // 特異行列チェック（prepare で計算済み）
        if (!invMatrix_.valid) {
            return RenderResult();
        }

        // 入力領域を計算（4頂点座標、AABB、面積情報を含む）
        InputRegion region = computeInputRegion(request);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        // ピクセル効率計測
        auto& metrics = PerfMetrics::instance().nodes[NodeType::Affine];
        metrics.usedPixels += static_cast<uint64_t>(region.outputPixels);
        // 分割時の理論最小値（平行四辺形面積 × 2、三角形領域の効率50%を考慮）
        metrics.theoreticalMinPixels += static_cast<uint64_t>(region.parallelogramPixels * 2);
#endif

        // AABB分割判定
        if (shouldSplitAABB(region)) {
            return pullProcessWithAABBSplit(request, region, upstream);
        }

        // 通常処理（分割なし）
        return pullProcessNoSplit(request, region, upstream);
    }

private:
    // ========================================
    // 通常処理（分割なし）
    // ========================================
    RenderResult pullProcessNoSplit(const RenderRequest& request,
                                    const InputRegion& region,
                                    Node* upstream) {
        // RenderRequest を構築
        RenderRequest inputReq;
        inputReq.width = static_cast<int16_t>(region.aabbRight - region.aabbLeft + 1);
        inputReq.height = static_cast<int16_t>(region.aabbBottom - region.aabbTop + 1);
        inputReq.origin.x = to_fixed8(-region.aabbLeft);
        inputReq.origin.y = to_fixed8(-region.aabbTop);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::Affine].requestedPixels += static_cast<uint64_t>(region.aabbPixels);
#endif

        // 上流を評価
        RenderResult input = upstream->pullProcess(inputReq);
        // プル型: 無効入力には空Resultを返却
        if (!input.isValid()) {
            return RenderResult(ImageBuffer(), request.origin);
        }

        // process() に委譲
        return process(std::move(input), request);
    }

    // ========================================
    // AABB分割処理
    // ========================================
    RenderResult pullProcessWithAABBSplit(const RenderRequest& request,
                                          const InputRegion& region,
                                          Node* upstream) {
        // 分割戦略を計算
        SplitStrategy strategy = computeSplitStrategy(region);

        // 分割サイズを計算（分割方向の寸法に基づく）
        int splitDim = strategy.splitInX
            ? (region.aabbRight - region.aabbLeft + 1)
            : (region.aabbBottom - region.aabbTop + 1);
        int splitSize = (splitDim + strategy.splitCount - 1) / strategy.splitCount;

        // 出力バッファ（最初の有効な入力でフォーマットを決定）
        ImageBuffer output;
        ViewPort outputView;

        // 全分割の有効範囲を追跡
        AffineResult totalResult;

        for (int i = 0; i < strategy.splitCount; ++i) {
            RenderRequest subReq;

            if (strategy.splitInX) {
                // X方向分割
                int splitLeft = region.aabbLeft + i * splitSize;
                int splitRight = std::min(splitLeft + splitSize - 1, region.aabbRight);
                if (splitLeft > splitRight) break;

                // 台形フィット: このX範囲で必要なY範囲を計算
                auto [yFitMin, yFitMax] = computeYRangeForXStrip(splitLeft, splitRight, region);
                // AABB範囲にクランプ
                yFitMin = std::max(yFitMin, region.aabbTop);
                yFitMax = std::min(yFitMax, region.aabbBottom);
                if (yFitMin > yFitMax) continue;

                subReq.width = static_cast<int16_t>(splitRight - splitLeft + 1);
                subReq.height = static_cast<int16_t>(yFitMax - yFitMin + 1);
                subReq.origin.x = to_fixed8(-splitLeft);
                subReq.origin.y = to_fixed8(-yFitMin);
            } else {
                // Y方向分割
                int splitTop = region.aabbTop + i * splitSize;
                int splitBottom = std::min(splitTop + splitSize - 1, region.aabbBottom);
                if (splitTop > splitBottom) break;

                // 台形フィット: このY範囲で必要なX範囲を計算
                auto [xFitMin, xFitMax] = computeXRangeForYStrip(splitTop, splitBottom, region);
                // AABB範囲にクランプ
                xFitMin = std::max(xFitMin, region.aabbLeft);
                xFitMax = std::min(xFitMax, region.aabbRight);
                if (xFitMin > xFitMax) continue;

                subReq.width = static_cast<int16_t>(xFitMax - xFitMin + 1);
                subReq.height = static_cast<int16_t>(splitBottom - splitTop + 1);
                subReq.origin.x = to_fixed8(-xFitMin);
                subReq.origin.y = to_fixed8(-splitTop);
            }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
            uint64_t splitPixels = static_cast<uint64_t>(subReq.width) * static_cast<uint64_t>(subReq.height);
            PerfMetrics::instance().nodes[NodeType::Affine].requestedPixels += splitPixels;
#endif

            // 上流から取得
            RenderResult subInput = upstream->pullProcess(subReq);
            if (!subInput.isValid()) continue;

            // 出力バッファを遅延初期化（入力フォーマットに合わせる）
            if (!output.isValid()) {
                output = ImageBuffer(request.width, request.height, subInput.buffer.formatID());
#ifdef FLEXIMG_DEBUG_PERF_METRICS
                PerfMetrics::instance().nodes[NodeType::Affine].recordAlloc(
                    output.totalBytes(), output.width(), output.height());
#endif
                outputView = output.view();
            }

            // 部分変換を実行（出力バッファに直接書き込み）
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            auto transformStart = std::chrono::high_resolution_clock::now();
#endif
            AffineResult subResult = applyAffine(outputView, request.origin.x, request.origin.y,
                        subInput.view(), subInput.origin.x, subInput.origin.y);
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            PerfMetrics::instance().nodes[NodeType::Affine].time_us +=
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock::now() - transformStart).count();
#endif

            // 全体の有効範囲を更新
            if (!subResult.isEmpty()) {
                if (subResult.minX < totalResult.minX) totalResult.minX = subResult.minX;
                if (subResult.maxX > totalResult.maxX) totalResult.maxX = subResult.maxX;
                if (subResult.minY < totalResult.minY) totalResult.minY = subResult.minY;
                if (subResult.maxY > totalResult.maxY) totalResult.maxY = subResult.maxY;
            }
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        // 分割処理全体のカウント（time_usは各applyAffineで累積済み）
        PerfMetrics::instance().nodes[NodeType::Affine].count++;
#endif

        // 有効範囲が空なら空のバッファを返す（透明扱い）
        if (totalResult.isEmpty()) {
            return RenderResult(ImageBuffer(), request.origin);
        }

        // 有効範囲が全域と同じなら従来通り
        if (totalResult.minX == 0 && totalResult.minY == 0 &&
            totalResult.maxX == request.width - 1 && totalResult.maxY == request.height - 1) {
            return RenderResult(std::move(output), request.origin);
        }

        // 有効範囲が小さい場合、トリミングして返す
        ImageBuffer trimmed(totalResult.width(), totalResult.height(), output.formatID());
        if (trimmed.isValid()) {
            const int bpp = static_cast<int>(output.bytesPerPixel());
            const size_t rowBytes = static_cast<size_t>(totalResult.width()) * static_cast<size_t>(bpp);
            for (int y = 0; y < totalResult.height(); ++y) {
                std::memcpy(trimmed.pixelAt(0, y),
                            output.pixelAt(totalResult.minX, totalResult.minY + y),
                            rowBytes);
            }
        }

        // origin 調整
        Point newOrigin = {
            request.origin.x - to_fixed8(totalResult.minX),
            request.origin.y - to_fixed8(totalResult.minY)
        };

        return RenderResult(std::move(trimmed), newOrigin);
    }

public:

    // ========================================
    // テスト用アクセサ
    // ========================================
    const Matrix2x2_fixed16& getInvMatrix() const { return invMatrix_; }
    const Matrix2x2_fixed16& getFwdMatrix() const { return fwdMatrix_; }
    int_fixed getTxFixed() const { return txFixed_; }
    int_fixed getTyFixed() const { return tyFixed_; }
    InputRegion testComputeInputRegion(const RenderRequest& request) {
        return computeInputRegion(request);
    }

    // ========================================
    // プッシュ型インターフェース
    // ========================================
    //
    // Renderer下流でアフィン変換を行う場合に使用。
    // 入力画像を順変換して出力サイズを計算し、下流へ渡す。
    //

    void pushProcess(RenderResult&& input,
                     const RenderRequest& request) override {
        (void)request;  // pushモードでは上流用requestは使わない

        // 循環エラー状態ならスキップ
        if (pushPrepareState_ != PrepareState::Prepared) {
            return;
        }

        if (!input.isValid()) {
            Node* downstream = downstreamNode(0);
            if (downstream) {
                downstream->pushProcess(std::move(input), request);
            }
            return;
        }

        // アフィン伝播済みの場合はパススルー（SinkNodeでDDA処理される）
        if (hasPushAffinePropagated_) {
            Node* downstream = downstreamNode(0);
            if (downstream) {
                downstream->pushProcess(std::move(input), request);
            }
            return;
        }

        // ----------------------------------------------------------------
        // [DEPRECATED] 以下はフォールバック処理（アフィン伝播が無効な場合のみ）
        // 通常はSinkNodeでDDA処理されるため、このパスは使用されない。
        // 将来削除予定。
        // ----------------------------------------------------------------

        // 特異行列チェック（フォールバック時のみ使用）
        if (!fwdMatrix_.valid) {
            return;
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
#endif

        // 入力画像情報
        ViewPort inputView = input.view();
        const int inW = inputView.width;
        const int inH = inputView.height;

        // 入力画像の4隅を順変換してAABBを計算
        // 入力座標は基準点相対（Q24.8）
        int_fixed8 in_x[4] = {
            -input.origin.x,
            to_fixed8(inW) - input.origin.x,
            -input.origin.x,
            to_fixed8(inW) - input.origin.x
        };
        int_fixed8 in_y[4] = {
            -input.origin.y,
            -input.origin.y,
            to_fixed8(inH) - input.origin.y,
            to_fixed8(inH) - input.origin.y
        };

        // 順変換: out = fwd * in + t
        // 演算: (Q16.16 * Q24.8) >> 16 = Q24.8
        int_fixed8 minX_f8 = INT32_MAX, minY_f8 = INT32_MAX;
        int_fixed8 maxX_f8 = INT32_MIN, maxY_f8 = INT32_MIN;
        for (int i = 0; i < 4; i++) {
            int64_t ox64 = static_cast<int64_t>(fwdMatrix_.a) * in_x[i]
                         + static_cast<int64_t>(fwdMatrix_.b) * in_y[i];
            int64_t oy64 = static_cast<int64_t>(fwdMatrix_.c) * in_x[i]
                         + static_cast<int64_t>(fwdMatrix_.d) * in_y[i];
            int_fixed8 ox = static_cast<int_fixed8>(ox64 >> INT_FIXED16_SHIFT) + txFixed_;
            int_fixed8 oy = static_cast<int_fixed8>(oy64 >> INT_FIXED16_SHIFT) + tyFixed_;
            minX_f8 = std::min(minX_f8, ox);
            minY_f8 = std::min(minY_f8, oy);
            maxX_f8 = std::max(maxX_f8, ox);
            maxY_f8 = std::max(maxY_f8, oy);
        }

        // floor/ceil で整数化（+マージン）
        int minX = from_fixed8_floor(minX_f8) - 1;
        int minY = from_fixed8_floor(minY_f8) - 1;
        int maxX = from_fixed8_ceil(maxX_f8) + 1;
        int maxY = from_fixed8_ceil(maxY_f8) + 1;

        int outW = maxX - minX;
        int outH = maxY - minY;

        if (outW <= 0 || outH <= 0) {
            return;
        }

        // 出力バッファを作成
        ImageBuffer output(static_cast<int16_t>(outW), static_cast<int16_t>(outH),
                          input.buffer.formatID());
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::Affine].recordAlloc(
            output.totalBytes(), output.width(), output.height());
#endif
        ViewPort outputView = output.view();

        // applyAffine 用の origin: バッファ左上の世界座標の負値
        // applyAffine 内で tx/ty を使うため、ここでは tx/ty を含めない
        int_fixed8 affineOriginX = -to_fixed8(minX);
        int_fixed8 affineOriginY = -to_fixed8(minY);

        // アフィン変換を適用
        applyAffine(outputView, affineOriginX, affineOriginY,
                    inputView, input.origin.x, input.origin.y);

        // RenderResult 用の origin: バッファ左上の基準点相対座標の負値
        // minX/minY は順変換後の座標（tx/ty を含む）なので、origin = -minX で tx が正しく反映
        // SinkNode での配置: dstX = sinkOrigin - (-minX) = sinkOrigin + minX
        //                        = sinkOrigin + (-srcOrigin + tx) = sinkOrigin - srcOrigin + tx
        int_fixed8 outOriginX = -to_fixed8(minX);
        int_fixed8 outOriginY = -to_fixed8(minY);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::Affine];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        // 下流用の RenderRequest を作成
        RenderRequest downstreamReq;
        downstreamReq.width = static_cast<int16_t>(outW);
        downstreamReq.height = static_cast<int16_t>(outH);
        downstreamReq.origin.x = outOriginX;
        downstreamReq.origin.y = outOriginY;

        // 下流へ渡す
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushProcess(
                RenderResult(std::move(output), Point{outOriginX, outOriginY}),
                downstreamReq);
        }
    }

    // ========================================
    // 変換処理（process() オーバーライド）
    // ========================================
    //
    // transform::affine() を呼ばず、直接 DDA ループを実装。
    // tx/ty を Q24.8 固定小数点で扱い、サブピクセル精度を実現。
    //
    // [DEPRECATED] アフィン伝播により、DDA処理はSourceNode/SinkNodeで実行される。
    // この関数はフォールバック用に残されているが、将来削除予定。
    //

    RenderResult process(RenderResult&& input,
                        const RenderRequest& request) override {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
#endif

        // 出力バッファを作成（ゼロ初期化済み）
        ImageBuffer output(request.width, request.height, input.buffer.formatID());
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::Affine].recordAlloc(
            output.totalBytes(), output.width(), output.height());
#endif
        ViewPort outputView = output.view();
        ViewPort inputView = input.view();

        // アフィン変換を適用（tx/ty サブピクセル精度版）
        AffineResult ar = applyAffine(outputView, request.origin.x, request.origin.y,
                                      inputView, input.origin.x, input.origin.y);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::Affine];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        // 有効範囲が空なら空のバッファを返す（透明扱い）
        if (ar.isEmpty()) {
            return RenderResult(ImageBuffer(), request.origin);
        }

        // 有効範囲が全域と同じなら従来通り
        if (ar.minX == 0 && ar.minY == 0 &&
            ar.maxX == request.width - 1 && ar.maxY == request.height - 1) {
            return RenderResult(std::move(output), request.origin);
        }

        // 有効範囲が小さい場合、トリミングして返す
        ImageBuffer trimmed(ar.width(), ar.height(), output.formatID());
        if (trimmed.isValid()) {
            const int bpp = static_cast<int>(output.bytesPerPixel());
            const size_t rowBytes = static_cast<size_t>(ar.width()) * static_cast<size_t>(bpp);
            for (int y = 0; y < ar.height(); ++y) {
                std::memcpy(trimmed.pixelAt(0, y),
                            output.pixelAt(ar.minX, ar.minY + y),
                            rowBytes);
            }
        }

        // origin 調整: 新バッファの (0,0) = 元の (ar.minX, ar.minY)
        Point newOrigin = {
            request.origin.x - to_fixed8(ar.minX),
            request.origin.y - to_fixed8(ar.minY)
        };

        return RenderResult(std::move(trimmed), newOrigin);
    }

protected:
    // ========================================
    // AABB分割パラメータ
    // ========================================
    static constexpr int MIN_SPLIT_SIZE = 16;         // 分割後の最小ピクセル数
    static constexpr int MAX_SPLIT_COUNT = 8;         // 分割数の上限
    static constexpr float AABB_SPLIT_THRESHOLD = 10.0f;  // 閾値（改善倍率）

    // ========================================
    // 分割戦略
    // ========================================
    struct SplitStrategy {
        bool splitInX;      // true: X方向分割, false: Y方向分割
        int splitCount;     // 分割数
    };

    // ========================================
    // AABB分割判定
    // ========================================
    //
    // 改善倍率が閾値以上なら分割を実行する。
    // 改善倍率 = aabbPixels / (parallelogramPixels * 2)
    //
    bool shouldSplitAABB(const InputRegion& region) const {
        if (region.parallelogramPixels == 0) return false;
        float improvementFactor = static_cast<float>(region.aabbPixels)
                                / static_cast<float>(region.parallelogramPixels * 2);
        return improvementFactor >= AABB_SPLIT_THRESHOLD;
    }

    // ========================================
    // 分割戦略の計算
    // ========================================
    //
    // 縦横比で分割方向を決定し、最小サイズに基づき分割数を計算。
    //
    SplitStrategy computeSplitStrategy(const InputRegion& region) const {
        int width = region.aabbRight - region.aabbLeft + 1;
        int height = region.aabbBottom - region.aabbTop + 1;

        // 縦横比で分割方向を決定（長い辺を分割）
        bool splitInX = (width > height);
        int dim = splitInX ? width : height;

        // 分割数を計算（最小サイズ以上を確保）
        int count = dim / MIN_SPLIT_SIZE;
        if (count < 1) count = 1;
        if (count > MAX_SPLIT_COUNT) count = MAX_SPLIT_COUNT;

        return {splitInX, count};
    }

    // ========================================
    // 範囲計算の統一関数（台形フィット用）
    // ========================================
    //
    // 平行四辺形の4頂点から、指定した primary 軸の範囲に対応する
    // secondary 軸の最小・最大値を求める。
    //
    // primary: 問い合わせ軸（Y範囲→X範囲 なら Y座標）
    // secondary: 結果軸（Y範囲→X範囲 なら X座標）
    //
    static std::pair<int, int> computeSecondaryRangeForPrimaryStrip(
        int primaryMin, int primaryMax,
        const int_fixed8* primaryCoords,    // 4要素
        const int_fixed8* secondaryCoords   // 4要素
    ) {
        // Q24.8 から整数に変換
        int p[4], s[4];
        for (int i = 0; i < 4; ++i) {
            p[i] = from_fixed8(primaryCoords[i]);
            s[i] = from_fixed8(secondaryCoords[i]);
        }

        // 4辺: (0,1), (0,2), (1,3), (2,3)
        static constexpr int edges[4][2] = {{0,1}, {0,2}, {1,3}, {2,3}};

        int sMin = INT32_MAX;
        int sMax = INT32_MIN;

        // 各辺との交点を計算
        for (int e = 0; e < 4; ++e) {
            int i0 = edges[e][0];
            int i1 = edges[e][1];
            int p0 = p[i0], p1 = p[i1];
            int s0 = s[i0], s1 = s[i1];

            // 辺が primary 範囲と交差するか確認
            int edgePMin = std::min(p0, p1);
            int edgePMax = std::max(p0, p1);
            if (edgePMax < primaryMin || edgePMin > primaryMax) continue;

            // primary 範囲の両端での交点 secondary 座標を計算
            for (int pv : {primaryMin, primaryMax}) {
                if (pv < edgePMin || pv > edgePMax) continue;
                if (p0 == p1) {
                    // primary 座標が同じ辺の場合
                    sMin = std::min({sMin, s0, s1});
                    sMax = std::max({sMax, s0, s1});
                } else {
                    // 線形補間で交点 secondary 座標を計算
                    int sv = s0 + (s1 - s0) * (pv - p0) / (p1 - p0);
                    sMin = std::min(sMin, sv);
                    sMax = std::max(sMax, sv);
                }
            }
        }

        // primary 範囲内にある頂点も考慮
        for (int i = 0; i < 4; ++i) {
            if (p[i] >= primaryMin && p[i] <= primaryMax) {
                sMin = std::min(sMin, s[i]);
                sMax = std::max(sMax, s[i]);
            }
        }

        // マージン追加（境界の丸め誤差対策）
        return {sMin - 1, sMax + 1};
    }

    // Y範囲に対応するX範囲を計算
    std::pair<int, int> computeXRangeForYStrip(int yMin, int yMax,
                                                const InputRegion& region) const {
        return computeSecondaryRangeForPrimaryStrip(
            yMin, yMax, region.corners_y, region.corners_x);
    }

    // X範囲に対応するY範囲を計算
    std::pair<int, int> computeYRangeForXStrip(int xMin, int xMax,
                                                const InputRegion& region) const {
        return computeSecondaryRangeForPrimaryStrip(
            xMin, xMax, region.corners_x, region.corners_y);
    }

    // ========================================
    // 入力領域計算（詳細版）
    // ========================================
    //
    // 出力要求の4頂点を逆変換し、入力領域の詳細情報を計算する。
    // 分割効率の見積もりにも使用。
    //
    InputRegion computeInputRegion(const RenderRequest& request) {
        InputRegion region;
        region.outputPixels = static_cast<int64_t>(request.width) * request.height;

        constexpr int_fixed8 HALF = 1 << (INT_FIXED8_SHIFT - 1);  // 0.5

        // 出力要求の4頂点を Q24.8 で計算
        // corners: 台形フィット用（ピクセル境界座標）
        // aabb:    AABB計算用（ピクセル中心座標）
        // DDA は (dx+0.5, dy+0.5) をサンプリングするため、AABB はピクセル中心で計算
        int_fixed8 left   = -request.origin.x;
        int_fixed8 right  = to_fixed8(request.width) - request.origin.x;
        int_fixed8 top    = -request.origin.y;
        int_fixed8 bottom = to_fixed8(request.height) - request.origin.y;

        // corners用（境界座標）: [0]=左上, [1]=右上, [2]=左下, [3]=右下
        int_fixed8 corner_x[4] = { left,  right,  left,  right  };
        int_fixed8 corner_y[4] = { top,   top,    bottom, bottom };

        // AABB用（ピクセル中心座標）
        int_fixed8 aabb_x[4] = { left + HALF,  right - HALF,  left + HALF,  right - HALF };
        int_fixed8 aabb_y[4] = { top + HALF,   top + HALF,    bottom - HALF, bottom - HALF };

        // tx/ty を Q24.8 のまま減算（小数部保持）
        for (int i = 0; i < 4; i++) {
            corner_x[i] -= txFixed_;
            corner_y[i] -= tyFixed_;
            aabb_x[i] -= txFixed_;
            aabb_y[i] -= tyFixed_;
        }

        // 逆変換して入力空間の座標を計算（Q24.8 精度）
        // 演算: (Q16.16 * Q24.8) >> 16 = Q24.8
        int_fixed8 minX_f8 = INT32_MAX, minY_f8 = INT32_MAX;
        int_fixed8 maxX_f8 = INT32_MIN, maxY_f8 = INT32_MIN;
        for (int i = 0; i < 4; i++) {
            // corners（台形フィット用）
            int64_t cx64 = static_cast<int64_t>(invMatrix_.a) * corner_x[i]
                         + static_cast<int64_t>(invMatrix_.b) * corner_y[i];
            int64_t cy64 = static_cast<int64_t>(invMatrix_.c) * corner_x[i]
                         + static_cast<int64_t>(invMatrix_.d) * corner_y[i];
            region.corners_x[i] = static_cast<int_fixed8>(cx64 >> INT_FIXED16_SHIFT);
            region.corners_y[i] = static_cast<int_fixed8>(cy64 >> INT_FIXED16_SHIFT);

            // AABB（ピクセル中心座標から計算、数学的に正確）
            int64_t ax64 = static_cast<int64_t>(invMatrix_.a) * aabb_x[i]
                         + static_cast<int64_t>(invMatrix_.b) * aabb_y[i];
            int64_t ay64 = static_cast<int64_t>(invMatrix_.c) * aabb_x[i]
                         + static_cast<int64_t>(invMatrix_.d) * aabb_y[i];
            int_fixed8 ax = static_cast<int_fixed8>(ax64 >> INT_FIXED16_SHIFT);
            int_fixed8 ay = static_cast<int_fixed8>(ay64 >> INT_FIXED16_SHIFT);
            minX_f8 = std::min(minX_f8, ax);
            minY_f8 = std::min(minY_f8, ay);
            maxX_f8 = std::max(maxX_f8, ax);
            maxY_f8 = std::max(maxY_f8, ay);
        }

        // ピクセル中心座標で計算済みなので、補正なしで floor
        int minX = from_fixed8_floor(minX_f8);
        int minY = from_fixed8_floor(minY_f8);
        int maxX = from_fixed8_floor(maxX_f8);
        int maxY = from_fixed8_floor(maxY_f8);

        region.aabbLeft = minX;
        region.aabbTop = minY;
        region.aabbRight = maxX;
        region.aabbBottom = maxY;
        region.aabbPixels = static_cast<int64_t>(region.aabbRight - region.aabbLeft + 1)
                          * (region.aabbBottom - region.aabbTop + 1);

        // 平行四辺形の面積を外積で計算
        // corners: [0]=左上, [1]=右上, [2]=左下, [3]=右下
        // 面積 = |cross(p1-p0, p2-p0)|
        // Q24.8 * Q24.8 = Q48.16、>> 16 で整数に
        int64_t dx1 = region.corners_x[1] - region.corners_x[0];  // 右上 - 左上
        int64_t dy1 = region.corners_y[1] - region.corners_y[0];
        int64_t dx2 = region.corners_x[2] - region.corners_x[0];  // 左下 - 左上
        int64_t dy2 = region.corners_y[2] - region.corners_y[0];
        int64_t cross = dx1 * dy2 - dy1 * dx2;
        region.parallelogramPixels = std::abs(cross) >> INT_FIXED8_SHIFT >> INT_FIXED8_SHIFT;

        return region;
    }

    // ========================================
    // 入力要求計算（RenderRequest を返す版）
    // ========================================
    virtual RenderRequest computeInputRequest(const RenderRequest& request) {
        InputRegion region = computeInputRegion(request);

        RenderRequest inputReq;
        inputReq.width = static_cast<int16_t>(region.aabbRight - region.aabbLeft + 1);
        inputReq.height = static_cast<int16_t>(region.aabbBottom - region.aabbTop + 1);
        inputReq.origin.x = to_fixed8(-region.aabbLeft);
        inputReq.origin.y = to_fixed8(-region.aabbTop);

        return inputReq;
    }

private:
    AffineMatrix matrix_;  // 恒等行列がデフォルト
    Matrix2x2_fixed16 invMatrix_;  // prepare() で計算（2x2逆行列、プル用）
    Matrix2x2_fixed16 fwdMatrix_;  // prepare() で計算（2x2順行列、AABB分割・プッシュ用）
    int_fixed txFixed_ = 0;  // tx を Q16.16 で保持
    int_fixed tyFixed_ = 0;  // ty を Q16.16 で保持
    bool hasAffinePropagated_ = false;      // プル型アフィン伝播済みフラグ
    bool hasPushAffinePropagated_ = false;  // プッシュ型アフィン伝播済みフラグ

    // ========================================
    // アフィン変換実装（tx/ty サブピクセル精度版）
    // ========================================
    //
    // transform::affine() との違い:
    // - tx/ty を Q24.8 固定小数点で扱う
    // - 平行移動の逆変換計算時に小数部を反映
    //

    // ----------------------------------------
    // テンプレート版 DDA 転写ループ（最内ループのみ）
    // ----------------------------------------
    //
    // BytesPerPixel: 1ピクセルあたりのバイト数
    // ピクセル構造を意識せず、データサイズ単位で転写する。
    //
    template<size_t BytesPerPixel>
    static void copyRowDDA(
        uint8_t* dstRow,
        const uint8_t* srcData,
        int32_t srcStride,
        int32_t srcX_fixed,
        int32_t srcY_fixed,
        int32_t fixedInvA,
        int32_t fixedInvC,
        int count
    ) {
        for (int i = 0; i < count; i++) {
            uint32_t sx = static_cast<uint32_t>(srcX_fixed) >> INT_FIXED16_SHIFT;
            uint32_t sy = static_cast<uint32_t>(srcY_fixed) >> INT_FIXED16_SHIFT;

            const uint8_t* srcPixel = srcData + static_cast<size_t>(sy) * static_cast<size_t>(srcStride) + static_cast<size_t>(sx) * BytesPerPixel;

            // データサイズ単位で転写（ピクセル構造を意識しない）
            if constexpr (BytesPerPixel == 8) {
                // 32bitマイコンでも予測可能な動作にするため、32bit×2で転写
                reinterpret_cast<uint32_t*>(dstRow)[0] =
                    reinterpret_cast<const uint32_t*>(srcPixel)[0];
                reinterpret_cast<uint32_t*>(dstRow)[1] =
                    reinterpret_cast<const uint32_t*>(srcPixel)[1];
            } else if constexpr (BytesPerPixel == 4) {
                *reinterpret_cast<uint32_t*>(dstRow) =
                    *reinterpret_cast<const uint32_t*>(srcPixel);
            } else if constexpr (BytesPerPixel == 2) {
                *reinterpret_cast<uint16_t*>(dstRow) =
                    *reinterpret_cast<const uint16_t*>(srcPixel);
            } else if constexpr (BytesPerPixel == 1) {
                *dstRow = *srcPixel;
            } else {
                std::memcpy(dstRow, srcPixel, BytesPerPixel);
            }

            dstRow += BytesPerPixel;
            srcX_fixed += fixedInvA;
            srcY_fixed += fixedInvC;
        }
    }

    // 関数ポインタ型（DDA転写ループ用）
    using CopyRowFunc = void(*)(
        uint8_t* dstRow,
        const uint8_t* srcData,
        int32_t srcStride,
        int32_t srcX_fixed,
        int32_t srcY_fixed,
        int32_t fixedInvA,
        int32_t fixedInvC,
        int count
    );

    // ----------------------------------------
    // applyAffine: 事前準備 + DDA ループ
    // ----------------------------------------
    // 返り値: 実際に書き込んだピクセル範囲
    AffineResult applyAffine(ViewPort& dst, int_fixed8 dstOriginX, int_fixed8 dstOriginY,
                     const ViewPort& src, int_fixed8 srcOriginX, int_fixed8 srcOriginY) {
        AffineResult result;

        if (!dst.isValid() || !src.isValid()) return result;
        if (!invMatrix_.valid) return result;

        const int outW = dst.width;
        const int outH = dst.height;

        // BytesPerPixel に応じて関数ポインタを選択（ループ外で1回だけ分岐）
        CopyRowFunc copyRow = nullptr;
        switch (getBytesPerPixel(src.formatID)) {
            case 8: copyRow = &copyRowDDA<8>; break;
            case 4: copyRow = &copyRowDDA<4>; break;
            case 3: copyRow = &copyRowDDA<3>; break;
            case 2: copyRow = &copyRowDDA<2>; break;
            case 1: copyRow = &copyRowDDA<1>; break;
            default: return result;
        }

        // 固定小数点逆行列の回転/スケール成分
        const int32_t fixedInvA = invMatrix_.a;
        const int32_t fixedInvB = invMatrix_.b;
        const int32_t fixedInvC = invMatrix_.c;
        const int32_t fixedInvD = invMatrix_.d;

        // 原点座標を整数化（固定小数点から変換）
        const int32_t dstOriginXInt = from_fixed8(dstOriginX);
        const int32_t dstOriginYInt = from_fixed8(dstOriginY);
        const int32_t srcOriginXInt = from_fixed8(srcOriginX);
        const int32_t srcOriginYInt = from_fixed8(srcOriginY);

        // ================================================================
        // 逆変換オフセットの計算（tx/ty 固定小数点版）
        // ================================================================
        int64_t invTx64 = -(static_cast<int64_t>(txFixed_) * fixedInvA
                          + static_cast<int64_t>(tyFixed_) * fixedInvB);
        int64_t invTy64 = -(static_cast<int64_t>(txFixed_) * fixedInvC
                          + static_cast<int64_t>(tyFixed_) * fixedInvD);
        int32_t invTxFixed = static_cast<int32_t>(invTx64 >> INT_FIXED8_SHIFT);
        int32_t invTyFixed = static_cast<int32_t>(invTy64 >> INT_FIXED8_SHIFT);

        // DDA用オフセット: 逆変換 + 整数キャンセル + srcOrigin
        const int32_t fixedInvTx = invTxFixed
                            - (dstOriginXInt * fixedInvA)
                            - (dstOriginYInt * fixedInvB)
                            + (srcOriginXInt << INT_FIXED16_SHIFT);
        const int32_t fixedInvTy = invTyFixed
                            - (dstOriginXInt * fixedInvC)
                            - (dstOriginYInt * fixedInvD)
                            + (srcOriginYInt << INT_FIXED16_SHIFT);

        // DDA オフセット（ピクセル中心補正）
        const int32_t rowOffsetX = fixedInvB >> 1;
        const int32_t rowOffsetY = fixedInvD >> 1;
        const int32_t dxOffsetX = fixedInvA >> 1;
        const int32_t dxOffsetY = fixedInvC >> 1;

        // バイト単位の stride
        const int32_t srcStride = src.stride;
        const uint8_t* srcData = static_cast<const uint8_t*>(src.data);

        // Y ループ（関数ポインタ呼び出し、分岐なし）
        for (int dy = 0; dy < outH; dy++) {
            int32_t rowBaseX = fixedInvB * dy + fixedInvTx + rowOffsetX;
            int32_t rowBaseY = fixedInvD * dy + fixedInvTy + rowOffsetY;

            auto [xStart, xEnd] = transform::calcValidRange(fixedInvA, rowBaseX, src.width, outW);
            auto [yStart, yEnd] = transform::calcValidRange(fixedInvC, rowBaseY, src.height, outW);
            int dxStart = std::max({0, xStart, yStart});
            int dxEnd = std::min({outW - 1, xEnd, yEnd});

            if (dxStart > dxEnd) continue;

            // 有効範囲を追跡
            if (dxStart < result.minX) result.minX = dxStart;
            if (dxEnd > result.maxX) result.maxX = dxEnd;
            if (dy < result.minY) result.minY = dy;
            if (dy > result.maxY) result.maxY = dy;

            int32_t srcX_fixed = fixedInvA * dxStart + rowBaseX + dxOffsetX;
            int32_t srcY_fixed = fixedInvC * dxStart + rowBaseY + dxOffsetY;
            int count = dxEnd - dxStart + 1;

            uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(dxStart, dy));

            copyRow(dstRow, srcData, srcStride,
                    srcX_fixed, srcY_fixed, fixedInvA, fixedInvC, count);
        }

        return result;
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_AFFINE_NODE_H
