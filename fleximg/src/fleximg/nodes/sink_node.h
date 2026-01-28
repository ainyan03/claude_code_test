#ifndef FLEXIMG_SINK_NODE_H
#define FLEXIMG_SINK_NODE_H

#include "../core/node.h"
#include "../core/affine_capability.h"
#include "../core/perf_metrics.h"
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
// アフィン変換はAffineCapability Mixinから継承:
// - setMatrix(), matrix()
// - setRotation(), setScale(), setTranslation(), setRotationScale()
//
// 座標系と視覚効果:
// - pivot: ワールド原点 (0,0) に対応するバッファ座標 + アフィン回転中心
// - tx/ty: 平行移動（アフィン時は回転の影響を受けない）
// - 変換式（アフィンあり）: src = Inv * (buf - pivot - tx) + pivot
// - 変換式（アフィンなし）: src = buf - pivot - tx
//

class SinkNode : public Node, public AffineCapability {
public:
    // コンストラクタ
    SinkNode() {
        initPorts(1, 0);  // 入力1、出力0
    }

    SinkNode(const ViewPort& vp, int_fixed pivotX = 0, int_fixed pivotY = 0)
        : target_(vp), pivotX_(pivotX), pivotY_(pivotY) {
        initPorts(1, 0);
    }

    // ターゲット設定
    void setTarget(const ViewPort& vp) { target_ = vp; }

    // pivot 設定（出力バッファ座標、変換の中心点）
    void setPivot(int_fixed x, int_fixed y) { pivotX_ = x; pivotY_ = y; }
    void setPivot(float x, float y) {
        pivotX_ = float_to_fixed(x);
        pivotY_ = float_to_fixed(y);
    }

    // 便利メソッド: ターゲット中央を pivot に設定
    void setPivotCenter() {
        pivotX_ = to_fixed(target_.width / 2);
        pivotY_ = to_fixed(target_.height / 2);
    }

    // アクセサ
    const ViewPort& target() const { return target_; }
    ViewPort& target() { return target_; }
    int_fixed pivotX() const { return pivotX_; }
    int_fixed pivotY() const { return pivotY_; }
    std::pair<float, float> getPivot() const {
        return {fixed_to_float(pivotX_), fixed_to_float(pivotY_)};
    }

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
    // SinkNodeは終端なので下流への伝播なし、PrepareResponseを返す
    PrepareResponse onPushPrepare(const PrepareRequest& request) override;

    // onPushProcess: タイル単位で呼び出され、出力バッファに書き込み
    // SinkNodeは終端なので下流への伝播なし
    void onPushProcess(RenderResponse&& input,
                       const RenderRequest& request) override;

private:
    ViewPort target_;
    int_fixed pivotX_ = 0;  // 変換の中心点X（出力バッファ座標、固定小数点 Q16.16）
    int_fixed pivotY_ = 0;  // 変換の中心点Y（出力バッファ座標、固定小数点 Q16.16）

    // アフィン伝播用メンバ変数（事前計算済み）
    Matrix2x2_fixed invMatrix_;    // 逆行列（固定小数点）
    int32_t baseTx_ = 0;           // 事前計算済みオフセットX（Q16.16、pivot込み）
    int32_t baseTy_ = 0;           // 事前計算済みオフセットY（Q16.16、pivot込み）
    bool hasAffine_ = false;       // アフィン変換が伝播されているか

    // アフィン変換付きプッシュ処理
    void pushProcessWithAffine(RenderResponse&& input);

    // アフィン変換実装（事前計算済み値を使用）
    void applyAffine(ViewPort& dst,
                     const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY);
};

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// SinkNode - Template Method フック実装
// ============================================================================

