#ifndef FLEXIMG_OPERATIONS_FILTERS_H
#define FLEXIMG_OPERATIONS_FILTERS_H

#include "../core/common.h"
#include "../image/viewport.h"

namespace FLEXIMG_NAMESPACE {
namespace filters {

// ========================================================================
// ラインフィルタ共通定義
// ========================================================================
//
// スキャンライン処理用の共通パラメータ構造体と関数型です。
// FilterNodeBase で使用され、派生クラスの共通化を実現します。
//

/// ラインフィルタ共通パラメータ
struct LineFilterParams {
    float value1 = 0.0f;  ///< brightness amount, alpha scale 等
    float value2 = 0.0f;  ///< 将来の拡張用
};

/// ラインフィルタ関数型（RGBA8_Straight形式、インプレース処理）
using LineFilterFunc = void(*)(uint8_t* pixels, int count, const LineFilterParams& params);

// ========================================================================
// ラインフィルタ関数（スキャンライン処理用）
// ========================================================================
//
// 1行分のピクセルデータ（RGBA8_Straight形式）を処理します。
// インプレース編集を前提とし、入力と出力は同一バッファです。
//

/// 明るさ調整（ラインフィルタ版）
/// params.value1: 明るさ調整量（-1.0〜1.0、0.5で+127相当）
void brightness_line(uint8_t* pixels, int count, const LineFilterParams& params);

/// グレースケール変換（ラインフィルタ版）
/// パラメータ未使用（将来の拡張用に引数は維持）
void grayscale_line(uint8_t* pixels, int count, const LineFilterParams& params);

/// アルファ調整（ラインフィルタ版）
/// params.value1: アルファスケール（0.0〜1.0）
void alpha_line(uint8_t* pixels, int count, const LineFilterParams& params);

// ========================================================================
// フィルタ操作（ViewPort版、移行期間中は維持）
// ========================================================================
//
// 画像に対するフィルタ処理を行います。
// 入力と出力は同じサイズ・フォーマットを前提とします。
// 8bit RGBA (Straight) 形式で処理します。
//

// ------------------------------------------------------------------------
// brightness - 明るさ調整
// ------------------------------------------------------------------------
//
// RGBチャンネルに一定値を加算します。
//
// パラメータ:
// - dst: 出力バッファ（事前確保済み）
// - src: 入力バッファ
// - amount: 明るさ調整量（-1.0〜1.0、0.5で+127相当）
//
void brightness(ViewPort& dst, const ViewPort& src, float amount);

// ------------------------------------------------------------------------
// grayscale - グレースケール変換
// ------------------------------------------------------------------------
//
// RGB平均法によるグレースケール変換を行います。
//
void grayscale(ViewPort& dst, const ViewPort& src);

// ------------------------------------------------------------------------
// boxBlur - ボックスブラー
// ------------------------------------------------------------------------
//
// 入力画像の範囲外を透明（α=0）として扱うボックスブラーです。
// α加重平均を使用し、透明ピクセルの色成分が結果に影響しないようにします。
// スライディングウィンドウ方式で O(width×height) の計算量を実現。
//
// パラメータ:
// - dst: 出力バッファ（事前確保済み、srcと異なるサイズ可）
// - src: 入力バッファ
// - radius: ブラー半径（ピクセル）
// - srcOffsetX: srcのdst座標系でのX方向オフセット（デフォルト0）
// - srcOffsetY: srcのdst座標系でのY方向オフセット（デフォルト0）
//
// 座標系:
//   dst座標 (dx, dy) に対応する src座標は (dx - srcOffsetX, dy - srcOffsetY)
//   src範囲外のピクセルは透明として扱われる
//
void boxBlur(ViewPort& dst, const ViewPort& src, int radius,
             int srcOffsetX = 0, int srcOffsetY = 0);

// ------------------------------------------------------------------------
// alpha - アルファ調整
// ------------------------------------------------------------------------
//
// アルファチャンネルにスケール係数を乗算します。
//
// パラメータ:
// - dst: 出力バッファ（事前確保済み）
// - src: 入力バッファ
// - scale: アルファスケール（0.0〜1.0）
//
void alpha(ViewPort& dst, const ViewPort& src, float scale);

} // namespace filters
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_OPERATIONS_FILTERS_H
