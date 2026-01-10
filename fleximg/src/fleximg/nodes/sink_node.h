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

    SinkNode(const ViewPort& vp, float originX = 0, float originY = 0)
        : target_(vp), originX_(originX), originY_(originY) {
        initPorts(1, 0);
    }

    // ターゲット設定
    void setTarget(const ViewPort& vp) { target_ = vp; }
    void setOrigin(float x, float y) { originX_ = x; originY_ = y; }

    // アクセサ
    const ViewPort& target() const { return target_; }
    ViewPort& target() { return target_; }
    float originX() const { return originX_; }
    float originY() const { return originY_; }

    // キャンバスサイズ（targetから取得）
    int canvasWidth() const { return target_.width; }
    int canvasHeight() const { return target_.height; }

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

        // 基準点一致ルールに基づく配置計算
        // input.origin: 画像左上の基準点相対座標
        // originX_/Y_: 出力バッファ内での基準点位置
        int dstX = static_cast<int>(input.origin.x + originX_);
        int dstY = static_cast<int>(input.origin.y + originY_);

        // クリッピング処理
        int srcX = 0, srcY = 0;
        if (dstX < 0) { srcX = -dstX; dstX = 0; }
        if (dstY < 0) { srcY = -dstY; dstY = 0; }

        int copyW = std::min(inputView.width - srcX, target_.width - dstX);
        int copyH = std::min(inputView.height - srcY, target_.height - dstY);

        if (copyW > 0 && copyH > 0) {
            view_ops::copy(target_, dstX, dstY, inputView, srcX, srcY, copyW, copyH);
        }
    }

private:
    ViewPort target_;
    float originX_ = 0;  // 出力先の基準点X（ピクセル座標）
    float originY_ = 0;  // 出力先の基準点Y（ピクセル座標）
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_SINK_NODE_H
