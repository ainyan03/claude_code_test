#ifndef FLEXIMG_SOURCE_NODE_H
#define FLEXIMG_SOURCE_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/viewport.h"
#include "../image/image_buffer.h"
#include "../operations/transform.h"
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

        // アフィン情報を受け取り、事前計算を行う
        if (request.hasAffine) {
            // 逆行列を計算
            invMatrix_ = inverseFixed16(request.affineMatrix);

            if (invMatrix_.valid) {
                // tx/ty を Q24.8 固定小数点に変換
                int_fixed8 txFixed8 = float_to_fixed8(request.affineMatrix.tx);
                int_fixed8 tyFixed8 = float_to_fixed8(request.affineMatrix.ty);

                // 逆変換オフセットの計算（tx/ty と逆行列から）
                int64_t invTx64 = -(static_cast<int64_t>(txFixed8) * invMatrix_.a
                                  + static_cast<int64_t>(tyFixed8) * invMatrix_.b);
                int64_t invTy64 = -(static_cast<int64_t>(txFixed8) * invMatrix_.c
                                  + static_cast<int64_t>(tyFixed8) * invMatrix_.d);
                int32_t invTxFixed = static_cast<int32_t>(invTx64 >> INT_FIXED8_SHIFT);
                int32_t invTyFixed = static_cast<int32_t>(invTy64 >> INT_FIXED8_SHIFT);

                // srcOrigin（自身のorigin）を加算して baseTx/Ty を計算
                const int32_t srcOriginXInt = from_fixed8(originX_);
                const int32_t srcOriginYInt = from_fixed8(originY_);
                baseTx_ = invTxFixed + (srcOriginXInt << INT_FIXED16_SHIFT);
                baseTy_ = invTyFixed + (srcOriginYInt << INT_FIXED16_SHIFT);

                // ピクセル中心オフセット
                rowOffsetX_ = invMatrix_.b >> 1;
                rowOffsetY_ = invMatrix_.d >> 1;
                dxOffsetX_ = invMatrix_.a >> 1;
                dxOffsetY_ = invMatrix_.c >> 1;
            }
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

    // アフィン伝播用メンバ変数（事前計算済み）
    Matrix2x2_fixed16 invMatrix_;  // 逆行列（2x2部分）
    int32_t baseTx_ = 0;           // 事前計算済みオフセットX（Q16.16）
    int32_t baseTy_ = 0;           // 事前計算済みオフセットY（Q16.16）
    int32_t rowOffsetX_ = 0;       // fixedInvB >> 1
    int32_t rowOffsetY_ = 0;       // fixedInvD >> 1
    int32_t dxOffsetX_ = 0;        // fixedInvA >> 1
    int32_t dxOffsetY_ = 0;        // fixedInvC >> 1
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

        // アフィン変換を適用（srcOrigin は事前計算済み）
        applyAffine(outputView, request.origin.x, request.origin.y, source_);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& m = PerfMetrics::instance().nodes[NodeType::Source];
        m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - sourceStart).count();
        m.count++;
#endif

        return RenderResult(std::move(output), request.origin);
    }

    // ========================================
    // アフィン変換実装（事前計算済み値を使用）
    // ========================================
    void applyAffine(ViewPort& dst, int_fixed8 dstOriginX, int_fixed8 dstOriginY,
                     const ViewPort& src) {
        if (!invMatrix_.valid) return;

        // dstOrigin分のみ計算（baseTx_/baseTy_ は srcOrigin 込みで事前計算済み）
        const int32_t dstOriginXInt = from_fixed8(dstOriginX);
        const int32_t dstOriginYInt = from_fixed8(dstOriginY);

        const int32_t fixedInvTx = baseTx_
                            - (dstOriginXInt * invMatrix_.a)
                            - (dstOriginYInt * invMatrix_.b);
        const int32_t fixedInvTy = baseTy_
                            - (dstOriginXInt * invMatrix_.c)
                            - (dstOriginYInt * invMatrix_.d);

        // 共通DDA処理を呼び出し
        transform::applyAffineDDA(dst, src, fixedInvTx, fixedInvTy,
                                  invMatrix_, rowOffsetX_, rowOffsetY_,
                                  dxOffsetX_, dxOffsetY_);
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_SOURCE_NODE_H
