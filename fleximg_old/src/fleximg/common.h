/**
 * @file common.h
 * @brief Common definitions for fleximg library
 */

#ifndef FLEXIMG_COMMON_H
#define FLEXIMG_COMMON_H

// Namespace definition
// Users can override this by defining FLEXIMG_NAMESPACE before including fleximg.h
#ifndef FLEXIMG_NAMESPACE
#define FLEXIMG_NAMESPACE fleximg
#endif

// Version information
#define FLEXIMG_VERSION_MAJOR 0
#define FLEXIMG_VERSION_MINOR 1
#define FLEXIMG_VERSION_PATCH 0

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// Point2f - 2D座標構造体
// ========================================================================

struct Point2f {
    float x = 0;
    float y = 0;

    Point2f() = default;
    Point2f(float x_, float y_) : x(x_), y(y_) {}

    Point2f operator+(const Point2f& o) const { return {x + o.x, y + o.y}; }
    Point2f operator-(const Point2f& o) const { return {x - o.x, y - o.y}; }
    Point2f& operator+=(const Point2f& o) { x += o.x; y += o.y; return *this; }
    Point2f& operator-=(const Point2f& o) { x -= o.x; y -= o.y; return *this; }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_COMMON_H
