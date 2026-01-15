#ifndef FLEXIMG_TYPES_H
#define FLEXIMG_TYPES_H

#include <cstdint>
#include <cmath>

namespace FLEXIMG_NAMESPACE {
namespace core {

// ========================================================================
// 固定小数点型
// ========================================================================
//
// 組み込み環境への移植を見据え、浮動小数点を排除するための固定小数点型。
// 変数名にはサフィックスを付けず、型名で意図を明確にする。
//

// ------------------------------------------------------------------------
// Q24.8 固定小数点（座標用）
// ------------------------------------------------------------------------
// 整数部: 24bit (-8,388,608 ~ 8,388,607)
// 小数部: 8bit (精度 1/256 = 0.00390625)
// 用途: origin座標、基準点位置など

using int_fixed8 = int32_t;

constexpr int INT_FIXED8_SHIFT = 8;
constexpr int_fixed8 INT_FIXED8_ONE = 1 << INT_FIXED8_SHIFT;  // 256
constexpr int_fixed8 INT_FIXED8_HALF = 1 << (INT_FIXED8_SHIFT - 1);  // 128

// ------------------------------------------------------------------------
// Q16.16 固定小数点（行列用）
// ------------------------------------------------------------------------
// 整数部: 16bit (-32,768 ~ 32,767)
// 小数部: 16bit (精度 1/65536 = 0.0000152587890625)
// 用途: アフィン変換行列の要素

using int_fixed16 = int32_t;

constexpr int INT_FIXED16_SHIFT = 16;
constexpr int_fixed16 INT_FIXED16_ONE = 1 << INT_FIXED16_SHIFT;  // 65536
constexpr int_fixed16 INT_FIXED16_HALF = 1 << (INT_FIXED16_SHIFT - 1);  // 32768


// ========================================================================
// 2x2 行列テンプレート
// ========================================================================
//
// アフィン変換の回転/スケール成分を表す 2x2 行列。
// 平行移動成分(tx,ty)は含まず、別途管理する。
//
// テンプレート引数で精度を指定:
// - Matrix2x2<int_fixed16>: Q16.16 固定小数点（DDA用）
// - Matrix2x2<float>: 浮動小数点（互換用）
//
// 逆行列か順行列かは変数名で区別する:
// - invMatrix_: 逆行列
// - matrix_: 順行列
//

template<typename T>
struct Matrix2x2 {
    T a, b, c, d;
    bool valid = false;

    Matrix2x2() : a(0), b(0), c(0), d(0), valid(false) {}
    Matrix2x2(T a_, T b_, T c_, T d_, bool v = true)
        : a(a_), b(b_), c(c_), d(d_), valid(v) {}
};

// 精度別エイリアス
using Matrix2x2_fixed16 = Matrix2x2<int_fixed16>;


// ========================================================================
// Point - 2D座標構造体（固定小数点 Q24.8）
// ========================================================================

struct Point {
    int_fixed8 x = 0;
    int_fixed8 y = 0;

    Point() = default;
    Point(int_fixed8 x_, int_fixed8 y_) : x(x_), y(y_) {}

