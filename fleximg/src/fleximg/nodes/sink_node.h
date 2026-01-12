#ifndef FLEXIMG_SINK_NODE_H
#define FLEXIMG_SINK_NODE_H

#include "../node.h"
#include "../viewport.h"
#include <algorithm>

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
    // プッシュ型インターフェース
    // ========================================

    // タイル単位で呼び出され、出力バッファに書き込み
    void pushProcess(RenderResult&& input,
                     const RenderRequest& request) override {
        (void)request;  // 現在は未使用

        if (!input.isValid() || !target_.isValid()) return;

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
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_SINK_NODE_H
