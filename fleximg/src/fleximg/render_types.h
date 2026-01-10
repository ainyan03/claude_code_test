#ifndef FLEXIMG_RENDER_TYPES_H
#define FLEXIMG_RENDER_TYPES_H

#include <utility>
#include <cstdint>
#include "common.h"
#include "image_buffer.h"
#include "perf_metrics.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// TileConfig - タイル分割設定
// ========================================================================

struct TileConfig {
    int16_t tileWidth = 0;   // 0 = 分割なし
    int16_t tileHeight = 0;

    TileConfig() = default;
    TileConfig(int w, int h)
        : tileWidth(static_cast<int16_t>(w))
        , tileHeight(static_cast<int16_t>(h)) {}

    bool isEnabled() const { return tileWidth > 0 && tileHeight > 0; }
};

// ========================================================================
// RenderRequest - 部分矩形要求
// ========================================================================

struct RenderRequest {
    int16_t width = 0;
    int16_t height = 0;
    Point origin;  // バッファ内での基準点位置（固定小数点 Q24.8）

    bool isEmpty() const { return width <= 0 || height <= 0; }

    // マージン分拡大（フィルタ用）
    RenderRequest expand(int margin) const {
        int_fixed8 marginFixed = to_fixed8(margin);
        return {
            static_cast<int16_t>(width + margin * 2),
            static_cast<int16_t>(height + margin * 2),
            {origin.x + marginFixed, origin.y + marginFixed}
        };
    }
};

// ========================================================================
// RenderResult - 評価結果
// ========================================================================

struct RenderResult {
    ImageBuffer buffer;
    Point origin;  // バッファ内での基準点位置（固定小数点 Q24.8）

    RenderResult() = default;

    RenderResult(ImageBuffer&& buf, Point org)
        : buffer(std::move(buf)), origin(org) {}

    // 移行用コンストラクタ（float引数、最終的に削除予定）
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
