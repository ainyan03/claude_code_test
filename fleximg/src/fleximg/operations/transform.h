#ifndef FLEXIMG_OPERATIONS_TRANSFORM_H
#define FLEXIMG_OPERATIONS_TRANSFORM_H

#include "../common.h"
#include "../viewport.h"

namespace FLEXIMG_NAMESPACE {
namespace transform {

// ========================================================================
// アフィン変換（純関数）
// ========================================================================
//
// 基準点座標系でのアフィン変換を行います。
//
// 座標系の考え方:
// - 各バッファは「基準点からの相対座標」で管理される
// - dstOrigin: dstバッファ内での基準点位置
// - srcOrigin: srcバッファ内での基準点位置
// - matrix: 基準点を中心とした変換行列
//
// 変換の流れ:
// 1. dstの各ピクセル座標を基準点相対座標に変換
// 2. 逆行列で入力の基準点相対座標を計算
// 3. srcバッファ座標に変換してサンプリング
//

// ------------------------------------------------------------------------
// affine - アフィン変換
// ------------------------------------------------------------------------
//
// 入力画像に対してアフィン変換を適用し、出力バッファに書き込みます。
// 出力バッファは事前にゼロクリアされていることを前提とします。
//
// パラメータ:
// - dst: 出力バッファ（事前確保、ゼロクリア済み）
// - dstOriginX/Y: 出力バッファ内での基準点位置
// - src: 入力バッファ
// - srcOriginX/Y: 入力バッファ内での基準点位置
// - matrix: アフィン変換行列（回転・拡縮・平行移動）
//
void affine(ViewPort& dst, float dstOriginX, float dstOriginY,
            const ViewPort& src, float srcOriginX, float srcOriginY,
            const AffineMatrix& matrix);

} // namespace transform
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_OPERATIONS_TRANSFORM_H
