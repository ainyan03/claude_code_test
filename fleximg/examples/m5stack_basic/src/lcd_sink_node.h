#ifndef LCD_SINK_NODE_H
#define LCD_SINK_NODE_H

// fleximg core
#include "fleximg/core/node.h"
#include "fleximg/image/pixel_format.h"

// M5Unified
#include <M5Unified.h>

#include <vector>
#include <algorithm>

namespace fleximg {

// ========================================================================
// LcdSinkNode - M5GFX LCD出力ノード
// ========================================================================
//
// fleximg パイプラインの終端ノードとして、スキャンラインを
// M5GFX経由でLCDに転送します。
//
// - 入力ポート: 1
// - 出力ポート: 0
// - RGBA8_Straight → RGB565_BE (swap565_t) 変換
// - スキャンライン単位でLCD転送
//

class LcdSinkNode : public Node {
public:
    LcdSinkNode()
        : lcd_(nullptr)
        , windowX_(0), windowY_(0)
        , windowW_(0), windowH_(0)
        , originX_(0), originY_(0)
        , currentY_(0)
    {
        initPorts(1, 0);  // 入力1、出力0（終端）
    }

    // ターゲットLCDと描画領域を設定
    void setTarget(M5GFX* lcd, int16_t x, int16_t y, int16_t w, int16_t h) {
        lcd_ = lcd;
        windowX_ = x;
        windowY_ = y;
        windowW_ = w;
        windowH_ = h;
    }

    // 基準点設定（固定小数点）
    void setOrigin(int_fixed x, int_fixed y) {
        originX_ = x;
        originY_ = y;
    }

    // アクセサ
    int16_t windowWidth() const { return windowW_; }
    int16_t windowHeight() const { return windowH_; }

    const char* name() const override { return "LcdSinkNode"; }

protected:
    int nodeTypeForMetrics() const override { return 100; }  // カスタムノードID

public:
    // ========================================
    // プッシュ型準備
    // ========================================
    bool pushPrepare(const PrepareRequest& request) override {
        bool shouldContinue;
        if (!checkPrepareState(pushPrepareState_, shouldContinue)) {
            return false;
        }
        if (!shouldContinue) {
            return true;
        }

        if (!lcd_) {
            pushPrepareState_ = PrepareState::CycleError;
            return false;
        }

        // 出力予定の画像幅と基準点を保存
        expectedWidth_ = request.width;
        expectedOriginX_ = request.origin.x;

        // LCDトランザクション開始
        lcd_->startWrite();
        currentY_ = 0;

        pushPrepareState_ = PrepareState::Prepared;
        return true;
    }

    // ========================================
    // プッシュ型処理（画像転送）
    // ========================================
    void pushProcess(RenderResult&& input,
                     const RenderRequest& request) override {
        (void)request;

        if (!lcd_) return;
        if (pushPrepareState_ != PrepareState::Prepared) return;

        ViewPort inputView = input.isValid() ? input.view() : ViewPort();

        // 基準点一致ルールに基づく配置計算
        // Y座標はrequestから取得（inputが無効でも常に正しい値を持つ）
        int dstX = input.isValid() ? from_fixed(originX_ - input.origin.x) : 0;
        int dstY = from_fixed(originY_ - request.origin.y);

        // クリッピング処理
        int srcX = 0, srcY = 0;
        if (dstX < 0) { srcX = -dstX; dstX = 0; }
        if (dstY < 0) { srcY = -dstY; dstY = 0; }

        int copyW = input.isValid() ? std::min<int>(inputView.width - srcX, windowW_ - dstX) : 0;
        int copyH = input.isValid() ? std::min<int>(inputView.height - srcY, windowH_ - dstY) : 0;
        if (copyW < 0) copyW = 0;
        if (copyH < 0) copyH = 0;

        // 予定領域との比較で左右余白を消去
        int expectedDstX = from_fixed(originX_ - expectedOriginX_);
        int expectedRight = expectedDstX + expectedWidth_;

        // 描画高さ（有効範囲がなくても1ライン分消去）
        int fillH = (copyH > 0) ? copyH : 1;
        int fillY = dstY;

        // 有効な描画範囲がある場合
        if (copyW > 0 && copyH > 0) {
            // 左余白を消去（expectedDstX から dstX まで）
            if (dstX > expectedDstX) {
                lcd_->fillRect(windowX_ + expectedDstX, windowY_ + fillY,
                               dstX - expectedDstX, fillH, 0);
            }
            // 右余白を消去（dstX + copyW から expectedRight まで）
            int imageRight = dstX + copyW;
            if (imageRight < expectedRight) {
                lcd_->fillRect(windowX_ + imageRight, windowY_ + fillY,
                               expectedRight - imageRight, fillH, 0);
            }

            // 変換バッファのサイズを確保
            size_t bufferSize = static_cast<size_t>(copyW) * static_cast<size_t>(copyH);
            if (imageBuffer_.size() < bufferSize) {
                imageBuffer_.resize(bufferSize);
            }

            // スキャンライン単位でRGB565_BEに変換
            for (int y = 0; y < copyH; ++y) {
                const void* srcRow = inputView.pixelAt(srcX, srcY + y);
                uint16_t* dstRow = imageBuffer_.data() + static_cast<size_t>(y) * static_cast<size_t>(copyW);

                ::fleximg::convertFormat(
                    srcRow,
                    inputView.formatID,
                    dstRow,
                    ::fleximg::PixelFormatIDs::RGB565_BE,
                    copyW
                );
            }

            // pushImageで正しい位置に描画
            lcd_->pushImage(
                windowX_ + dstX,
                windowY_ + dstY,
                copyW,
                copyH,
                reinterpret_cast<const lgfx::swap565_t*>(imageBuffer_.data())
            );
        } else {
            // 有効な画像がない場合は全体を消去
            lcd_->fillRect(windowX_ + expectedDstX, windowY_ + fillY, expectedWidth_, fillH, 0);
        }
    }

    // ========================================
    // プッシュ型終了処理
    // ========================================
    void pushFinalize() override {
        if (lcd_) {
            lcd_->endWrite();
        }
        Node::pushFinalize();
    }

private:
    M5GFX* lcd_;
    int16_t windowX_, windowY_;
    int16_t windowW_, windowH_;
    int_fixed originX_, originY_;
    int16_t currentY_;

    // 出力予定情報（pushPrepareで保存）
    int16_t expectedWidth_ = 0;
    int_fixed expectedOriginX_ = 0;

    // RGB565変換バッファ（画像全体用）
    std::vector<uint16_t> imageBuffer_;
};

} // namespace fleximg

#endif // LCD_SINK_NODE_H
