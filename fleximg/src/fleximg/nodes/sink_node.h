#ifndef FLEXIMG_SINK_NODE_H
#define FLEXIMG_SINK_NODE_H

#include "../core/node.h"
#include "../image/viewport.h"
#include "../operations/transform.h"

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

    SinkNode(const ViewPort& vp, int_fixed8 originX = 0, int_fixed8 originY = 0)
        : target_(vp), originX_(originX), originY_(originY) {
        initPorts(1, 0);
    }

    // ターゲット設定
    void setTarget(const ViewPort& vp) { target_ = vp; }
    void setOrigin(int_fixed8 x, int_fixed8 y) { originX_ = x; originY_ = y; }

    // アクセサ
    const ViewPort& target() const { return target_; }
    ViewPort& target() { return target_; }
    int_fixed8 originX() const { return originX_; }
    int_fixed8 originY() const { return originY_; }

    // キャンバスサイズ（targetから取得）
    int16_t canvasWidth() const { return target_.width; }
    int16_t canvasHeight() const { return target_.height; }

    const char* name() const override { return "SinkNode"; }

    // ========================================
    // プッシュ型準備（アフィン情報受け取り）
    // ========================================

    bool pushPrepare(const PrepareRequest& request) override {
        // 循環参照検出
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

        // アフィン情報を受け取り、事前計算を行う
        if (request.hasPushAffine) {
            // 逆行列を計算
            invMatrix_ = inverseFixed16(request.pushAffineMatrix);

            if (invMatrix_.valid) {
                // tx/ty を Q24.8 固定小数点に変換
                int_fixed8 txFixed8 = float_to_fixed8(request.pushAffineMatrix.tx);
                int_fixed8 tyFixed8 = float_to_fixed8(request.pushAffineMatrix.ty);

                // 逆変換オフセットの計算（tx/ty と逆行列から）
                int64_t invTx64 = -(static_cast<int64_t>(txFixed8) * invMatrix_.a
                                  + static_cast<int64_t>(tyFixed8) * invMatrix_.b);
                int64_t invTy64 = -(static_cast<int64_t>(txFixed8) * invMatrix_.c
                                  + static_cast<int64_t>(tyFixed8) * invMatrix_.d);
                int32_t invTxFixed = static_cast<int32_t>(invTx64 >> INT_FIXED8_SHIFT);
                int32_t invTyFixed = static_cast<int32_t>(invTy64 >> INT_FIXED8_SHIFT);

                // dstOrigin（自身のorigin）を減算して baseTx/Ty を計算
                const int32_t dstOriginXInt = from_fixed8(originX_);
                const int32_t dstOriginYInt = from_fixed8(originY_);
                baseTx_ = invTxFixed
                        - (dstOriginXInt * invMatrix_.a)
                        - (dstOriginYInt * invMatrix_.b);
                baseTy_ = invTyFixed
                        - (dstOriginXInt * invMatrix_.c)
                        - (dstOriginYInt * invMatrix_.d);

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

        // SinkNodeは終端なので下流への伝播なし
        pushPrepareState_ = PrepareState::Prepared;
        return true;
    }

    // ========================================
    // プッシュ型インターフェース
    // ========================================

    // タイル単位で呼び出され、出力バッファに書き込み
    void pushProcess(RenderResult&& input,
                     const RenderRequest& request) override {
        (void)request;  // 現在は未使用

        if (!input.isValid() || !target_.isValid()) return;

        // アフィン変換が伝播されている場合はDDA処理
        if (hasAffine_) {
            pushProcessWithAffine(std::move(input));
            return;
        }

        ViewPort inputView = input.view();

        // 基準点一致ルールに基づく配置計算（固定小数点演算）
        // input.origin: 入力バッファ内での基準点位置
        // originX_/Y_: 出力バッファ内での基準点位置
        // dstX = originX_ - input.origin.x
        int dstX = from_fixed8(originX_ - input.origin.x);
        int dstY = from_fixed8(originY_ - input.origin.y);

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
    }

private:
    ViewPort target_;
    int_fixed8 originX_ = 0;  // 出力先の基準点X（固定小数点 Q24.8）
    int_fixed8 originY_ = 0;  // 出力先の基準点Y（固定小数点 Q24.8）

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
    // アフィン変換付きプッシュ処理
    // ========================================
    void pushProcessWithAffine(RenderResult&& input) {
        // 特異行列チェック
        if (!invMatrix_.valid) {
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
                     const ViewPort& src, int_fixed8 srcOriginX, int_fixed8 srcOriginY) {
        if (!invMatrix_.valid) return;

        // srcOrigin分のみ計算（baseTx_/baseTy_ は dstOrigin 込みで事前計算済み）
        const int32_t srcOriginXInt = from_fixed8(srcOriginX);
        const int32_t srcOriginYInt = from_fixed8(srcOriginY);

        const int32_t fixedInvTx = baseTx_
                            + (srcOriginXInt << INT_FIXED16_SHIFT);
        const int32_t fixedInvTy = baseTy_
                            + (srcOriginYInt << INT_FIXED16_SHIFT);

        // 共通DDA処理を呼び出し
        transform::applyAffineDDA(dst, src, fixedInvTx, fixedInvTy,
                                  invMatrix_, rowOffsetX_, rowOffsetY_,
                                  dxOffsetX_, dxOffsetY_);
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_SINK_NODE_H
