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

        // 入力要求を計算
        RenderRequest inputReq = computeInputRequest(request);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        // ピクセル効率計測
        auto& metrics = PerfMetrics::instance().nodes[NodeType::Affine];
        metrics.requestedPixels += static_cast<uint64_t>(inputReq.width) * inputReq.height;
        metrics.usedPixels += static_cast<uint64_t>(request.width) * request.height;
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
    // 入力要求計算
    // ========================================
    //
    // 出力要求の4頂点を逆変換し、必要な入力領域のAABBを計算する。
    // 全ての座標を Q24.8 で計算し、最終的に floor/ceil で整数化。
    //
    virtual RenderRequest computeInputRequest(const RenderRequest& request) {
        // 出力要求の4頂点を Q24.8 で計算（小数部保持）
        int_fixed8 corners_x[4] = {
            -request.origin.x,
            to_fixed8(request.width) - request.origin.x,
            -request.origin.x,
            to_fixed8(request.width) - request.origin.x
        };
        int_fixed8 corners_y[4] = {
            -request.origin.y,
            -request.origin.y,
            to_fixed8(request.height) - request.origin.y,
            to_fixed8(request.height) - request.origin.y
        };

        // tx/ty を Q24.8 のまま減算（小数部保持）
        for (int i = 0; i < 4; i++) {
            corners_x[i] -= txFixed8_;
            corners_y[i] -= tyFixed8_;
        }

        // 逆変換して min/max を計算（Q24.8 精度）
        // 演算: (Q16.16 * Q24.8) >> 16 = Q24.8
        int_fixed8 minX_f8 = INT32_MAX, minY_f8 = INT32_MAX;
        int_fixed8 maxX_f8 = INT32_MIN, maxY_f8 = INT32_MIN;
        for (int i = 0; i < 4; i++) {
            int64_t sx64 = static_cast<int64_t>(invMatrix_.a) * corners_x[i]
                         + static_cast<int64_t>(invMatrix_.b) * corners_y[i];
            int64_t sy64 = static_cast<int64_t>(invMatrix_.c) * corners_x[i]
                         + static_cast<int64_t>(invMatrix_.d) * corners_y[i];
            int_fixed8 sx = static_cast<int_fixed8>(sx64 >> INT_FIXED16_SHIFT);
            int_fixed8 sy = static_cast<int_fixed8>(sy64 >> INT_FIXED16_SHIFT);
            minX_f8 = std::min(minX_f8, sx);
            minY_f8 = std::min(minY_f8, sy);
            maxX_f8 = std::max(maxX_f8, sx);
            maxY_f8 = std::max(maxY_f8, sy);
        }

        // floor/ceil で整数化（正確な境界）
        int minX = from_fixed8_floor(minX_f8);
        int minY = from_fixed8_floor(minY_f8);
        int maxX = from_fixed8_ceil(maxX_f8);
        int maxY = from_fixed8_ceil(maxY_f8);

        // マージン: +1（DDA 半ピクセルオフセット対策）
        int reqLeft = minX - 1;
        int reqTop = minY - 1;
        int inputWidth = maxX - minX + 3;   // +1(左) + +1(右) + 1(右端包含)
        int inputHeight = maxY - minY + 3;

        RenderRequest inputReq;
        inputReq.width = static_cast<int16_t>(inputWidth);
        inputReq.height = static_cast<int16_t>(inputHeight);
        inputReq.origin.x = to_fixed8(-reqLeft);
        inputReq.origin.y = to_fixed8(-reqTop);

        return inputReq;
    }

private:
    AffineMatrix matrix_;  // 恒等行列がデフォルト
    Matrix2x2_fixed16 invMatrix_;  // prepare() で計算（2x2逆行列）
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
