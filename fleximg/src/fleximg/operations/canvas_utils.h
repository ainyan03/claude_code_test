#ifndef FLEXIMG_CANVAS_UTILS_H
#define FLEXIMG_CANVAS_UTILS_H

#include "../image/image_buffer.h"
#include "../image/render_types.h"
#include "../operations/blend.h"

namespace FLEXIMG_NAMESPACE {
namespace canvas_utils {

// ========================================================================
// キャンバスユーティリティ
// ========================================================================
// CompositeNode や NinePatchSourceNode など、複数の画像を合成するノードで
// 共通して使用するキャンバス操作をまとめたユーティリティ関数群。
//

// キャンバス作成（RGBA16_Premultiplied）
// 合成処理に適したフォーマットでバッファを確保
inline ImageBuffer createCanvas(int width, int height) {
    return ImageBuffer(width, height, PixelFormatIDs::RGBA16_Premultiplied);
}

// 最初の画像をキャンバスに配置
// blend::first を使用（透明キャンバスへの最初の描画、memcpy最適化）
inline void placeFirst(ViewPort& canvas, int_fixed8 canvasOriginX, int_fixed8 canvasOriginY,
                       const ViewPort& src, int_fixed8 srcOriginX, int_fixed8 srcOriginY) {
    blend::first(canvas, canvasOriginX, canvasOriginY, src, srcOriginX, srcOriginY);
}

// 追加画像をキャンバスに配置（ブレンド）
// blend::onto を使用（2枚目以降の合成、ブレンド計算）
inline void placeOnto(ViewPort& canvas, int_fixed8 canvasOriginX, int_fixed8 canvasOriginY,
                      const ViewPort& src, int_fixed8 srcOriginX, int_fixed8 srcOriginY) {
    blend::onto(canvas, canvasOriginX, canvasOriginY, src, srcOriginX, srcOriginY);
}

// フォーマット変換（必要なら）
// blend関数が対応していないフォーマットをRGBA16_Premultipliedに変換
// 対応フォーマット: RGBA8_Straight, RGBA16_Premultiplied
inline RenderResult ensureBlendableFormat(RenderResult&& input) {
    if (!input.isValid()) {
        return std::move(input);
    }

    PixelFormatID inputFmt = input.view().formatID;
    if (inputFmt == PixelFormatIDs::RGBA8_Straight ||
        inputFmt == PixelFormatIDs::RGBA16_Premultiplied) {
        // 対応フォーマットならそのまま返す
        return std::move(input);
    }

    // RGBA16_Premultiplied に変換
    Point savedOrigin = input.origin;
    return RenderResult(
        std::move(input.buffer).toFormat(PixelFormatIDs::RGBA16_Premultiplied),
        savedOrigin
    );
}

} // namespace canvas_utils
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_CANVAS_UTILS_H
