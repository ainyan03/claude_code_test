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

// ========================================================================
// RGBA8_Straight 形式のキャンバス操作（既存、over合成用）
// ========================================================================

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

// ========================================================================
// RGBA16_Premultiplied 形式のキャンバス操作（under合成用）
// ========================================================================
// under合成: 手前のレイヤーから順に処理し、dstが不透明なら何もしない
// メリット:
// - 不透明ピクセルで覆われた部分の変換・ブレンド計算をスキップ
// - SIMD最適化しやすい16bit演算
// - 中間計算の精度維持

// Premul形式のキャンバス作成（RGBA16_Premultiplied形式）
// under合成に最適化されたフォーマットでバッファを確保
inline ImageBuffer createPremulCanvas(int width, int height,
                                      InitPolicy init = InitPolicy::Zero) {
    return ImageBuffer(width, height, PixelFormatIDs::RGBA16_Premultiplied, init);
}

// under合成でレイヤーを配置
// canvas: RGBA16_Premultiplied形式のキャンバス
// src: blendUnderPremul関数を持つフォーマットの入力画像
// 動作:
//   - dst が不透明 → スキップ（変換すら不要）
//   - dst が透明 → 単純変換コピー
//   - dst が半透明 → under合成
inline void placeUnder(ViewPort& canvas, int_fixed canvasOriginX, int_fixed canvasOriginY,
                       const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY) {
    if (!canvas.isValid() || !src.isValid()) return;
    if (canvas.formatID != PixelFormatIDs::RGBA16_Premultiplied) return;

    // srcフォーマットのblendUnderPremul関数を取得
    auto blendFunc = src.formatID->blendUnderPremul;
    if (!blendFunc) return;

    // 基準点を一致させるためのオフセット計算
    int offsetX = from_fixed(canvasOriginX - srcOriginX);
    int offsetY = from_fixed(canvasOriginY - srcOriginY);

    // クリッピング範囲を計算
    int srcStartX = std::max(0, -offsetX);
    int srcStartY = std::max(0, -offsetY);
    int dstStartX = std::max(0, offsetX);
    int dstStartY = std::max(0, offsetY);
    int copyWidth = std::min(src.width - srcStartX, canvas.width - dstStartX);
    int copyHeight = std::min(src.height - srcStartY, canvas.height - dstStartY);

    if (copyWidth <= 0 || copyHeight <= 0) return;

    // 行ごとにunder合成
    for (int y = 0; y < copyHeight; y++) {
        const void* srcRow = src.pixelAt(srcStartX, srcStartY + y);
        void* dstRow = canvas.pixelAt(dstStartX, dstStartY + y);
        blendFunc(dstRow, srcRow, copyWidth, nullptr);
    }
}

// Premulキャンバスを最終出力フォーマット（RGBA8_Straight）に変換
inline ImageBuffer finalizePremulCanvas(ImageBuffer&& canvas) {
    if (!canvas.isValid()) return std::move(canvas);
    if (canvas.formatID() == PixelFormatIDs::RGBA8_Straight) return std::move(canvas);

    return std::move(canvas).toFormat(PixelFormatIDs::RGBA8_Straight);
}

} // namespace canvas_utils
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_CANVAS_UTILS_H
