/**
 * @file common.h
 * @brief Common definitions for fleximg library
 */

#ifndef FLEXIMG_COMMON_H
#define FLEXIMG_COMMON_H

// Namespace definition (types.h より前に必要)
#ifndef FLEXIMG_NAMESPACE
#define FLEXIMG_NAMESPACE fleximg
#endif

#include "types.h"

// Version information
#define FLEXIMG_VERSION_MAJOR 2
#define FLEXIMG_VERSION_MINOR 0
#define FLEXIMG_VERSION_PATCH 0

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// Point - 2D座標構造体（固定小数点 Q24.8）
// ========================================================================

struct Point {
    int_fixed8 x = 0;
    int_fixed8 y = 0;

    Point() = default;
    Point(int_fixed8 x_, int_fixed8 y_) : x(x_), y(y_) {}

    // 移行用コンストラクタ（float引数、最終的に削除予定）
    Point(float x_, float y_)
        : x(float_to_fixed8(x_)), y(float_to_fixed8(y_)) {}

    Point operator+(const Point& o) const { return {x + o.x, y + o.y}; }
    Point operator-(const Point& o) const { return {x - o.x, y - o.y}; }
    Point operator-() const { return {-x, -y}; }
    Point& operator+=(const Point& o) { x += o.x; y += o.y; return *this; }
    Point& operator-=(const Point& o) { x -= o.x; y -= o.y; return *this; }

    // 移行用アクセサ（float変換、最終的に削除予定）
    float xf() const { return fixed8_to_float(x); }
    float yf() const { return fixed8_to_float(y); }
};

// 後方互換性のためのエイリアス（最終的に削除予定）
using Point2f = Point;

// ========================================================================
// AffineMatrix - アフィン変換行列
// ========================================================================

struct AffineMatrix {
    float a = 1, b = 0;  // | a  b  tx |
    float c = 0, d = 1;  // | c  d  ty |
    float tx = 0, ty = 0;

    AffineMatrix() = default;
    AffineMatrix(float a_, float b_, float c_, float d_, float tx_, float ty_)
        : a(a_), b(b_), c(c_), d(d_), tx(tx_), ty(ty_) {}

    // 単位行列
    static AffineMatrix identity() { return {1, 0, 0, 1, 0, 0}; }

    // 平行移動
    static AffineMatrix translate(float x, float y) { return {1, 0, 0, 1, x, y}; }

    // スケール
    static AffineMatrix scale(float sx, float sy) { return {sx, 0, 0, sy, 0, 0}; }

    // 回転（ラジアン）
    static AffineMatrix rotate(float radians);
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_COMMON_H
