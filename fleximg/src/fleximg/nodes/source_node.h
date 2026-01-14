#ifndef FLEXIMG_SOURCE_NODE_H
#define FLEXIMG_SOURCE_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/viewport.h"
#include "../image/image_buffer.h"
#include "../operations/transform.h"
#include <algorithm>
#include <cstring>
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// SourceNode - 画像入力ノード（終端）
// ========================================================================
//
// パイプラインの入力端点となるノードです。
// - 入力ポート: 0
// - 出力ポート: 1
// - 外部のViewPortを参照
//

class SourceNode : public Node {
public:
    // コンストラクタ
    SourceNode() {
        initPorts(0, 1);  // 入力0、出力1
    }

    SourceNode(const ViewPort& vp, int_fixed8 originX = 0, int_fixed8 originY = 0)
        : source_(vp), originX_(originX), originY_(originY) {
        initPorts(0, 1);
    }

    // ソース設定
    void setSource(const ViewPort& vp) { source_ = vp; }
    void setOrigin(int_fixed8 x, int_fixed8 y) { originX_ = x; originY_ = y; }

    // アクセサ
    const ViewPort& source() const { return source_; }
    int_fixed8 originX() const { return originX_; }
    int_fixed8 originY() const { return originY_; }

    const char* name() const override { return "SourceNode"; }

    // ========================================
    // PrepareRequest対応（循環検出+アフィン伝播）
    // ========================================

    bool pullPrepare(const PrepareRequest& request) override {
        // 循環参照検出: Preparing状態で再訪問 = 循環
        if (pullPrepareState_ == PrepareState::Preparing) {
            pullPrepareState_ = PrepareState::CycleError;
            return false;
        }
        // DAG共有ノード: スキップ
        if (pullPrepareState_ == PrepareState::Prepared) {
            return true;
        }
        // 既にエラー状態
        if (pullPrepareState_ == PrepareState::CycleError) {
            return false;
        }

        pullPrepareState_ = PrepareState::Preparing;

        // アフィン情報を受け取る
        if (request.hasAffine) {
            // 逆行列を事前計算
            invMatrix_ = inverseFixed16(request.affineMatrix);
            // tx/ty を Q24.8 固定小数点で保持
            txFixed8_ = float_to_fixed8(request.affineMatrix.tx);
            tyFixed8_ = float_to_fixed8(request.affineMatrix.ty);
            hasAffine_ = true;
        } else {
            hasAffine_ = false;
        }

        // SourceNodeは終端なので上流への伝播なし
        pullPrepareState_ = PrepareState::Prepared;
        return true;
    }

    // ========================================
    // プル型インターフェース
    // ========================================

    // SourceNodeは入力がないため、pullProcess()を直接オーバーライド
    RenderResult pullProcess(const RenderRequest& request) override {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto sourceStart = std::chrono::high_resolution_clock::now();
#endif

        if (!source_.isValid()) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            auto& m = PerfMetrics::instance().nodes[NodeType::Source];
            m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - sourceStart).count();
            m.count++;
#endif
            return RenderResult();
        }

        // アフィン変換が伝播されている場合はDDA処理
        if (hasAffine_) {
            return pullProcessWithAffine(request);
        }

        // ソース画像の基準相対座標範囲（固定小数点 Q24.8）
        int_fixed8 imgLeft = -originX_;
        int_fixed8 imgTop = -originY_;
        int_fixed8 imgRight = imgLeft + to_fixed8(source_.width);
        int_fixed8 imgBottom = imgTop + to_fixed8(source_.height);

        // 要求範囲の基準相対座標（固定小数点 Q24.8）
        int_fixed8 reqLeft = -request.origin.x;
        int_fixed8 reqTop = -request.origin.y;
        int_fixed8 reqRight = reqLeft + to_fixed8(request.width);
        int_fixed8 reqBottom = reqTop + to_fixed8(request.height);

        // 交差領域
        int_fixed8 interLeft = std::max(imgLeft, reqLeft);
        int_fixed8 interTop = std::max(imgTop, reqTop);
        int_fixed8 interRight = std::min(imgRight, reqRight);
        int_fixed8 interBottom = std::min(imgBottom, reqBottom);

        if (interLeft >= interRight || interTop >= interBottom) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            auto& m = PerfMetrics::instance().nodes[NodeType::Source];
            m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - sourceStart).count();
            m.count++;
#endif
            // バッファ内基準点位置 = request.origin
            return RenderResult(ImageBuffer(), request.origin);
        }

        // 交差領域のサブビューを参照モードで返す（コピーなし）
        // 開始位置は floor、終端位置は ceil で計算（タイル境界でのピクセル欠落を防ぐ）
        int srcX = from_fixed8_floor(interLeft - imgLeft);
        int srcY = from_fixed8_floor(interTop - imgTop);
        int srcEndX = from_fixed8_ceil(interRight - imgLeft);
        int srcEndY = from_fixed8_ceil(interBottom - imgTop);
        int interW = srcEndX - srcX;
        int interH = srcEndY - srcY;

        // サブビューの参照モードImageBufferを作成（メモリ確保なし）
        ImageBuffer result(view_ops::subView(source_, srcX, srcY, interW, interH));

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& m = PerfMetrics::instance().nodes[NodeType::Source];
        m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - sourceStart).count();
        m.count++;
