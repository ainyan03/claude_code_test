#ifndef FLEXIMG_SINK_NODE_H
#define FLEXIMG_SINK_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/viewport.h"
#include "../operations/transform.h"
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// SinkNode - 画像出力ノード（終端）
// ========================================================================
//
// パイプラインの出力端点となるノードです。
// - 入力ポート: 1
// - 出力ポート: 0
// - 外部のViewPortに結果を書き込む
//

class SinkNode : public Node {
public:
    // コンストラクタ
    SinkNode() {
        initPorts(1, 0);  // 入力1、出力0
    }

    SinkNode(const ViewPort& vp, int_fixed originX = 0, int_fixed originY = 0)
        : target_(vp), originX_(originX), originY_(originY) {
        initPorts(1, 0);
    }

    // ターゲット設定
    void setTarget(const ViewPort& vp) { target_ = vp; }
    void setOrigin(int_fixed x, int_fixed y) { originX_ = x; originY_ = y; }

    // アクセサ
    const ViewPort& target() const { return target_; }
    ViewPort& target() { return target_; }
    int_fixed originX() const { return originX_; }
    int_fixed originY() const { return originY_; }

    // キャンバスサイズ（targetから取得）
    int16_t canvasWidth() const { return target_.width; }
    int16_t canvasHeight() const { return target_.height; }

    const char* name() const override { return "SinkNode"; }

protected:
    int nodeTypeForMetrics() const override { return NodeType::Sink; }

protected:
    // ========================================
    // Template Method フック
    // ========================================

    // onPushPrepare: アフィン情報を受け取り、事前計算を行う
    // SinkNodeは終端なので下流への伝播なし
    bool onPushPrepare(const PrepareRequest& request) override {
        // アフィン情報を受け取り、事前計算を行う
        if (request.hasPushAffine) {
            // 逆行列とピクセル中心オフセットを計算（共通処理）
            affine_ = precomputeInverseAffine(request.pushAffineMatrix);

            if (affine_.isValid()) {
                // dstOrigin（自身のorigin）を減算して baseTx/Ty を計算
                const int32_t dstOriginXInt = from_fixed(originX_);
                const int32_t dstOriginYInt = from_fixed(originY_);
                baseTx_ = affine_.invTxFixed
                        - (dstOriginXInt * affine_.invMatrix.a)
                        - (dstOriginYInt * affine_.invMatrix.b);
                baseTy_ = affine_.invTyFixed
                        - (dstOriginXInt * affine_.invMatrix.c)
                        - (dstOriginYInt * affine_.invMatrix.d);
            }
            hasAffine_ = true;
        } else {
            hasAffine_ = false;
        }

        // SinkNodeは終端なので下流への伝播なし（return trueのみ）
        return true;
    }

    // onPushProcess: タイル単位で呼び出され、出力バッファに書き込み
    // SinkNodeは終端なので下流への伝播なし
    void onPushProcess(RenderResult&& input,
                       const RenderRequest& request) override {
        (void)request;  // 現在は未使用

        if (!input.isValid() || !target_.isValid()) return;

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
#endif

        // アフィン変換が伝播されている場合はDDA処理
        if (hasAffine_) {
            pushProcessWithAffine(std::move(input));
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            auto& metrics = PerfMetrics::instance().nodes[NodeType::Sink];
            metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start).count();
            metrics.count++;
#endif
            return;
        }

        ViewPort inputView = input.view();

        // 基準点一致ルールに基づく配置計算（固定小数点演算）
        // input.origin: 入力バッファ内での基準点位置
        // originX_/Y_: 出力バッファ内での基準点位置
        // dstX = originX_ - input.origin.x
        int dstX = from_fixed(originX_ - input.origin.x);
        int dstY = from_fixed(originY_ - input.origin.y);

        // クリッピング処理
        int srcX = 0, srcY = 0;
        if (dstX < 0) { srcX = -dstX; dstX = 0; }
        if (dstY < 0) { srcY = -dstY; dstY = 0; }

        int_fast32_t copyW = std::min<int_fast32_t>(inputView.width - srcX, target_.width - dstX);
        int_fast32_t copyH = std::min<int_fast32_t>(inputView.height - srcY, target_.height - dstY);

        if (copyW > 0 && copyH > 0) {
            view_ops::copy(target_, dstX, dstY, inputView, srcX, srcY,
                          static_cast<int>(copyW), static_cast<int>(copyH));
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::Sink];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif
    }

private:
    ViewPort target_;
    int_fixed originX_ = 0;  // 出力先の基準点X（固定小数点 Q16.16）
    int_fixed originY_ = 0;  // 出力先の基準点Y（固定小数点 Q16.16）

    // アフィン伝播用メンバ変数（事前計算済み）
    AffinePrecomputed affine_;     // 逆行列・ピクセル中心オフセット
    int32_t baseTx_ = 0;           // 事前計算済みオフセットX（Q16.16、dstOrigin込み）
    int32_t baseTy_ = 0;           // 事前計算済みオフセットY（Q16.16、dstOrigin込み）
    bool hasAffine_ = false;       // アフィン変換が伝播されているか

    // ========================================
    // アフィン変換付きプッシュ処理
    // ========================================
    void pushProcessWithAffine(RenderResult&& input) {
        // 特異行列チェック
        if (!affine_.isValid()) {
            return;
        }

        // ターゲットフォーマットに変換（フォーマットが異なる場合のみ）
        PixelFormatID targetFormat = target_.formatID;
        ImageBuffer convertedBuffer;
        ViewPort inputView;

        if (input.buffer.formatID() != targetFormat) {
            convertedBuffer = std::move(input.buffer).toFormat(targetFormat);
            inputView = convertedBuffer.view();
        } else {
            inputView = input.view();
        }

        // アフィン変換を適用してターゲットに書き込み（dstOrigin は事前計算済み）
        applyAffine(target_, inputView, input.origin.x, input.origin.y);
    }

    // ========================================
    // アフィン変換実装（事前計算済み値を使用）
    // ========================================
    void applyAffine(ViewPort& dst,
                     const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY) {
        if (!affine_.isValid()) return;

        // srcOrigin分のみ計算（baseTx_/baseTy_ は dstOrigin 込みで事前計算済み）
        const int32_t srcOriginXInt = from_fixed(srcOriginX);
        const int32_t srcOriginYInt = from_fixed(srcOriginY);

        const int32_t fixedInvTx = baseTx_
                            + (srcOriginXInt << INT_FIXED16_SHIFT);
        const int32_t fixedInvTy = baseTy_
                            + (srcOriginYInt << INT_FIXED16_SHIFT);

        // 共通DDA処理を呼び出し
        transform::applyAffineDDA(dst, src, fixedInvTx, fixedInvTy,
                                  affine_.invMatrix, affine_.rowOffsetX, affine_.rowOffsetY,
                                  affine_.dxOffsetX, affine_.dxOffsetY);
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_SINK_NODE_H
