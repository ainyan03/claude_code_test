#ifndef FLEXIMG_TYPES_H
#define FLEXIMG_TYPES_H

#include <cstdint>

namespace FLEXIMG_NAMESPACE {

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
// float ↔ fixed 変換（移行期間用、最終的に削除予定）
// ------------------------------------------------------------------------

// float → fixed8
constexpr int_fixed8 float_to_fixed8(float v) {
    return static_cast<int_fixed8>(v * INT_FIXED8_ONE);
}

// fixed8 → float
constexpr float fixed8_to_float(int_fixed8 v) {
    return static_cast<float>(v) / INT_FIXED8_ONE;
}

// float → fixed16
constexpr int_fixed16 float_to_fixed16(float v) {
    return static_cast<int_fixed16>(v * INT_FIXED16_ONE);
}

// fixed16 → float
constexpr float fixed16_to_float(int_fixed16 v) {
    return static_cast<float>(v) / INT_FIXED16_ONE;
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

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_TYPES_H