    Point operator+(const Point& o) const { return {x + o.x, y + o.y}; }
    Point operator-(const Point& o) const { return {x - o.x, y - o.y}; }
    Point operator-() const { return {-x, -y}; }
    Point& operator+=(const Point& o) { x += o.x; y += o.y; return *this; }
    Point& operator-=(const Point& o) { x -= o.x; y -= o.y; return *this; }
};


// ========================================================================
// 変換関数
// ========================================================================

// ------------------------------------------------------------------------
// int ↔ fixed8 変換
// ------------------------------------------------------------------------

// int → fixed8
constexpr int_fixed8 to_fixed8(int v) {
    return static_cast<int_fixed8>(v) << INT_FIXED8_SHIFT;
}

// fixed8 → int (0方向への切り捨て)
constexpr int from_fixed8(int_fixed8 v) {
    return static_cast<int>(v >> INT_FIXED8_SHIFT);
}

// fixed8 → int (四捨五入)
constexpr int from_fixed8_round(int_fixed8 v) {
    return static_cast<int>((v + INT_FIXED8_HALF) >> INT_FIXED8_SHIFT);
}

// fixed8 → int (負の無限大方向への切り捨て = floor)
constexpr int from_fixed8_floor(int_fixed8 v) {
    // 負の値の場合、単純な右シフトでは0方向に切り捨てられるため調整
    return (v >= 0) ? (v >> INT_FIXED8_SHIFT)
                    : -(((-v) + INT_FIXED8_ONE - 1) >> INT_FIXED8_SHIFT);
}

// fixed8 → int (正の無限大方向への切り上げ = ceil)
constexpr int from_fixed8_ceil(int_fixed8 v) {
    // 正の値の場合、小数部があれば切り上げ
    // 負の値の場合、単純な右シフトで0方向に切り捨て = 負方向から見ると切り上げ
    return (v >= 0) ? ((v + INT_FIXED8_ONE - 1) >> INT_FIXED8_SHIFT)
                    : -((-v) >> INT_FIXED8_SHIFT);
}

// ------------------------------------------------------------------------
// int ↔ fixed16 変換
// ------------------------------------------------------------------------

// int → fixed16
constexpr int_fixed16 to_fixed16(int v) {
    return static_cast<int_fixed16>(v) << INT_FIXED16_SHIFT;
}

// fixed16 → int (0方向への切り捨て)
constexpr int from_fixed16(int_fixed16 v) {
    return static_cast<int>(v >> INT_FIXED16_SHIFT);
}

// fixed16 → int (四捨五入)
constexpr int from_fixed16_round(int_fixed16 v) {
    return static_cast<int>((v + INT_FIXED16_HALF) >> INT_FIXED16_SHIFT);
}

// ------------------------------------------------------------------------
// float ↔ fixed8 変換
// ------------------------------------------------------------------------

// float → fixed8
constexpr int_fixed8 float_to_fixed8(float v) {
    return static_cast<int_fixed8>(v * INT_FIXED8_ONE);
}

// fixed8 → float
constexpr float fixed8_to_float(int_fixed8 v) {
    return static_cast<float>(v) / INT_FIXED8_ONE;
}


// ========================================================================
// 固定小数点演算ヘルパー
// ========================================================================

// fixed8 同士の乗算 (結果も fixed8)
constexpr int_fixed8 mul_fixed8(int_fixed8 a, int_fixed8 b) {
    return static_cast<int_fixed8>((static_cast<int64_t>(a) * b) >> INT_FIXED8_SHIFT);
}

// fixed8 同士の除算 (結果も fixed8)
constexpr int_fixed8 div_fixed8(int_fixed8 a, int_fixed8 b) {
    return static_cast<int_fixed8>((static_cast<int64_t>(a) << INT_FIXED8_SHIFT) / b);
}

// fixed16 同士の乗算 (結果も fixed16)
constexpr int_fixed16 mul_fixed16(int_fixed16 a, int_fixed16 b) {
    return static_cast<int_fixed16>((static_cast<int64_t>(a) * b) >> INT_FIXED16_SHIFT);
}

// fixed16 同士の除算 (結果も fixed16)
constexpr int_fixed16 div_fixed16(int_fixed16 a, int_fixed16 b) {
    return static_cast<int_fixed16>((static_cast<int64_t>(a) << INT_FIXED16_SHIFT) / b);
}


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

