#ifndef FLEXIMG_AFFINE_NODE_H
#define FLEXIMG_AFFINE_NODE_H

#include "../node.h"
#include "../common.h"
#include "../image_buffer.h"
#include "../operations/transform.h"
#include "../perf_metrics.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
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
// - tx/ty を Q24.8 固定小数点で保持し、サブピクセル精度の平行移動に対応
// - 回転・拡縮時に tx/ty の小数成分が DDA に正しく反映される
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

struct InputRegion {
    // 4頂点座標（入力空間、Q24.8）
    // corners[0]: 左上, corners[1]: 右上, corners[2]: 左下, corners[3]: 右下
    int_fixed8 corners_x[4];
    int_fixed8 corners_y[4];

    // AABB（入力空間、整数）
    int aabbLeft, aabbTop, aabbRight, aabbBottom;

    // 面積情報
    int64_t aabbPixels;          // AABB のピクセル数
    int64_t parallelogramPixels; // 平行四辺形の面積（理論最小値）
    int64_t outputPixels;        // 出力タイルのピクセル数

    // 効率（0.0〜1.0）
    float currentEfficiency() const {
        return aabbPixels > 0 ? static_cast<float>(parallelogramPixels) / aabbPixels : 1.0f;
    }
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

    // 便利なセッター
    void setRotation(float radians) {
        float c = std::cos(radians);
        float s = std::sin(radians);
        matrix_.a = c;  matrix_.b = -s;
        matrix_.c = s;  matrix_.d = c;
        matrix_.tx = 0; matrix_.ty = 0;
    }

    void setScale(float sx, float sy) {
        matrix_.a = sx; matrix_.b = 0;
        matrix_.c = 0;  matrix_.d = sy;
        matrix_.tx = 0; matrix_.ty = 0;
    }

    void setTranslation(float tx, float ty) {
        matrix_.a = 1;  matrix_.b = 0;
        matrix_.c = 0;  matrix_.d = 1;
        matrix_.tx = tx; matrix_.ty = ty;
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

        // tx/ty を Q24.8 固定小数点で保持（サブピクセル精度）
        txFixed8_ = float_to_fixed8(matrix_.tx);
        tyFixed8_ = float_to_fixed8(matrix_.ty);
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

        // 特異行列チェック（prepare で計算済み）
        if (!invMatrix_.valid) {
            return RenderResult();
        }

        // 入力領域を計算（4頂点座標、AABB、面積情報を含む）
        InputRegion region = computeInputRegion(request);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        // ピクセル効率計測
        auto& metrics = PerfMetrics::instance().nodes[NodeType::Affine];
        metrics.usedPixels += region.outputPixels;
        // 分割時の理論最小値（平行四辺形面積 × 2、三角形領域の効率50%を考慮）
        metrics.theoreticalMinPixels += region.parallelogramPixels * 2;
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
        PerfMetrics::instance().nodes[NodeType::Affine].requestedPixels += region.aabbPixels;
#endif

        // 上流を評価
        RenderResult input = upstream->pullProcess(inputReq);
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
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
#endif

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
            int64_t splitPixels = static_cast<int64_t>(subReq.width) * subReq.height;
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
            applyAffine(outputView, request.origin.x, request.origin.y,
                        subInput.view(), subInput.origin.x, subInput.origin.y);
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::Affine];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(output), request.origin);
    }

