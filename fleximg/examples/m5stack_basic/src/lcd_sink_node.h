#ifndef LCD_SINK_NODE_H
#define LCD_SINK_NODE_H

// fleximg core
#include "fleximg/core/node.h"
#include "fleximg/image/pixel_format.h"
#include "fleximg/image/image_buffer_set.h"

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

    bool getDrawEnabled() const { return drawEnabled_; }
    void setDrawEnabled(bool en) { drawEnabled_ = en; }

protected:
    bool drawEnabled_ = true;

    int nodeTypeForMetrics() const override { return 100; }  // カスタムノードID

    // ========================================
    // Template Method フック
    // ========================================

    // onPushPrepare: LCD準備
    PrepareResponse onPushPrepare(const PrepareRequest& request) override {
        PrepareResponse result;
        if (!lcd_) {
            result.status = PrepareStatus::NoDownstream;
            return result;
        }

        // 出力予定の画像幅と基準点を保存
        // request が未設定（0）の場合は自身のウィンドウサイズを使用
        expectedWidth_ = (request.width > 0) ? request.width : windowW_;
        expectedOriginX_ = (request.width > 0) ? request.origin.x : originX_;

        // LCDトランザクション開始
        lcd_->startWrite();
        currentY_ = 0;

        result.status = PrepareStatus::Prepared;
        result.width = windowW_;
        result.height = windowH_;
        result.origin = {-originX_, -originY_};
        return result;
    }

    // onPushProcess: 画像転送
    void onPushProcess(RenderResponse&& input,
                       const RenderRequest& request) override {
        (void)request;

        if (!lcd_ || !drawEnabled_) return;

        // ImageBufferSetの場合はconsolidate()して単一バッファに変換
        consolidateIfNeeded(input);

        // パレット情報を取得（Index8フォーマット対応）
        const ::fleximg::PixelAuxInfo* srcAux = nullptr;
        if (input.buffer.auxInfo().palette) {
            srcAux = &input.buffer.auxInfo();
        }

        ViewPort inputView = input.isValid() ? input.view() : ViewPort();

        // 新座標系: originはバッファ左上のワールド座標
        // Y座標はrequestから取得（inputが無効でも常に正しい値を持つ）
        // offset = 入力左上 - 出力左上
        int dstX = from_fixed(originX_ + input.origin.x);
        int dstY = from_fixed(originY_ + input.origin.y);

        // クリッピング処理
        int srcX = 0, srcY = 0;
        if (dstX < 0) { srcX = -dstX; dstX = 0; }
        if (dstY < 0) { srcY = -dstY; dstY = 0; }

        int copyW = input.isValid() ? std::min<int>(inputView.width - srcX, windowW_ - dstX) : 0;
        int copyH = input.isValid() ? std::min<int>(inputView.height - srcY, windowH_ - dstY) : 0;
        if (copyW < 0) copyW = 0;
        if (copyH < 0) copyH = 0;

        // 予定領域の配置計算（新座標系）
        int expectedDstX = from_fixed(expectedOriginX_ - originX_);

        // 描画高さ（有効範囲がなくても1ライン分描画）
        int fillH = (copyH > 0) ? copyH : 1;
        int fillY = dstY;

        // ダブルバッファ: 現在のバッファを選択
        auto& currentBuffer = imageBuffers_[currentBufferIndex_];
        currentBufferIndex_ = 1 - currentBufferIndex_;  // 次回用に切り替え

        // バッファをexpectedWidth幅で確保（余白含む）
        size_t rowWidth = static_cast<size_t>(expectedWidth_);
        size_t bufferSize = rowWidth * static_cast<size_t>(fillH);
        if (currentBuffer.size() < bufferSize) {
            currentBuffer.resize(bufferSize);
        }

        // バッファ全体をゼロクリア（余白を黒に）
        std::fill(currentBuffer.begin(), currentBuffer.begin() + static_cast<ptrdiff_t>(bufferSize), 0);

        // 有効な描画範囲がある場合、画像部分のみ変換して配置
        if (copyW > 0 && copyH > 0) {
            int offsetInRow = dstX - expectedDstX;

            // スキャンライン単位でRGB565_BEに変換
            for (int y = 0; y < copyH; ++y) {
                const void* srcRow = inputView.pixelAt(srcX, srcY + y);
                uint16_t* dstRow = currentBuffer.data()
                    + static_cast<size_t>(y) * rowWidth
                    + static_cast<size_t>(offsetInRow);

                ::fleximg::convertFormat(
                    srcRow,
                    inputView.formatID,
                    dstRow,
                    ::fleximg::PixelFormatIDs::RGB565_BE,
                    copyW
                );
            }
        }

        // 余白含めて一括転送
        lcd_->pushImageDMA(
            windowX_ + expectedDstX,
            windowY_ + fillY,
            expectedWidth_,
            fillH,
            reinterpret_cast<const lgfx::swap565_t*>(currentBuffer.data())
        );
    }

    // onPushFinalize: LCD終了処理
    void onPushFinalize() override {
        if (lcd_) {
            lcd_->endWrite();
        }
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

    // RGB565変換ダブルバッファ（DMA転送中の上書き防止）
    std::vector<uint16_t> imageBuffers_[2];
    int currentBufferIndex_ = 0;
};

} // namespace fleximg

#endif // LCD_SINK_NODE_H
