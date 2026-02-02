/**
 * @file render_context.h
 * @brief レンダリングコンテキスト（パイプライン動的リソース管理）
 */

#ifndef FLEXIMG_RENDER_CONTEXT_H
#define FLEXIMG_RENDER_CONTEXT_H

#include "common.h"
#include "memory/allocator.h"
#include "../image/render_types.h"

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
    /// @brief RenderResponseプールサイズ（ImageBufferEntryPoolと同様の管理）
    static constexpr int MAX_RESPONSES_BITS = 4;  // 2^4 = 16
    static constexpr int MAX_RESPONSES = 1 << MAX_RESPONSES_BITS;

    /// @brief エラー種別
    enum class Error {
        None = 0,
        PoolExhausted,       // プール枯渇
        ResponseNotReturned, // 未返却検出
    };

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

    // ========================================
    // RenderResponse貸出API（ImageBufferEntryPool方式）
    // ========================================

    /// @brief RenderResponseを取得（借用）
    /// @return 初期化済みRenderResponse参照（pool/allocator設定済み）
    /// @note プール枯渇時はエラーフラグを設定し、フォールバックを返す
    /// @note ImageBufferEntryPoolと同様のヒント付き循環探索
    RenderResponse& acquireResponse() {
        // nextHint_から開始して循環探索
        for (int i = 0; i < MAX_RESPONSES; ++i) {
            int idx = (nextHint_ + i) & (MAX_RESPONSES - 1);
            if (!responsePool_[idx].inUse) {
                responsePool_[idx].inUse = true;
                nextHint_ = (idx + 1) & (MAX_RESPONSES - 1);
                RenderResponse& resp = responsePool_[idx];
                resp.bufferSet.setPool(entryPool_);
                resp.bufferSet.setAllocator(allocator_);
                resp.bufferSet.clear();
                return resp;
            }
        }
        // プール枯渇
        error_ = Error::PoolExhausted;
#ifdef FLEXIMG_DEBUG
        printf("ERROR: RenderResponse pool exhausted! MAX=%d\n", MAX_RESPONSES);
        fflush(stdout);
#ifdef ARDUINO
        vTaskDelay(1);
#endif
#endif
        // フォールバック: 最後のエントリを強制再利用（エラー状態）
        RenderResponse& fallback = responsePool_[MAX_RESPONSES - 1];
        fallback.bufferSet.setPool(entryPool_);
        fallback.bufferSet.setAllocator(allocator_);
        fallback.bufferSet.clear();
        return fallback;
    }

    /// @brief RenderResponseを返却
    /// @param resp 返却するResponse参照
    /// @note ImageBufferEntryPoolと同様の範囲チェック付き
    void releaseResponse(RenderResponse& resp) {
        // 範囲チェック（プール内のアドレスか確認）
        size_t idx = static_cast<size_t>(&resp - responsePool_);
        if (idx < MAX_RESPONSES) {
#ifdef FLEXIMG_DEBUG
            if (!resp.inUse) {
                printf("WARN: releaseResponse called on non-inUse response idx=%d\n",
                       static_cast<int>(idx));
                fflush(stdout);
#ifdef ARDUINO
                vTaskDelay(1);
#endif
            }
#endif
            resp.bufferSet.clear();  // エントリをプールに返却
            resp.inUse = false;      // スロットを再利用可能に
        }
    }

    /// @brief 全RenderResponseを一括解放（フレーム終了時）
    /// @note ImageBufferEntryPool::releaseAll()と同様
    void resetScanlineResources() {
#ifdef FLEXIMG_DEBUG
        // 未返却チェック
        int inUseCount = 0;
        for (int i = 0; i < MAX_RESPONSES; ++i) {
            if (responsePool_[i].inUse) ++inUseCount;
        }
        if (inUseCount > 1) {
            // 1つは下流に渡されるため、1以下なら正常
            printf("WARN: resetScanlineResources with %d responses still in use\n", inUseCount);
            fflush(stdout);
#ifdef ARDUINO
            vTaskDelay(1);
#endif
        }
#endif
        for (int i = 0; i < MAX_RESPONSES; ++i) {
            if (responsePool_[i].inUse) {
                responsePool_[i].inUse = false;
                responsePool_[i].bufferSet.clear();
            }
        }
        nextHint_ = 0;
    }

    // ========================================
    // エラー管理
    // ========================================

    /// @brief エラーがあるか確認
    bool hasError() const { return error_ != Error::None; }

    /// @brief エラー種別を取得
    Error error() const { return error_; }

    /// @brief エラーをクリア
    void clearError() { error_ = Error::None; }

private:
    memory::IAllocator* allocator_ = nullptr;
    ImageBufferEntryPool* entryPool_ = nullptr;

    // RenderResponseプール（ImageBufferEntryPoolと同様の管理）
    RenderResponse responsePool_[MAX_RESPONSES];
    int nextHint_ = 0;  // 次回探索開始位置（循環探索用）
    Error error_ = Error::None;
};

} // namespace core

// 親名前空間に公開
using core::RenderContext;

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_RENDER_CONTEXT_H
