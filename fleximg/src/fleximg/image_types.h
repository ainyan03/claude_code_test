#ifndef FLEXIMG_IMAGE_TYPES_H
#define FLEXIMG_IMAGE_TYPES_H

#include <vector>
#include <cstdint>
#include <cmath>

#include "common.h"
#include "pixel_format.h"

namespace FLEXIMG_NAMESPACE {

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

// 2x3アフィン変換行列
// [a  b  tx]   [x]   [a*x + b*y + tx]
// [c  d  ty] * [y] = [c*x + d*y + ty]
//              [1]
struct AffineMatrix {
    float a, b, c, d, tx, ty;

    AffineMatrix() : a(1.0f), b(0.0f), c(0.0f), d(1.0f), tx(0.0f), ty(0.0f) {}
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMAGE_TYPES_H
