/**
 * @file image_buffer_entry_pool.h
 * @brief パイプライン全体で共有するImageBufferエントリプール
 */

#ifndef FLEXIMG_IMAGE_BUFFER_ENTRY_POOL_H
#define FLEXIMG_IMAGE_BUFFER_ENTRY_POOL_H

#include "image_buffer.h"
#include "data_range.h"
#include "../core/common.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// ImageBufferEntryPool - エントリプール
// ========================================================================
//
// パイプライン全体で共有するImageBufferエントリのストレージ。
// RendererNodeが所有し、PrepareRequestで全ノードに伝播する。
//
// 特徴:
// - 固定サイズプール（32エントリ、約2.2KB）
// - 断片化なし
// - フレーム終了時にreleaseAll()で一括解放
//
// メモリ構成:
// - Entry構造体: ImageBuffer(64バイト) + DataRange(4バイト) + bool(1バイト) ≈ 70バイト
// - 32エントリ × 70バイト ≈ 2.2KB
//
// 使用例:
//   ImageBufferEntryPool pool;
//   auto* entry = pool.acquire();
//   if (entry) {
//       entry->buffer = std::move(someBuffer);
//       entry->range = {0, 100};
//   }
//   pool.release(entry);  // 個別解放
//   pool.releaseAll();    // 一括解放（フレーム終了時）
//

class ImageBufferEntryPool {
public:
    /// @brief プールサイズ（組み込み向け固定上限）
    static constexpr int POOL_SIZE_BITS = 5;  // 2^5 = 32エントリ
    static constexpr int POOL_SIZE = 1 << POOL_SIZE_BITS;

    /// @brief エントリ構造体
    struct Entry {
        ImageBuffer buffer;   ///< バッファ本体
        DataRange range;      ///< 範囲（startX, endX）
        bool inUse = false;   ///< 使用中フラグ
    };

    // ========================================
    // 構築・破棄
    // ========================================

    /// @brief デフォルトコンストラクタ
    ImageBufferEntryPool() : nextHint_(0) {
        // エントリを初期化
        for (int i = 0; i < POOL_SIZE; ++i) {
            entries_[i].inUse = false;
        }
    }

    /// @brief デストラクタ
    ~ImageBufferEntryPool() {
        releaseAll();
    }

    // コピー禁止
    ImageBufferEntryPool(const ImageBufferEntryPool&) = delete;
    ImageBufferEntryPool& operator=(const ImageBufferEntryPool&) = delete;

    // ムーブ禁止（アドレス固定が必要）
    ImageBufferEntryPool(ImageBufferEntryPool&&) = delete;
    ImageBufferEntryPool& operator=(ImageBufferEntryPool&&) = delete;

    // ========================================
    // エントリ管理
    // ========================================

    /// @brief 空きエントリを取得
    /// @return 取得したエントリへのポインタ。空きがない場合はnullptr
    /// @note バッファ/範囲のリセットは行わない（呼び出し側で初期化される）
    /// @note ヒント付き循環探索でO(1)に近い性能を実現
    Entry* acquire() {
        // nextHint_から開始して循環探索
        for (int i = 0; i < POOL_SIZE; ++i) {
            int idx = (nextHint_ + i) & (POOL_SIZE - 1);
            if (!entries_[idx].inUse) {
                entries_[idx].inUse = true;
                nextHint_ = (idx + 1) & (POOL_SIZE - 1);
                return &entries_[idx];
            }
        }
        return nullptr;  // 枯渇
    }

    /// @brief エントリを返却
    /// @param entry 返却するエントリ
    /// @note バッファも解放（再取得時のムーブ代入での二重解放を防止）
    void release(Entry* entry) {
        if (entry && entry >= entries_ && entry < entries_ + POOL_SIZE) {
            entry->buffer.reset();  // バッファ解放（重要: 再取得前にクリア）
            entry->inUse = false;
        }
    }

    /// @brief 全エントリを一括解放（フレーム終了時）
    void releaseAll() {
        for (int i = 0; i < POOL_SIZE; ++i) {
            entries_[i].inUse = false;
            entries_[i].buffer.reset();  // 軽量リセット
        }
        nextHint_ = 0;  // ヒントをリセット
    }

    // ========================================
    // 状態照会
    // ========================================

    /// @brief 使用中のエントリ数を取得
    int usedCount() const {
        int count = 0;
        for (int i = 0; i < POOL_SIZE; ++i) {
            if (entries_[i].inUse) ++count;
        }
        return count;
    }

    /// @brief 空きエントリ数を取得
    int freeCount() const {
        return POOL_SIZE - usedCount();
    }

    /// @brief 空きがあるか
    bool hasAvailable() const {
        for (int i = 0; i < POOL_SIZE; ++i) {
            if (!entries_[i].inUse) return true;
        }
        return false;
    }

private:
    Entry entries_[POOL_SIZE];  ///< エントリ配列
    int nextHint_;              ///< 次回探索開始位置（循環探索用）
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMAGE_BUFFER_ENTRY_POOL_H
