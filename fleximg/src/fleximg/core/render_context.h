/**
 * @file render_context.h
 * @brief レンダリングコンテキスト（パイプライン動的リソース管理）
 */

#ifndef FLEXIMG_RENDER_CONTEXT_H
#define FLEXIMG_RENDER_CONTEXT_H

#include "common.h"
#include "memory/allocator.h"
#include "../image/render_types.h"
#include "../image/data_range.h"

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
    static constexpr int MAX_RESPONSES_BITS = 3;  // 2^3 = 8
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

    /// @brief アロケータとエントリプールを一括設定
    /// @param alloc メモリアロケータ
    /// @param pool エントリプール
    void setup(memory::IAllocator* alloc, ImageBufferEntryPool* pool) {
        allocator_ = alloc;
        entryPool_ = pool;
        for (uint_fast8_t i = 0; i < MAX_RESPONSES; ++i) {
            responsePool_[i].bufferSet.setAllocator(allocator_);
            responsePool_[i].bufferSet.setPool(entryPool_);
        }
    }

    // ========================================
    // ValidSegmentsプール（バンプアロケータ）
    // ========================================

    /// @brief セグメント領域を確保
    /// @param count 必要なDataRangeスロット数
    /// @return 確保した領域の先頭ポインタ（枯渇時はnullptr）
    /// @note スキャンラインスコープ。resetScanlineResources()で一括解放
    DataRange* acquireSegments(int count) {
        if (segmentOffset_ + count > SEGMENT_POOL_SIZE) return nullptr;
        DataRange* result = &segmentStorage_[segmentOffset_];
        segmentOffset_ += count;
        return result;
    }

    // ========================================
    // RenderResponse貸出API（ImageBufferEntryPool方式）
    // ========================================

    /// @brief RenderResponseを取得（借用）
    /// @return RenderResponse参照（pool/allocatorはsetupで設定済み）
    /// @note プール枯渇時はエラーフラグを設定し、フォールバックを返す
    /// @note ヒント付き循環探索でO(1)に近い性能を実現
    RenderResponse& acquireResponse() {
        // nextHint_から開始して循環探索
        uint_fast8_t idx = nextHint_;
        for (int i = 0; i < MAX_RESPONSES; ++i) {
            idx = (idx + 1) & (MAX_RESPONSES - 1);
            if (!responsePool_[idx].inUse) {
                responsePool_[idx].inUse = true;
                nextHint_ = idx;
                return responsePool_[idx];
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
                responsePool_[i].bufferSet.clear();
                responsePool_[i].inUse = false;
            }
        }
        nextHint_ = 0;
        segmentOffset_ = 0;
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
    Error error_ = Error::None;
    uint_fast8_t nextHint_ = 0;  // 次回探索開始位置（循環探索用）

    // ValidSegmentsプール（バンプアロケータ）
    // CompositeNode等がImageBufferのvalidSegments追跡用に借用する
    // スキャンラインスコープで一括解放（resetScanlineResources）
    static constexpr int SEGMENT_POOL_SIZE = 256;
    DataRange segmentStorage_[SEGMENT_POOL_SIZE];
    int_fast16_t segmentOffset_ = 0;
};

} // namespace core

// 親名前空間に公開
using core::RenderContext;

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_RENDER_CONTEXT_H
