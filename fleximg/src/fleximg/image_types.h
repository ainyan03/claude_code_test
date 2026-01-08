#ifndef FLEXIMG_IMAGE_TYPES_H
#define FLEXIMG_IMAGE_TYPES_H

#include <cmath>

#include "common.h"

namespace FLEXIMG_NAMESPACE {

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
