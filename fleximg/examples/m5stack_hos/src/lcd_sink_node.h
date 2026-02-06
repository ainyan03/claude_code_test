#ifndef LCD_SINK_NODE_H
#define LCD_SINK_NODE_H

// fleximg core
#include "fleximg/core/node.h"
#include "fleximg/image/pixel_format.h"
#include "fleximg/image/data_range.h"

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
// - 前回描画範囲を記憶し、和集合範囲のみを転送（差分更新最適化）
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
        // 前回描画範囲の配列を初期化
        prevRanges_.resize(static_cast<size_t>(h));
        clearPrevRanges();
    }

    // 前回描画範囲をクリア（全画面再描画が必要な場合に呼ぶ）
    void clearPrevRanges() {
        for (auto& range : prevRanges_) {
            range = DataRange{0, 0};  // 無効範囲
        }
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

    // onPushProcess: 画像転送（差分更新最適化）
    void onPushProcess(RenderResponse& input,
                       const RenderRequest& request) override {
        (void)request;

        if (!lcd_ || !drawEnabled_) return;

        // バッファ準備
        consolidateIfNeeded(input);

        ViewPort inputView = input.isValid() ? input.view() : ViewPort();

        // 座標計算
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

        // 描画高さ（有効範囲がなくても1ライン分処理）
        int fillH = (copyH > 0) ? copyH : 1;
        int fillY = dstY;

        // ダブルバッファ: 現在のバッファを選択
        auto& currentBuffer = imageBuffers_[currentBufferIndex_];
        currentBufferIndex_ = 1 - currentBufferIndex_;

        // 行ごとに差分更新処理
        for (int y = 0; y < fillH; ++y) {
            int screenY = fillY + y;
            if (screenY < 0 || screenY >= windowH_) continue;

            // 今回の描画範囲を計算
            DataRange currRange{0, 0};  // 無効範囲
            if (copyW > 0 && y < copyH) {
                currRange.startX = static_cast<int16_t>(dstX);
                currRange.endX = static_cast<int16_t>(dstX + copyW);
            }

            // 前回の描画範囲を取得
            DataRange& prevRange = prevRanges_[static_cast<size_t>(screenY)];

            // 和集合範囲を計算
            DataRange unionRange{0, 0};
            if (currRange.hasData() && prevRange.hasData()) {
                // 両方有効: 和集合
                unionRange.startX = std::min(currRange.startX, prevRange.startX);
                unionRange.endX = std::max(currRange.endX, prevRange.endX);
            } else if (currRange.hasData()) {
                // 今回のみ有効
                unionRange = currRange;
            } else if (prevRange.hasData()) {
                // 前回のみ有効（クリアが必要）
                unionRange = prevRange;
            }

            // 前回範囲を更新
            prevRange = currRange;

            // 和集合範囲がなければスキップ
            if (!unionRange.hasData()) continue;

            int unionWidth = unionRange.width();

            // バッファを和集合幅で確保
            size_t bufferSize = static_cast<size_t>(unionWidth);
            if (currentBuffer.size() < bufferSize) {
                currentBuffer.resize(bufferSize);
            }

            // バッファをゼロクリア（前回範囲で今回範囲外の部分を黒に）
            std::fill(currentBuffer.begin(),
                      currentBuffer.begin() + static_cast<ptrdiff_t>(bufferSize), 0);

            // 今回の有効範囲がある場合、画像データを変換して配置
            if (currRange.hasData() && y < copyH) {
                int offsetInBuffer = currRange.startX - unionRange.startX;
                const void* srcRow = inputView.pixelAt(srcX, srcY + y);
                uint16_t* dstRow = currentBuffer.data() + offsetInBuffer;

                ::fleximg::convertFormat(
                    srcRow,
                    inputView.formatID,
                    dstRow,
                    ::fleximg::PixelFormatIDs::RGB565_BE,
                    copyW
                );
            }

            // 和集合範囲のみをLCD転送
            lcd_->pushImageDMA(
                windowX_ + unionRange.startX,
                windowY_ + screenY,
                unionWidth,
                1,
                reinterpret_cast<const lgfx::swap565_t*>(currentBuffer.data())
            );
        }
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

    // 前回描画範囲（Y座標別）
    std::vector<DataRange> prevRanges_;
};

} // namespace fleximg

#endif // LCD_SINK_NODE_H
