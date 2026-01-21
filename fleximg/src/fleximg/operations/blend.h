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
// - dstOrigin: dstバッファ内での基準点位置（固定小数点 Q16.16）
// - srcOrigin: srcバッファ内での基準点位置（固定小数点 Q16.16）
//
// 両者の基準点を一致させて合成します。
//

// [DEPRECATED] 将来削除予定
// canvas_utils::placeFirst を使用してください。
// PixelFormatDescriptor の変換関数（toPremul等）を直接使用する実装に移行しました。
#if 0
// ------------------------------------------------------------------------
// first - 透明キャンバスへの最初の描画（memcpy最適化）
// ------------------------------------------------------------------------
//
// 宛先が透明であることを前提とし、単純コピーで合成します。
//
void first(ViewPort& dst, int_fixed dstOriginX, int_fixed dstOriginY,
           const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY);
#endif

// [DEPRECATED] 将来削除予定
// over合成はunder合成に統一されたため、この関数は不要になりました。
// 参照: canvas_utils::placeUnder, PixelFormatDescriptor::blendUnderPremul
#if 0
// ------------------------------------------------------------------------
// onto - 既存画像への合成（アルファブレンド）
// ------------------------------------------------------------------------
//
// プリマルチプライド形式でのアルファブレンド合成を行います。
//
void onto(ViewPort& dst, int_fixed dstOriginX, int_fixed dstOriginY,
          const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY);
#endif

} // namespace blend
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_OPERATIONS_BLEND_H
