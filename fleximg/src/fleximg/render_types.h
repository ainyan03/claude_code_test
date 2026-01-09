#ifndef FLEXIMG_RENDER_TYPES_H
#define FLEXIMG_RENDER_TYPES_H

#include <utility>
#include "common.h"
#include "image_buffer.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// TileConfig - タイル分割設定
// ========================================================================

struct TileConfig {
    int tileWidth = 0;   // 0 = 分割なし
    int tileHeight = 0;

    TileConfig() = default;
    TileConfig(int w, int h) : tileWidth(w), tileHeight(h) {}

    bool isEnabled() const { return tileWidth > 0 && tileHeight > 0; }
};

// ========================================================================
// RenderRequest - 部分矩形要求
// ========================================================================

struct RenderRequest {
    int width = 0;
    int height = 0;
    float originX = 0;  // バッファ内での基準点X位置
    float originY = 0;  // バッファ内での基準点Y位置

    bool isEmpty() const { return width <= 0 || height <= 0; }

    // マージン分拡大（フィルタ用）
    RenderRequest expand(int margin) const {
        return {
            width + margin * 2, height + margin * 2,
            originX + margin, originY + margin
        };
    }
};

// ========================================================================
// RenderContext - レンダリングコンテキスト
// ========================================================================

struct RenderContext {
    int canvasWidth = 0;
    int canvasHeight = 0;
    float originX = 0;  // 出力先基準点X
    float originY = 0;  // 出力先基準点Y

    TileConfig tileConfig;

    // 現在のタイル情報
    int tileX = 0;
    int tileY = 0;

    // タイルサイズ取得
    int effectiveTileWidth() const {
        return tileConfig.isEnabled() ? tileConfig.tileWidth : canvasWidth;
    }

    int effectiveTileHeight() const {
        return tileConfig.isEnabled() ? tileConfig.tileHeight : canvasHeight;
    }

    // タイル数取得
    int tileCountX() const {
        int tw = effectiveTileWidth();
        return (tw > 0) ? (canvasWidth + tw - 1) / tw : 1;
    }

    int tileCountY() const {
        int th = effectiveTileHeight();
        return (th > 0) ? (canvasHeight + th - 1) / th : 1;
    }
};

// ========================================================================
// RenderResult - 評価結果
// ========================================================================

struct RenderResult {
    ImageBuffer buffer;
    Point2f origin;  // 基準点からの相対座標（画像左上の位置）

    RenderResult() = default;

    RenderResult(ImageBuffer&& buf, Point2f org)
        : buffer(std::move(buf)), origin(org) {}

    RenderResult(ImageBuffer&& buf, float ox, float oy)
        : buffer(std::move(buf)), origin(ox, oy) {}

    // ムーブのみ
    RenderResult(const RenderResult&) = delete;
    RenderResult& operator=(const RenderResult&) = delete;
    RenderResult(RenderResult&&) = default;
    RenderResult& operator=(RenderResult&&) = default;

    bool isValid() const { return buffer.isValid(); }
    ViewPort view() { return buffer.view(); }
    ViewPort view() const { return buffer.view(); }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_RENDER_TYPES_H
