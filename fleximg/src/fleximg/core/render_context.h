/**
 * @file render_context.h
 * @brief レンダリングコンテキスト（パイプライン動的リソース管理）
 */

#ifndef FLEXIMG_RENDER_CONTEXT_H
#define FLEXIMG_RENDER_CONTEXT_H

#include "common.h"
#include "memory/allocator.h"

// 前方宣言（循環参照回避）
namespace FLEXIMG_NAMESPACE {
class ImageBufferEntryPool;
}

namespace FLEXIMG_NAMESPACE {
namespace core {

// ========================================================================
// RenderContext - レンダリングコンテキスト
// ========================================================================
//
// パイプライン動作中の動的オブジェクト管理を一元化するクラス。
// - RendererNodeが値型メンバとして所有
// - PrepareRequest.context経由で全ノードに伝播
// - 各ノードはcontext_ポインタとして保持
//
// 将来の拡張予定:
// - PerfMetrics*: パフォーマンス計測
// - TextureCache*: テクスチャキャッシュ
// - TempBufferPool*: 一時バッファプール
// - RenderFlags: デバッグフラグ等
//

class RenderContext {
public:
    RenderContext() = default;

    // ========================================
    // アクセサ
    // ========================================

    /// @brief アロケータを取得
    memory::IAllocator* allocator() const { return allocator_; }

    /// @brief エントリプールを取得
    ImageBufferEntryPool* entryPool() const { return entryPool_; }

    // ========================================
    // RendererNode用設定メソッド
    // ========================================

    /// @brief アロケータを設定
    void setAllocator(memory::IAllocator* alloc) { allocator_ = alloc; }

    /// @brief エントリプールを設定
    void setEntryPool(ImageBufferEntryPool* pool) { entryPool_ = pool; }

private:
    memory::IAllocator* allocator_ = nullptr;
    ImageBufferEntryPool* entryPool_ = nullptr;
};

} // namespace core

// 親名前空間に公開
using core::RenderContext;

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_RENDER_CONTEXT_H
