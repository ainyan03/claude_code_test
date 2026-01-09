#ifndef FLEXIMG_OPERATIONS_BLEND_H
#define FLEXIMG_OPERATIONS_BLEND_H

#include "../common.h"
#include "../viewport.h"

namespace FLEXIMG_NAMESPACE {
namespace blend {

// ========================================================================
// ブレンド操作（純関数）
// ========================================================================
//
// 基準点座標系での合成処理を行います。
// - dstOrigin: dstバッファ内での基準点位置
// - srcOrigin: srcバッファ内での基準点位置
//
// 両者の基準点を一致させて合成します。
//

// ------------------------------------------------------------------------
// first - 透明キャンバスへの最初の描画（memcpy最適化）
// ------------------------------------------------------------------------
//
// 宛先が透明であることを前提とし、単純コピーで合成します。
//
void first(ViewPort& dst, float dstOriginX, float dstOriginY,
           const ViewPort& src, float srcOriginX, float srcOriginY);

// ------------------------------------------------------------------------
// onto - 既存画像への合成（アルファブレンド）
// ------------------------------------------------------------------------
//
// プリマルチプライド形式でのアルファブレンド合成を行います。
//
void onto(ViewPort& dst, float dstOriginX, float dstOriginY,
          const ViewPort& src, float srcOriginX, float srcOriginY);

} // namespace blend
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_OPERATIONS_BLEND_H
