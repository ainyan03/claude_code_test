#ifndef IMAGE_TYPES_H
#define IMAGE_TYPES_H

#include <vector>
#include <cstdint>
#include <cmath>
#include "pixel_format.h"

namespace ImageTransform {

// ========================================================================
// 画像データ構造（8bit RGBA、ストレートアルファ）
// ========================================================================
// 【非推奨】このImageは将来的にViewPortに統合されます。
// 新規コードではViewPortの使用を推奨します。
// 移行完了後にこの型は削除されます。
struct Image {
    std::vector<uint8_t> data;  // RGBA format
    int width;
    int height;

    Image() : width(0), height(0) {}
    Image(int w, int h) : width(w), height(h), data(w * h * 4, 0) {}
};

// アフィン変換パラメータ
struct AffineParams {
    double translateX;  // 平行移動 X
    double translateY;  // 平行移動 Y
    double rotation;    // 回転角度（ラジアン）
    double scaleX;      // スケール X
    double scaleY;      // スケール Y

    AffineParams()
        : translateX(0), translateY(0), rotation(0),
          scaleX(1.0), scaleY(1.0) {}
};

// 2x3アフィン変換行列
// [a  b  tx]   [x]   [a*x + b*y + tx]
// [c  d  ty] * [y] = [c*x + d*y + ty]
//              [1]
struct AffineMatrix {
    double a, b, c, d, tx, ty;

    AffineMatrix() : a(1), b(0), c(0), d(1), tx(0), ty(0) {}

    // AffineParamsから行列を生成
    static AffineMatrix fromParams(const AffineParams& params, double centerX, double centerY);
};

} // namespace ImageTransform

#endif // IMAGE_TYPES_H
