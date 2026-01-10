#ifndef FLEXIMG_RENDER_TYPES_H
#define FLEXIMG_RENDER_TYPES_H

#include <utility>
#include "common.h"
#include "image_buffer.h"
#include "perf_metrics.h"

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
    Point2f origin;  // バッファ内での基準点位置

    bool isEmpty() const { return width <= 0 || height <= 0; }

    // マージン分拡大（フィルタ用）
    RenderRequest expand(int margin) const {
        return {
            width + margin * 2, height + margin * 2,
            {origin.x + margin, origin.y + margin}
        };
    }
};

// ========================================================================
// RenderResult - 評価結果
// ========================================================================

struct RenderResult {
    ImageBuffer buffer;
    Point2f origin;  // バッファ内での基準点位置

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