PrepareResponse SinkNode::onPushPrepare(const PrepareRequest& request) {
    // アフィン情報を受け取り、事前計算を行う
    // localMatrix_ も含めて合成
    AffineMatrix combinedMatrix;
    bool hasTransform = false;

    if (request.hasPushAffine || hasLocalTransform()) {
        // 行列合成: localMatrix_ * request.pushAffineMatrix
        // Pull側と同じ合成順序（自身の変換を先に掛ける）
        if (request.hasPushAffine) {
            combinedMatrix = localMatrix_ * request.pushAffineMatrix;
        } else {
            combinedMatrix = localMatrix_;
        }
        hasTransform = true;

        // 逆行列を固定小数点に変換
        invMatrix_ = inverseFixed(combinedMatrix);

        if (invMatrix_.valid) {
            // 変換式: src = Inv * (buf - pivot - tx)
            // - pivot: ワールド原点 (0,0) に対応するバッファ座標 + 回転中心
            // - tx/ty: 平行移動（回転の影響を受けない）
            // 全て int_fixed (Q16.16) で演算し、小数精度を保持
            int_fixed txFixed = float_to_fixed(combinedMatrix.tx);
            int_fixed tyFixed = float_to_fixed(combinedMatrix.ty);
            // (pivot + tx) を Inv で変換
            int64_t combinedX = pivotX_ + txFixed;
            int64_t combinedY = pivotY_ + tyFixed;
            int64_t invCombinedX = (combinedX * invMatrix_.a
                                  + combinedY * invMatrix_.b) >> INT_FIXED_SHIFT;
            int64_t invCombinedY = (combinedX * invMatrix_.c
                                  + combinedY * invMatrix_.d) >> INT_FIXED_SHIFT;
            // baseTx_ = -Inv * (pivot + tx)
            baseTx_ = -static_cast<int32_t>(invCombinedX);
            baseTy_ = -static_cast<int32_t>(invCombinedY);
        }
        hasAffine_ = true;
    } else {
        hasAffine_ = false;
    }

    // SinkNodeは終端なので下流への伝播なし
    // プッシュアフィン変換がある場合、入力側で必要なAABBを計算
    PrepareResponse result;
    result.status = PrepareStatus::Prepared;
    result.preferredFormat = target_.formatID;

    if (hasTransform && invMatrix_.valid) {
        // 逆行列でAABBを計算（buf→src変換の結果範囲）
        // 変換式: src = Inv * (buf - pivot - tx)
        // (pivot + tx) を "pivot" として渡す
        AffineMatrix aabbMatrix = combinedMatrix;
        aabbMatrix.tx = 0;
        aabbMatrix.ty = 0;
        int_fixed combinedPivotX = pivotX_ + float_to_fixed(combinedMatrix.tx);
        int_fixed combinedPivotY = pivotY_ + float_to_fixed(combinedMatrix.ty);
        calcInverseAffineAABB(
            target_.width, target_.height,
            {combinedPivotX, combinedPivotY},
            aabbMatrix,
            result.width, result.height, result.origin);
    } else {
        // アフィンなしの場合
        // 変換式: src = buf - tx - pivot
        // バッファ左上 (0, 0) のワールド座標 = -tx - pivot
        result.width = target_.width;
        result.height = target_.height;
        // pivotX_/pivotY_ は int_fixed (Q16.16) で小数成分を保持
        result.origin = {
            -float_to_fixed(localMatrix_.tx) - pivotX_,
            -float_to_fixed(localMatrix_.ty) - pivotY_
        };
    }
    return result;
}

void SinkNode::onPushProcess(RenderResponse&& input,
                             const RenderRequest& request) {
    (void)request;  // 現在は未使用

    if (!input.isValid() || !target_.isValid()) return;

    FLEXIMG_METRICS_SCOPE(NodeType::Sink);

    // アフィン変換が伝播されている場合はDDA処理
    if (hasAffine_) {
        pushProcessWithAffine(std::move(input));
        return;
    }

    // ターゲットフォーマットに変換（フォーマットが異なる場合のみ）
    // ImageBuffer::toFormat を使うことで、Index8のパレット展開等が正しく処理される
    ImageBuffer convertedBuffer;
    ViewPort inputView;

    if (input.buffer.formatID() != target_.formatID) {
        convertedBuffer = std::move(input.buffer).toFormat(target_.formatID);
        inputView = convertedBuffer.view();
    } else {
        inputView = input.view();
    }

    // 配置計算（固定小数点演算）
    // 変換式: src = buf - tx - pivot → buf = src + tx + pivot
    // 全て int_fixed (Q16.16) で演算し、最終的にピクセル座標へ変換
    int_fixed txFixed = float_to_fixed(localMatrix_.tx);
    int_fixed tyFixed = float_to_fixed(localMatrix_.ty);
    int dstX = from_fixed(input.origin.x + txFixed + pivotX_);
    int dstY = from_fixed(input.origin.y + tyFixed + pivotY_);

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

// ============================================================================
// SinkNode - private ヘルパーメソッド実装
// ============================================================================

void SinkNode::pushProcessWithAffine(RenderResponse&& input) {
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

    // アフィン変換を適用してターゲットに書き込み（pivot は事前計算済み）
    applyAffine(target_, inputView, input.origin.x, input.origin.y);
}

void SinkNode::applyAffine(ViewPort& dst,
                           const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY) {
    if (!invMatrix_.valid) return;

    // srcOrigin分のみ計算（baseTx_/baseTy_ は pivot 込みで事前計算済み）
    // srcOrigin は入力バッファ左上のワールド座標
    const int32_t srcOriginXInt = from_fixed(srcOriginX);
    const int32_t srcOriginYInt = from_fixed(srcOriginY);

    // baseTx_はすでにworldオフセットを含む
    // srcOriginは入力バッファの左上のworld座標なので減算
    const int32_t fixedTx = baseTx_ - (srcOriginXInt << INT_FIXED_SHIFT);
    const int32_t fixedTy = baseTy_ - (srcOriginYInt << INT_FIXED_SHIFT);

    // ピクセル中心オフセット（逆行列用）
    int_fixed rowOffsetX = invMatrix_.b >> 1;
    int_fixed rowOffsetY = invMatrix_.d >> 1;
    int_fixed dxOffsetX = invMatrix_.a >> 1;
    int_fixed dxOffsetY = invMatrix_.c >> 1;

    // 共通DDA処理を呼び出し
    view_ops::affineTransform(dst, src, fixedTx, fixedTy,
                              invMatrix_, rowOffsetX, rowOffsetY,
                              dxOffsetX, dxOffsetY);
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_SINK_NODE_H
