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

// キャンバス作成（RGBA8_Straight形式）
// 合成処理に適したフォーマットでバッファを確保
// init: 初期化ポリシー（デフォルトはDefaultInitPolicy）
//   - 全面を画像で埋める場合: DefaultInitPolicy（初期化スキップ可）
//   - 部分的な描画の場合: InitPolicy::Zero（透明で初期化）
inline ImageBuffer createCanvas(int width, int height,
                                InitPolicy init = DefaultInitPolicy) {
    return ImageBuffer(width, height, PixelFormatIDs::RGBA8_Straight, init);
}

// 最初の画像をキャンバスに配置
// blend::first を使用（透明キャンバスへの最初の描画、memcpy最適化）
inline void placeFirst(ViewPort& canvas, int_fixed canvasOriginX, int_fixed canvasOriginY,
                       const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY) {
    blend::first(canvas, canvasOriginX, canvasOriginY, src, srcOriginX, srcOriginY);
}

// 追加画像をキャンバスに配置（ブレンド）
// blend::onto を使用（2枚目以降の合成、ブレンド計算）
inline void placeOnto(ViewPort& canvas, int_fixed canvasOriginX, int_fixed canvasOriginY,
                      const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY) {
    blend::onto(canvas, canvasOriginX, canvasOriginY, src, srcOriginX, srcOriginY);
}

// フォーマット変換（必要なら）
// blend関数が対応していないフォーマットをRGBA8_Straightに変換
inline RenderResult ensureBlendableFormat(RenderResult&& input) {
    if (!input.isValid()) {
        return std::move(input);
    }

    PixelFormatID inputFmt = input.view().formatID;
    if (inputFmt == PixelFormatIDs::RGBA8_Straight) {
        // 対応フォーマットならそのまま返す
        return std::move(input);
    }

    // RGBA8_Straight に変換
    Point savedOrigin = input.origin;
    return RenderResult(
        std::move(input.buffer).toFormat(PixelFormatIDs::RGBA8_Straight),
        savedOrigin
    );
}

} // namespace canvas_utils
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_CANVAS_UTILS_H
