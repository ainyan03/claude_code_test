#ifndef FILTERS_H
#define FILTERS_H

#include "viewport.h"

namespace ImageTransform {

// ========================================================================
// フィルタパラメータ構造体
// ========================================================================

struct BrightnessFilterParams {
    float brightness;  // -1.0 ~ 1.0

    BrightnessFilterParams(float b = 0.0f) : brightness(b) {}
};

struct GrayscaleFilterParams {
    // パラメータなし（将来の拡張用）
    GrayscaleFilterParams() {}
};

struct BoxBlurFilterParams {
    int radius;  // ブラー半径（1以上）

    BoxBlurFilterParams(int r = 3) : radius(r > 0 ? r : 1) {}
};

// ========================================================================
// フィルタクラス群（ViewPortベース処理）
// ========================================================================

// フィルタの基底クラス
class ImageFilter {
protected:
    ImageFilter(const char* name) : name_(name) {}

public:
    virtual ~ImageFilter() = default;
    virtual ViewPort apply(const ViewPort& input) const = 0;
    const char* getName() const { return name_; }

    // このフィルタが要求する入力形式
    virtual PixelFormatID getPreferredInputFormat() const {
        return PixelFormatIDs::RGBA16_Straight;  // デフォルト: Straight
    }

    // このフィルタが出力する形式
    virtual PixelFormatID getOutputFormat() const {
        return PixelFormatIDs::RGBA16_Straight;  // デフォルト: Straight
    }

private:
    const char* name_;
};

// 明るさ調整フィルタ
class BrightnessFilter : public ImageFilter {
public:
    explicit BrightnessFilter(const BrightnessFilterParams& params)
        : ImageFilter("Brightness"), params_(params) {}

    ViewPort apply(const ViewPort& input) const override;

private:
    BrightnessFilterParams params_;
};

// グレースケールフィルタ
class GrayscaleFilter : public ImageFilter {
public:
    explicit GrayscaleFilter(const GrayscaleFilterParams& params = {})
        : ImageFilter("Grayscale"), params_(params) {}

    ViewPort apply(const ViewPort& input) const override;

private:
    GrayscaleFilterParams params_;
};

// ボックスブラーフィルタ
class BoxBlurFilter : public ImageFilter {
public:
    explicit BoxBlurFilter(const BoxBlurFilterParams& params)
        : ImageFilter("BoxBlur"), params_(params) {}

    ViewPort apply(const ViewPort& input) const override;

private:
    BoxBlurFilterParams params_;
};

} // namespace ImageTransform

#endif // FILTERS_H