#endif
        // バッファ内基準点位置 = -interLeft, -interTop
        return RenderResult(std::move(result), Point{-interLeft, -interTop});
    }

private:
    ViewPort source_;
    int_fixed8 originX_ = 0;  // 画像内の基準点X（固定小数点 Q24.8）
    int_fixed8 originY_ = 0;  // 画像内の基準点Y（固定小数点 Q24.8）

    // アフィン伝播用メンバ変数
    Matrix2x2_fixed16 invMatrix_;  // 逆行列（2x2部分）
    int_fixed8 txFixed8_ = 0;      // tx を Q24.8 で保持
    int_fixed8 tyFixed8_ = 0;      // ty を Q24.8 で保持
    bool hasAffine_ = false;       // アフィン変換が伝播されているか

    // ========================================
    // アフィン変換付きプル処理
    // ========================================
    RenderResult pullProcessWithAffine(const RenderRequest& request) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto sourceStart = std::chrono::high_resolution_clock::now();
#endif

        // 特異行列チェック
        if (!invMatrix_.valid) {
            return RenderResult(ImageBuffer(), request.origin);
        }

        // 出力バッファを作成
        ImageBuffer output(request.width, request.height, source_.formatID);
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::Source].recordAlloc(
            output.totalBytes(), output.width(), output.height());
#endif
        ViewPort outputView = output.view();

        // アフィン変換を適用
        applyAffine(outputView, request.origin.x, request.origin.y,
                    source_, originX_, originY_);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& m = PerfMetrics::instance().nodes[NodeType::Source];
        m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - sourceStart).count();
        m.count++;
#endif

        return RenderResult(std::move(output), request.origin);
    }

    // ========================================
    // DDA転写テンプレート
    // ========================================
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

            const uint8_t* srcPixel = srcData + sy * srcStride + sx * BytesPerPixel;

            if constexpr (BytesPerPixel == 8) {
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

    // ========================================
    // アフィン変換実装
    // ========================================
    void applyAffine(ViewPort& dst, int_fixed8 dstOriginX, int_fixed8 dstOriginY,
                     const ViewPort& src, int_fixed8 srcOriginX, int_fixed8 srcOriginY) {
        if (!dst.isValid() || !src.isValid()) return;
        if (!invMatrix_.valid) return;

        const int outW = dst.width;
        const int outH = dst.height;

        // BytesPerPixel に応じて関数ポインタを選択
        CopyRowFunc copyRow = nullptr;
        switch (getBytesPerPixel(src.formatID)) {
            case 8: copyRow = &copyRowDDA<8>; break;
            case 4: copyRow = &copyRowDDA<4>; break;
            case 3: copyRow = &copyRowDDA<3>; break;
            case 2: copyRow = &copyRowDDA<2>; break;
            case 1: copyRow = &copyRowDDA<1>; break;
            default: return;
        }

        const int32_t fixedInvA = invMatrix_.a;
        const int32_t fixedInvB = invMatrix_.b;
        const int32_t fixedInvC = invMatrix_.c;
        const int32_t fixedInvD = invMatrix_.d;

        const int32_t dstOriginXInt = from_fixed8(dstOriginX);
        const int32_t dstOriginYInt = from_fixed8(dstOriginY);
        const int32_t srcOriginXInt = from_fixed8(srcOriginX);
        const int32_t srcOriginYInt = from_fixed8(srcOriginY);

        // 逆変換オフセットの計算
        int64_t invTx64 = -(static_cast<int64_t>(txFixed8_) * fixedInvA
                          + static_cast<int64_t>(tyFixed8_) * fixedInvB);
        int64_t invTy64 = -(static_cast<int64_t>(txFixed8_) * fixedInvC
                          + static_cast<int64_t>(tyFixed8_) * fixedInvD);
        int32_t invTxFixed = static_cast<int32_t>(invTx64 >> INT_FIXED8_SHIFT);
        int32_t invTyFixed = static_cast<int32_t>(invTy64 >> INT_FIXED8_SHIFT);

        const int32_t fixedInvTx = invTxFixed
                            - (dstOriginXInt * fixedInvA)
                            - (dstOriginYInt * fixedInvB)
                            + (srcOriginXInt << INT_FIXED16_SHIFT);
        const int32_t fixedInvTy = invTyFixed
                            - (dstOriginXInt * fixedInvC)
                            - (dstOriginYInt * fixedInvD)
                            + (srcOriginYInt << INT_FIXED16_SHIFT);

        const int32_t rowOffsetX = fixedInvB >> 1;
        const int32_t rowOffsetY = fixedInvD >> 1;
        const int32_t dxOffsetX = fixedInvA >> 1;
        const int32_t dxOffsetY = fixedInvC >> 1;

        const int32_t srcStride = src.stride;
        const uint8_t* srcData = static_cast<const uint8_t*>(src.data);

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
            int count = dxEnd - dxStart + 1;

            uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(dxStart, dy));

            copyRow(dstRow, srcData, srcStride,
                    srcX_fixed, srcY_fixed, fixedInvA, fixedInvC, count);
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_SOURCE_NODE_H
