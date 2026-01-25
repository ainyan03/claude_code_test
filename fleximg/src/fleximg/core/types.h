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
// Q16.16 固定小数点
// ------------------------------------------------------------------------
// 整数部: 16bit (-32,768 ~ 32,767)
// 小数部: 16bit (精度 1/65536 = 0.0000152587890625)
// 用途: アフィン変換行列の要素

using int_fixed = int32_t;

constexpr int INT_FIXED_SHIFT = 16;
constexpr int_fixed INT_FIXED_ONE = 1 << INT_FIXED_SHIFT;  // 65536
constexpr int_fixed INT_FIXED_HALF = 1 << (INT_FIXED_SHIFT - 1);  // 32768


// ========================================================================
// 2x2 行列テンプレート
// ========================================================================
//
// アフィン変換の回転/スケール成分を表す 2x2 行列。
// 平行移動成分(tx,ty)は含まず、別途管理する。
//
// テンプレート引数で精度を指定:
// - Matrix2x2<int_fixed>: Q16.16 固定小数点（DDA用）
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
using Matrix2x2_fixed = Matrix2x2<int_fixed>;


// ========================================================================
// Point - 2D座標構造体（固定小数点 Q16.16）
// ========================================================================

struct Point {
    int_fixed x = 0;
    int_fixed y = 0;

    Point() = default;
    Point(int_fixed x_, int_fixed y_) : x(x_), y(y_) {}

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
// int ↔ fixed 変換
// ------------------------------------------------------------------------

// int → fixed
constexpr int_fixed to_fixed(int v) {
    return static_cast<int_fixed>(v) << INT_FIXED_SHIFT;
}

// fixed → int (floor: 負の無限大方向への丸め)
// 算術右シフトにより、常に負の無限大方向へ丸められる
// 例: 10.7 → 10, 10.3 → 10, -10.3 → -11, -10.7 → -11
constexpr int from_fixed_floor(int_fixed v) {
    return v >> INT_FIXED_SHIFT;
}

// fixed → int (ceil: 正の無限大方向への丸め)
// 例: 10.3 → 11, 10.0 → 10, -10.7 → -10, -10.0 → -10
constexpr int from_fixed_ceil(int_fixed v) {
    return (v + INT_FIXED_ONE - 1) >> INT_FIXED_SHIFT;
}

// fixed → int (round: 四捨五入、round half up)
// 0.5以上で切り上げ、0.5未満で切り捨て
// 例: 10.5 → 11, 10.4 → 10, -10.4 → -10, -10.5 → -10, -10.6 → -11
constexpr int from_fixed_round(int_fixed v) {
    return (v + INT_FIXED_HALF) >> INT_FIXED_SHIFT;
}

// 互換性のためのエイリアス（from_fixed_floor と同じ）
constexpr int from_fixed(int_fixed v) {
    return from_fixed_floor(v);
}

// ------------------------------------------------------------------------
// float ↔ fixed 変換
// ------------------------------------------------------------------------

// float → fixed
constexpr int_fixed float_to_fixed(float v) {
    return static_cast<int_fixed>(v * INT_FIXED_ONE);
}

// fixed → float
constexpr float fixed_to_float(int_fixed v) {
    return static_cast<float>(v) / INT_FIXED_ONE;
}

// ========================================================================
// 固定小数点演算ヘルパー
// ========================================================================

// fixed 同士の乗算 (結果も fixed)
constexpr int_fixed mul_fixed(int_fixed a, int_fixed b) {
    return static_cast<int_fixed>((static_cast<int64_t>(a) * b) >> INT_FIXED_SHIFT);
}

// fixed 同士の除算 (結果も fixed)
constexpr int_fixed div_fixed(int_fixed a, int_fixed b) {
    return static_cast<int_fixed>((static_cast<int64_t>(a) << INT_FIXED_SHIFT) / b);
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
inline Matrix2x2_fixed toFixed(const AffineMatrix& m) {
    return Matrix2x2_fixed(
        static_cast<int_fixed>(std::lround(m.a * INT_FIXED_ONE)),
        static_cast<int_fixed>(std::lround(m.b * INT_FIXED_ONE)),
        static_cast<int_fixed>(std::lround(m.c * INT_FIXED_ONE)),
        static_cast<int_fixed>(std::lround(m.d * INT_FIXED_ONE)),
        true  // valid
    );
}

// AffineMatrix の 2x2 部分の逆行列を固定小数点で返す（逆変換用）
// 平行移動成分(tx,ty)は含まない（呼び出し側で別途管理）
inline Matrix2x2_fixed inverseFixed(const AffineMatrix& m) {
    float det = m.a * m.d - m.b * m.c;
    if (std::abs(det) < 1e-10f) {
        return Matrix2x2_fixed();  // valid = false
    }

    float invDet = 1.0f / det;
    return Matrix2x2_fixed(
        static_cast<int_fixed>(std::lround(m.d * invDet * INT_FIXED_ONE)),
        static_cast<int_fixed>(std::lround(-m.b * invDet * INT_FIXED_ONE)),
        static_cast<int_fixed>(std::lround(-m.c * invDet * INT_FIXED_ONE)),
        static_cast<int_fixed>(std::lround(m.a * invDet * INT_FIXED_ONE)),
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
    Matrix2x2_fixed invMatrix;  // 逆行列（2x2部分）
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
    result.invMatrix = inverseFixed(m);
    if (!result.invMatrix.valid) {
        return result;  // 特異行列の場合は無効な結果を返す
    }

    // tx/ty を Q16.16 固定小数点に変換
    int_fixed txFixed = float_to_fixed(m.tx);
    int_fixed tyFixed = float_to_fixed(m.ty);

    // 逆変換オフセットの計算（tx/ty と逆行列から）
    // Q16.16 × Q16.16 = Q32、16bit シフトで Q16.16
    int64_t invTx64 = -(static_cast<int64_t>(txFixed) * result.invMatrix.a
                      + static_cast<int64_t>(tyFixed) * result.invMatrix.b);
    int64_t invTy64 = -(static_cast<int64_t>(txFixed) * result.invMatrix.c
                      + static_cast<int64_t>(tyFixed) * result.invMatrix.d);
    result.invTxFixed = static_cast<int32_t>(invTx64 >> INT_FIXED_SHIFT);
    result.invTyFixed = static_cast<int32_t>(invTy64 >> INT_FIXED_SHIFT);

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
using core::int_fixed;
using core::int_fixed;
using core::INT_FIXED_SHIFT;
using core::INT_FIXED_ONE;
using core::INT_FIXED_HALF;
using core::Matrix2x2;
using core::Matrix2x2_fixed;
using core::Point;
using core::to_fixed;
using core::from_fixed;
using core::from_fixed_round;
using core::from_fixed_floor;
using core::from_fixed_ceil;
using core::float_to_fixed;
using core::fixed_to_float;
using core::mul_fixed;
using core::div_fixed;
using core::AffineMatrix;
using core::toFixed;
using core::inverseFixed;
using core::AffinePrecomputed;
using core::precomputeInverseAffine;

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_TYPES_H
