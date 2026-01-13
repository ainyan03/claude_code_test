#ifndef FLEXIMG_OPERATIONS_BLEND_H
#define FLEXIMG_OPERATIONS_BLEND_H

#include "../core/common.h"
#include "../core/types.h"
#include "../image/viewport.h"

namespace FLEXIMG_NAMESPACE {
namespace blend {

// ========================================================================
// ブレンド操作（純関数）
// ========================================================================
//
// 基準点座標系での合成処理を行います。
// - dstOrigin: dstバッファ内での基準点位置（固定小数点 Q24.8）
// - srcOrigin: srcバッファ内での基準点位置（固定小数点 Q24.8）
//
// 両者の基準点を一致させて合成します。
//

// ------------------------------------------------------------------------
// first - 透明キャンバスへの最初の描画（memcpy最適化）
// ------------------------------------------------------------------------
//
// 宛先が透明であることを前提とし、単純コピーで合成します。
//
void first(ViewPort& dst, int_fixed8 dstOriginX, int_fixed8 dstOriginY,
           const ViewPort& src, int_fixed8 srcOriginX, int_fixed8 srcOriginY);

// ------------------------------------------------------------------------
// onto - 既存画像への合成（アルファブレンド）
// ------------------------------------------------------------------------
//
// プリマルチプライド形式でのアルファブレンド合成を行います。
//
void onto(ViewPort& dst, int_fixed8 dstOriginX, int_fixed8 dstOriginY,
          const ViewPort& src, int_fixed8 srcOriginX, int_fixed8 srcOriginY);

} // namespace blend
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_OPERATIONS_BLEND_H
