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
#include <cmath>

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

// ========================================================================
// 行列変換関数
// ========================================================================

// AffineMatrix の 2x2 部分を固定小数点で返す（順変換用）
// 平行移動成分(tx,ty)は含まない（呼び出し側で別途管理）
inline Matrix2x2_fixed16 toFixed16(const AffineMatrix& m) {
    return Matrix2x2_fixed16(
        static_cast<int_fixed16>(std::lround(m.a * INT_FIXED16_ONE)),
        static_cast<int_fixed16>(std::lround(m.b * INT_FIXED16_ONE)),
        static_cast<int_fixed16>(std::lround(m.c * INT_FIXED16_ONE)),
        static_cast<int_fixed16>(std::lround(m.d * INT_FIXED16_ONE)),
        true  // valid
    );
}

// AffineMatrix の 2x2 部分の逆行列を固定小数点で返す（逆変換用）
// 平行移動成分(tx,ty)は含まない（呼び出し側で別途管理）
inline Matrix2x2_fixed16 inverseFixed16(const AffineMatrix& m) {
    float det = m.a * m.d - m.b * m.c;
    if (std::abs(det) < 1e-10f) {
        return Matrix2x2_fixed16();  // valid = false
    }

    float invDet = 1.0f / det;
    return Matrix2x2_fixed16(
        static_cast<int_fixed16>(std::lround(m.d * invDet * INT_FIXED16_ONE)),
        static_cast<int_fixed16>(std::lround(-m.b * invDet * INT_FIXED16_ONE)),
        static_cast<int_fixed16>(std::lround(-m.c * invDet * INT_FIXED16_ONE)),
        static_cast<int_fixed16>(std::lround(m.a * invDet * INT_FIXED16_ONE)),
        true  // valid
    );
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_COMMON_H