    // 行列の乗算（合成）: this * other
    AffineMatrix operator*(const AffineMatrix& other) const {
        return AffineMatrix(
            a * other.a + b * other.c,           // a
            a * other.b + b * other.d,           // b
            c * other.a + d * other.c,           // c
            c * other.b + d * other.d,           // d
            a * other.tx + b * other.ty + tx,    // tx
            c * other.tx + d * other.ty + ty     // ty
        );
    }
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


// ========================================================================
// AffinePrecomputed - アフィン変換の事前計算結果
// ========================================================================
//
// SourceNode/SinkNode でのDDA処理に必要な事前計算値をまとめた構造体。
// 逆行列とピクセル中心オフセットを保持する。
// baseTx/baseTy は呼び出し側で origin に応じて計算する。
//

struct AffinePrecomputed {
    Matrix2x2_fixed16 invMatrix;  // 逆行列（2x2部分）
    int32_t invTxFixed = 0;       // 逆変換オフセットX（Q16.16）
    int32_t invTyFixed = 0;       // 逆変換オフセットY（Q16.16）
    int32_t rowOffsetX = 0;       // ピクセル中心オフセット: invMatrix.b >> 1
    int32_t rowOffsetY = 0;       // ピクセル中心オフセット: invMatrix.d >> 1
    int32_t dxOffsetX = 0;        // ピクセル中心オフセット: invMatrix.a >> 1
    int32_t dxOffsetY = 0;        // ピクセル中心オフセット: invMatrix.c >> 1

    bool isValid() const { return invMatrix.valid; }
};

// アフィン行列から事前計算値を生成
// 逆行列、逆変換オフセット、ピクセル中心オフセットを計算
inline AffinePrecomputed precomputeInverseAffine(const AffineMatrix& m) {
    AffinePrecomputed result;

    // 逆行列を計算
    result.invMatrix = inverseFixed16(m);
    if (!result.invMatrix.valid) {
        return result;  // 特異行列の場合は無効な結果を返す
    }

    // tx/ty を Q24.8 固定小数点に変換
    int_fixed8 txFixed8 = float_to_fixed8(m.tx);
    int_fixed8 tyFixed8 = float_to_fixed8(m.ty);

    // 逆変換オフセットの計算（tx/ty と逆行列から）
    int64_t invTx64 = -(static_cast<int64_t>(txFixed8) * result.invMatrix.a
                      + static_cast<int64_t>(tyFixed8) * result.invMatrix.b);
    int64_t invTy64 = -(static_cast<int64_t>(txFixed8) * result.invMatrix.c
                      + static_cast<int64_t>(tyFixed8) * result.invMatrix.d);
    result.invTxFixed = static_cast<int32_t>(invTx64 >> INT_FIXED8_SHIFT);
    result.invTyFixed = static_cast<int32_t>(invTy64 >> INT_FIXED8_SHIFT);

    // ピクセル中心オフセット
    result.rowOffsetX = result.invMatrix.b >> 1;
    result.rowOffsetY = result.invMatrix.d >> 1;
    result.dxOffsetX = result.invMatrix.a >> 1;
    result.dxOffsetY = result.invMatrix.c >> 1;

    return result;
}

} // namespace core

// [DEPRECATED] 後方互換性のため親名前空間に公開。将来廃止予定。
// 新規コードでは core:: プレフィックスを使用してください。
using core::int_fixed8;
using core::int_fixed16;
using core::INT_FIXED8_SHIFT;
using core::INT_FIXED8_ONE;
using core::INT_FIXED8_HALF;
using core::INT_FIXED16_SHIFT;
using core::INT_FIXED16_ONE;
using core::INT_FIXED16_HALF;
using core::Matrix2x2;
using core::Matrix2x2_fixed16;
using core::Point;
using core::to_fixed8;
using core::from_fixed8;
using core::from_fixed8_round;
using core::from_fixed8_floor;
using core::from_fixed8_ceil;
using core::to_fixed16;
using core::from_fixed16;
using core::from_fixed16_round;
using core::float_to_fixed8;
using core::fixed8_to_float;
using core::mul_fixed8;
using core::div_fixed8;
using core::mul_fixed16;
using core::div_fixed16;
using core::AffineMatrix;
using core::toFixed16;
using core::inverseFixed16;
using core::AffinePrecomputed;
using core::precomputeInverseAffine;

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_TYPES_H
