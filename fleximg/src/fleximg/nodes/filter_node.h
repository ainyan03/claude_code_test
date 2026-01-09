#ifndef FLEXIMG_FILTER_NODE_H
#define FLEXIMG_FILTER_NODE_H

#include "../node.h"
#include <string>
#include <vector>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// FilterType - フィルタ種別
// ========================================================================

enum class FilterType {
    None,
    Brightness,
    Grayscale,
    BoxBlur,
    Alpha
};

// ========================================================================
// FilterNode - フィルタノード
// ========================================================================
//
// 入力画像に対してフィルタ処理を適用します。
// - 入力: 1ポート
// - 出力: 1ポート
//
// 使用例:
//   FilterNode blur;
//   blur.setBoxBlur(5);
//   src >> blur >> sink;
//

class FilterNode : public Node {
public:
    FilterNode() {
        initPorts(1, 1);  // 入力1、出力1
    }

    // ========================================
    // フィルタ設定
    // ========================================

    FilterType filterType() const { return filterType_; }

    // 明るさ調整
    void setBrightness(float amount) {
        filterType_ = FilterType::Brightness;
        param1_ = amount;
    }
    float brightnessAmount() const { return param1_; }

    // グレースケール
    void setGrayscale() {
        filterType_ = FilterType::Grayscale;
    }

    // ボックスブラー
    void setBoxBlur(int radius) {
        filterType_ = FilterType::BoxBlur;
        param1_ = static_cast<float>(radius);
    }
    int blurRadius() const { return static_cast<int>(param1_); }

    // アルファ調整
    void setAlpha(float scale) {
        filterType_ = FilterType::Alpha;
        param1_ = scale;
    }
    float alphaScale() const { return param1_; }

    // カーネル半径（ブラー用、入力要求計算に使用）
    int kernelRadius() const {
        if (filterType_ == FilterType::BoxBlur) {
            return static_cast<int>(param1_);
        }
        return 0;
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "FilterNode"; }

private:
    FilterType filterType_ = FilterType::None;
    float param1_ = 0.0f;
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_FILTER_NODE_H
