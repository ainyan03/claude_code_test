#ifndef FILTERS_H
#define FILTERS_H

#include "image_types.h"

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
// フィルタクラス群（16bit premultiplied alpha処理）
// ========================================================================

// 16bitフィルタの基底クラス
class ImageFilter16 {
protected:
    ImageFilter16(const char* name) : name_(name) {}

public:
    virtual ~ImageFilter16() = default;
    virtual Image16 apply(const Image16& input) const = 0;
    const char* getName() const { return name_; }

private:
    const char* name_;
};

// 明るさ調整フィルタ
class BrightnessFilter16 : public ImageFilter16 {
public:
    explicit BrightnessFilter16(const BrightnessFilterParams& params)
        : ImageFilter16("Brightness"), params_(params) {}

    Image16 apply(const Image16& input) const override;

private:
    BrightnessFilterParams params_;
};

// グレースケールフィルタ
class GrayscaleFilter16 : public ImageFilter16 {
public:
    explicit GrayscaleFilter16(const GrayscaleFilterParams& params = {})
        : ImageFilter16("Grayscale"), params_(params) {}

    Image16 apply(const Image16& input) const override;

private:
    GrayscaleFilterParams params_;
};

// ボックスブラーフィルタ
class BoxBlurFilter16 : public ImageFilter16 {
public:
    explicit BoxBlurFilter16(const BoxBlurFilterParams& params)
        : ImageFilter16("BoxBlur"), params_(params) {}

    Image16 apply(const Image16& input) const override;

private:
    BoxBlurFilterParams params_;
};

} // namespace ImageTransform

#endif // FILTERS_H
