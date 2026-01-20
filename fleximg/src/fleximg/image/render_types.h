#ifndef FLEXIMG_RENDER_TYPES_H
#define FLEXIMG_RENDER_TYPES_H

#include <utility>
#include <cstdint>
#include "../core/common.h"
#include "../core/perf_metrics.h"
#include "../core/memory/allocator.h"
#include "image_buffer.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// ExecResult - パイプライン実行結果
// ========================================================================
//
// 成功 = 0、エラー = 非0（C言語の慣例に従う）
//

enum class ExecResult : int {
    Success = 0,           // 成功
    CycleDetected = 1,     // 循環参照を検出
    NoUpstream = 2,        // 上流ノードが未接続
    NoDownstream = 3,      // 下流ノードが未接続
};

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
    Point origin;  // バッファ内での基準点位置（固定小数点 Q16.16）

    bool isEmpty() const { return width <= 0 || height <= 0; }

    // マージン分拡大（フィルタ用）
    // 左右上下に適用されるため width/height は margin*2 増加
    RenderRequest expand(int margin) const {
        int_fixed marginFixed = to_fixed(margin);
        return {
            static_cast<int16_t>(width + margin * 2),
            static_cast<int16_t>(height + margin * 2),
            {origin.x + marginFixed, origin.y + marginFixed}
        };
    }
};

// ========================================================================
// PrepareRequest - 準備リクエスト（アフィン伝播対応）
// ========================================================================
//
// pullPrepare時にアフィン変換パラメータを上流に伝播するための構造体。
// AffineNodeで行列を合成し、SourceNodeで一括実行する。
//

struct PrepareRequest {
    int16_t width = 0;
    int16_t height = 0;
    Point origin;  // 基準点位置（固定小数点 Q16.16）

    // プル型アフィン（上流→Source で実行）
    AffineMatrix affineMatrix;
    bool hasAffine = false;

    // プッシュ型アフィン（下流→Sink で実行）
    AffineMatrix pushAffineMatrix;
    bool hasPushAffine = false;

    // アロケータ（RendererNodeから伝播、各ノードがprepare時に保持）
    core::memory::IAllocator* allocator = nullptr;
};

// ========================================================================
// RenderResult - 評価結果
// ========================================================================

struct RenderResult {
    ImageBuffer buffer;
    Point origin;  // バッファ内での基準点位置（固定小数点 Q16.16）

    RenderResult() = default;

    RenderResult(ImageBuffer&& buf, Point org)
        : buffer(std::move(buf)), origin(org) {}

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
