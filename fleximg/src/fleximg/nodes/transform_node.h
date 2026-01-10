#ifndef FLEXIMG_TRANSFORM_NODE_H
#define FLEXIMG_TRANSFORM_NODE_H

#include "../node.h"
#include "../common.h"
#include "../image_buffer.h"
#include "../operations/transform.h"
#include "../perf_metrics.h"
#include <algorithm>
#include <cstdint>
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// TransformNode - アフィン変換ノード
// ========================================================================
//
// 入力画像に対してアフィン変換（回転・拡縮・平行移動）を適用します。
// - 入力: 1ポート
// - 出力: 1ポート
//
// 使用例:
//   TransformNode transform;
//   transform.setMatrix(AffineMatrix::rotation(0.5f));
//   src >> transform >> sink;
//

class TransformNode : public Node {
public:
    TransformNode() {
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

    const char* name() const override { return "TransformNode"; }

    // ========================================
    // プル型インターフェース
    // ========================================

    // 逆変換で入力要求を計算するため、pullProcess()を直接オーバーライド
    RenderResult pullProcess(const RenderRequest& request) override {
        Node* upstream = upstreamNode(0);
        if (!upstream) return RenderResult();

        // 固定小数点逆行列を事前計算
        auto invMatrix = transform::FixedPointInverseMatrix::fromMatrix(matrix_);
        if (!invMatrix.valid) {
            return RenderResult();  // 特異行列
        }

        // ================================================================
        // 入力要求範囲の算出
        // ================================================================
        // 出力要求の4頂点を逆変換し、必要な入力領域のAABBを計算する。

        // 出力要求の4頂点（基準相対座標 = -origin、整数に変換）
        int32_t ox = from_fixed8(request.origin.x);
        int32_t oy = from_fixed8(request.origin.y);
        int32_t corners[4][2] = {
            {-ox, -oy},
            {request.width - ox, -oy},
            {-ox, request.height - oy},
            {request.width - ox, request.height - oy}
        };

        int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
        for (int i = 0; i < 4; i++) {
            // 平行移動をキャンセル（整数演算）
            int32_t rx = corners[i][0] - invMatrix.tx;
            int32_t ry = corners[i][1] - invMatrix.ty;
            // 回転/スケール逆変換（固定小数点演算）
            int64_t sx64 = static_cast<int64_t>(invMatrix.a) * rx
                         + static_cast<int64_t>(invMatrix.b) * ry;
            int64_t sy64 = static_cast<int64_t>(invMatrix.c) * rx
                         + static_cast<int64_t>(invMatrix.d) * ry;
            int32_t sx = static_cast<int32_t>(sx64 >> transform::FIXED_POINT_BITS);
            int32_t sy = static_cast<int32_t>(sy64 >> transform::FIXED_POINT_BITS);
            minX = std::min(minX, sx);
            minY = std::min(minY, sy);
            maxX = std::max(maxX, sx);
            maxY = std::max(maxY, sy);
        }

        // マージンを追加（固定小数点の丸め誤差対策）
        int reqLeft = minX - 1;
        int reqTop = minY - 1;
        int inputWidth = maxX - minX + 3;
        int inputHeight = maxY - minY + 3;

        RenderRequest inputReq;
        inputReq.width = static_cast<int16_t>(inputWidth);
        inputReq.height = static_cast<int16_t>(inputHeight);
        inputReq.origin.x = to_fixed8(-reqLeft);
        inputReq.origin.y = to_fixed8(-reqTop);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        // ピクセル効率計測
        auto& mTrans = PerfMetrics::instance().nodes[NodeType::Transform];
        mTrans.requestedPixels += static_cast<uint64_t>(inputReq.width) * inputReq.height;
        mTrans.usedPixels += static_cast<uint64_t>(request.width) * request.height;
#endif

        // 上流を評価（新APIを使用）
        RenderResult inputResult = upstream->pullProcess(inputReq);
        if (!inputResult.isValid()) {
            return RenderResult(ImageBuffer(), request.origin);
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto transformStart = std::chrono::high_resolution_clock::now();
#endif

        // 出力バッファを作成（ゼロ初期化済み）
        ImageBuffer output(request.width, request.height, inputResult.buffer.formatID());
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::Transform].recordAlloc(
            output.totalBytes(), output.width(), output.height());
#endif
        ViewPort outputView = output.view();

        // アフィン変換を適用
        ViewPort inputView = inputResult.view();
        transform::affine(outputView, request.origin.x, request.origin.y,
                          inputView,
                          inputResult.origin.x, inputResult.origin.y,
                          invMatrix);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& mTransEnd = PerfMetrics::instance().nodes[NodeType::Transform];
        mTransEnd.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - transformStart).count();
        mTransEnd.count++;
#endif

        return RenderResult(std::move(output), request.origin);
    }

private:
    AffineMatrix matrix_;  // 恒等行列がデフォルト
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_TRANSFORM_NODE_H