public:

    // ========================================
    // 変換処理（process() オーバーライド）
    // ========================================
    //
    // transform::affine() を呼ばず、直接 DDA ループを実装。
    // tx/ty を Q24.8 固定小数点で扱い、サブピクセル精度を実現。
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
        applyAffine(outputView, request.origin.x, request.origin.y,
                    inputView, input.origin.x, input.origin.y);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::Affine];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(output), request.origin);
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
                                / (region.parallelogramPixels * 2);
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

        // 出力要求の4頂点を Q24.8 で計算（小数部保持）
        int_fixed8 out_x[4] = {
            -request.origin.x,
            to_fixed8(request.width) - request.origin.x,
            -request.origin.x,
            to_fixed8(request.width) - request.origin.x
        };
        int_fixed8 out_y[4] = {
            -request.origin.y,
            -request.origin.y,
            to_fixed8(request.height) - request.origin.y,
            to_fixed8(request.height) - request.origin.y
        };

        // tx/ty を Q24.8 のまま減算（小数部保持）
        for (int i = 0; i < 4; i++) {
            out_x[i] -= txFixed8_;
            out_y[i] -= tyFixed8_;
        }

        // 逆変換して入力空間の4頂点を計算（Q24.8 精度）
        // 演算: (Q16.16 * Q24.8) >> 16 = Q24.8
        int_fixed8 minX_f8 = INT32_MAX, minY_f8 = INT32_MAX;
        int_fixed8 maxX_f8 = INT32_MIN, maxY_f8 = INT32_MIN;
        for (int i = 0; i < 4; i++) {
            int64_t sx64 = static_cast<int64_t>(invMatrix_.a) * out_x[i]
                         + static_cast<int64_t>(invMatrix_.b) * out_y[i];
            int64_t sy64 = static_cast<int64_t>(invMatrix_.c) * out_x[i]
                         + static_cast<int64_t>(invMatrix_.d) * out_y[i];
            region.corners_x[i] = static_cast<int_fixed8>(sx64 >> INT_FIXED16_SHIFT);
            region.corners_y[i] = static_cast<int_fixed8>(sy64 >> INT_FIXED16_SHIFT);
            minX_f8 = std::min(minX_f8, region.corners_x[i]);
            minY_f8 = std::min(minY_f8, region.corners_y[i]);
            maxX_f8 = std::max(maxX_f8, region.corners_x[i]);
            maxY_f8 = std::max(maxY_f8, region.corners_y[i]);
        }

        // floor/ceil で整数化（正確な境界）
        int minX = from_fixed8_floor(minX_f8);
        int minY = from_fixed8_floor(minY_f8);
        int maxX = from_fixed8_ceil(maxX_f8);
        int maxY = from_fixed8_ceil(maxY_f8);

        // マージン: +1（DDA 半ピクセルオフセット対策）
        region.aabbLeft = minX - 1;
        region.aabbTop = minY - 1;
        region.aabbRight = maxX + 1;
        region.aabbBottom = maxY + 1;
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
    int_fixed8 txFixed8_ = 0;  // tx を Q24.8 で保持
    int_fixed8 tyFixed8_ = 0;  // ty を Q24.8 で保持

    // ========================================
    // アフィン変換実装（tx/ty サブピクセル精度版）
    // ========================================
    //
    // transform::affine() との違い:
    // - tx/ty を Q24.8 固定小数点で扱う
    // - 平行移動の逆変換計算時に小数部を反映
    //
    void applyAffine(ViewPort& dst, int_fixed8 dstOriginX, int_fixed8 dstOriginY,
                     const ViewPort& src, int_fixed8 srcOriginX, int_fixed8 srcOriginY) {
        if (!dst.isValid() || !src.isValid()) return;
        if (!invMatrix_.valid) return;

        int outW = dst.width;
        int outH = dst.height;

        // 固定小数点逆行列の回転/スケール成分
        int32_t fixedInvA = invMatrix_.a;
        int32_t fixedInvB = invMatrix_.b;
        int32_t fixedInvC = invMatrix_.c;
        int32_t fixedInvD = invMatrix_.d;

        // 原点座標を整数化（固定小数点から変換）
        int32_t dstOriginXInt = from_fixed8(dstOriginX);
        int32_t dstOriginYInt = from_fixed8(dstOriginY);
        int32_t srcOriginXInt = from_fixed8(srcOriginX);
        int32_t srcOriginYInt = from_fixed8(srcOriginY);

        // ================================================================
        // 逆変換オフセットの計算（tx/ty 固定小数点版）
        // ================================================================
        //
        // 逆変換の数式: srcPos = R^(-1) * dstPos + invT
        //   invT = -R^(-1) * T = -(invA*tx + invB*ty, invC*tx + invD*ty)
        //
        // tx/ty は Q(32-S8).S8、invA 等は Q(32-S16).S16
        // → 積は Q(64-S8-S16).(S8+S16)、これを Q(32-S16).S16 に変換するため >> S8
        //

        // 平行移動の逆変換
        int64_t invTx64 = -(static_cast<int64_t>(txFixed8_) * fixedInvA
                          + static_cast<int64_t>(tyFixed8_) * fixedInvB);
        int64_t invTy64 = -(static_cast<int64_t>(txFixed8_) * fixedInvC
                          + static_cast<int64_t>(tyFixed8_) * fixedInvD);
        int32_t invTxFixed = static_cast<int32_t>(invTx64 >> INT_FIXED8_SHIFT);
        int32_t invTyFixed = static_cast<int32_t>(invTy64 >> INT_FIXED8_SHIFT);

        // DDA用オフセット: 逆変換 + 整数キャンセル + srcOrigin
        int32_t fixedInvTx = invTxFixed
                            - (dstOriginXInt * fixedInvA)
                            - (dstOriginYInt * fixedInvB)
                            + (srcOriginXInt << INT_FIXED16_SHIFT);
        int32_t fixedInvTy = invTyFixed
                            - (dstOriginXInt * fixedInvC)
                            - (dstOriginYInt * fixedInvD)
                            + (srcOriginYInt << INT_FIXED16_SHIFT);

        // ピクセルスキャン（DDAアルゴリズム）
        size_t srcBpp = getBytesPerPixel(src.formatID);
        const int inputStride16 = src.stride / sizeof(uint16_t);
        const int32_t rowOffsetX = fixedInvB >> 1;
        const int32_t rowOffsetY = fixedInvD >> 1;
        const int32_t dxOffsetX = fixedInvA >> 1;
        const int32_t dxOffsetY = fixedInvC >> 1;

        // 16bit RGBA用
        if (srcBpp == 8) {
            for (int dy = 0; dy < outH; dy++) {
                int32_t rowBaseX = fixedInvB * dy + fixedInvTx + rowOffsetX;
                int32_t rowBaseY = fixedInvD * dy + fixedInvTy + rowOffsetY;

                auto [xStart, xEnd] = transform::calcValidRange(fixedInvA, rowBaseX, src.width, outW);
                auto [yStart, yEnd] = transform::calcValidRange(fixedInvC, rowBaseY, src.height, outW);
                int dxStart = std::max({0, xStart, yStart});
                int dxEnd = std::min({outW - 1, xEnd, yEnd});

                if (dxStart > dxEnd) continue;

                int32_t srcX_fixed = fixedInvA * dxStart + rowBaseX + dxOffsetX;
                int32_t srcY_fixed = fixedInvC * dxStart + rowBaseY + dxOffsetY;

                uint16_t* dstRow = static_cast<uint16_t*>(dst.pixelAt(dxStart, dy));
                const uint16_t* srcData = static_cast<const uint16_t*>(src.data);

                for (int dx = dxStart; dx <= dxEnd; dx++) {
                    uint32_t sx = static_cast<uint32_t>(srcX_fixed) >> INT_FIXED16_SHIFT;
                    uint32_t sy = static_cast<uint32_t>(srcY_fixed) >> INT_FIXED16_SHIFT;

#ifdef FLEXIMG_DEBUG
                    // calcValidRange が正しければ範囲内のはず
                    assert(sx < static_cast<uint32_t>(src.width) && "calcValidRange mismatch: sx out of range");
                    assert(sy < static_cast<uint32_t>(src.height) && "calcValidRange mismatch: sy out of range");
#endif
                    const uint16_t* srcPixel = srcData + sy * inputStride16 + sx * 4;
                    dstRow[0] = srcPixel[0];
                    dstRow[1] = srcPixel[1];
                    dstRow[2] = srcPixel[2];
                    dstRow[3] = srcPixel[3];

                    dstRow += 4;
                    srcX_fixed += fixedInvA;
                    srcY_fixed += fixedInvC;
                }
            }
        }
        // 8bit RGBA用
        else if (srcBpp == 4) {
            for (int dy = 0; dy < outH; dy++) {
                int32_t rowBaseX = fixedInvB * dy + fixedInvTx + rowOffsetX;
                int32_t rowBaseY = fixedInvD * dy + fixedInvTy + rowOffsetY;

                auto [xStart, xEnd] = transform::calcValidRange(fixedInvA, rowBaseX, src.width, outW);
                auto [yStart, yEnd] = transform::calcValidRange(fixedInvC, rowBaseY, src.height, outW);
                int dxStart = std::max({0, xStart, yStart});
                int dxEnd = std::min({outW - 1, xEnd, yEnd});

                if (dxStart > dxEnd) continue;

                int32_t srcX_fixed = fixedInvA * dxStart + rowBaseX + dxOffsetX;
                int32_t srcY_fixed = fixedInvC * dxStart + rowBaseY + dxOffsetY;

                uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(dxStart, dy));
                const uint8_t* srcData = static_cast<const uint8_t*>(src.data);
                const int stride8 = src.stride;

                for (int dx = dxStart; dx <= dxEnd; dx++) {
                    uint32_t sx = static_cast<uint32_t>(srcX_fixed) >> INT_FIXED16_SHIFT;
                    uint32_t sy = static_cast<uint32_t>(srcY_fixed) >> INT_FIXED16_SHIFT;

#ifdef FLEXIMG_DEBUG
                    // calcValidRange が正しければ範囲内のはず
                    assert(sx < static_cast<uint32_t>(src.width) && "calcValidRange mismatch: sx out of range");
                    assert(sy < static_cast<uint32_t>(src.height) && "calcValidRange mismatch: sy out of range");
#endif
                    const uint8_t* srcPixel = srcData + sy * stride8 + sx * 4;
                    dstRow[0] = srcPixel[0];
                    dstRow[1] = srcPixel[1];
                    dstRow[2] = srcPixel[2];
                    dstRow[3] = srcPixel[3];

                    dstRow += 4;
                    srcX_fixed += fixedInvA;
                    srcY_fixed += fixedInvC;
                }
            }
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_AFFINE_NODE_H
