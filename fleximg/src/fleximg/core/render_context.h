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
    /// @brief RenderResponseプールサイズ
    static constexpr int MAX_RESPONSES = 64;

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
    // RenderResponse貸出API（参照返し）
    // ========================================

    /// @brief RenderResponseを取得（借用）
    /// @return 初期化済みRenderResponse参照（pool/allocator設定済み）
    /// @note プール枯渇時はエラーフラグを設定し、最後のエントリを返す
    RenderResponse& acquireResponse() {
#ifdef FLEXIMG_DEBUG
        if (nextResponseIndex_ >= MAX_RESPONSES - 4) {
            printf("WARN: acquireResponse idx=%d/%d\n", nextResponseIndex_, MAX_RESPONSES);
            fflush(stdout);
#ifdef ARDUINO
            vTaskDelay(1);
#endif
        }
#endif
        if (nextResponseIndex_ >= MAX_RESPONSES) {
            // プール枯渇 - エラーフラグを設定、最後のエントリを返す
            error_ = Error::PoolExhausted;
#ifdef FLEXIMG_DEBUG
            printf("ERROR: RenderResponse pool exhausted! MAX=%d\n", MAX_RESPONSES);
            fflush(stdout);
#ifdef ARDUINO
            vTaskDelay(1);
#endif
#endif
            RenderResponse& fallback = responsePool_[MAX_RESPONSES - 1];
            fallback.bufferSet.setPool(entryPool_);
            fallback.bufferSet.setAllocator(allocator_);
            fallback.bufferSet.clear();
            return fallback;
        }
        ++inUseCount_;
        RenderResponse& resp = responsePool_[nextResponseIndex_++];
        resp.bufferSet.setPool(entryPool_);
        resp.bufferSet.setAllocator(allocator_);
        resp.bufferSet.clear();
        return resp;
    }

    /// @brief RenderResponseを返却
    /// @param resp 返却するResponse参照
    void releaseResponse(RenderResponse& resp) {
        (void)resp;  // 参照の検証は省略（信頼ベース）
        if (inUseCount_ > 0) --inUseCount_;
    }

    /// @brief スキャンライン終了時にリソースをリセット（RendererNode用）
    /// @note 未返却チェックを行い、インデックスをリセット
    void resetScanlineResources() {
#ifdef FLEXIMG_DEBUG
        static int resetCount = 0;
        if (++resetCount % 100 == 0) {
            printf("RESET: cnt=%d idx=%d\n", resetCount, nextResponseIndex_);
            fflush(stdout);
        }
#endif
        // 未返却チェック（1つは下流に渡されるため、1以下なら正常）
        if (inUseCount_ > 1) {
            error_ = Error::ResponseNotReturned;
        }
        nextResponseIndex_ = 0;
        inUseCount_ = 0;
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

    // RenderResponseプール（スキャンライン単位で再利用）
    RenderResponse responsePool_[MAX_RESPONSES];
    int nextResponseIndex_ = 0;
    int inUseCount_ = 0;  // 貸出中のResponse数
    Error error_ = Error::None;
};

} // namespace core

// 親名前空間に公開
using core::RenderContext;

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_RENDER_CONTEXT_H
