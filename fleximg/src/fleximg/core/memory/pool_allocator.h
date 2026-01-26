/**
 * @file pool_allocator.h
 * @brief ビットマップベースのプールアロケータ
 *
 * 固定サイズブロックのプールを管理し、
 * フラグメンテーションを軽減します。
 */

#ifndef FLEXIMG_CORE_MEMORY_POOL_ALLOCATOR_H
#define FLEXIMG_CORE_MEMORY_POOL_ALLOCATOR_H

#include <cstddef>
#include <cstdint>

#include "../common.h"
#include "allocator.h"

namespace FLEXIMG_NAMESPACE {
namespace core {
namespace memory {

// ========================================================================
// プールアロケータの統計情報
// ========================================================================

struct PoolStats {
    size_t totalAllocations = 0;    // 累計確保回数
    size_t totalDeallocations = 0;  // 累計解放回数
    size_t hits = 0;                // 確保成功回数
    size_t misses = 0;              // 確保失敗回数
    size_t peakUsedBlocks = 0;      // 最大同時使用ブロック数
    uint32_t allocatedBitmap = 0;   // 現在の使用状況（デバッグ用）

    void reset() {
        totalAllocations = 0;
        totalDeallocations = 0;
        hits = 0;
        misses = 0;
        peakUsedBlocks = 0;
        allocatedBitmap = 0;
    }
};

// ========================================================================
// PoolAllocator - ビットマップベースのプールアロケータ
// ========================================================================
//
// 最大32ブロックまで対応（uint32_tビットマップ制限）
//

class PoolAllocator {
public:
    PoolAllocator() = default;
    ~PoolAllocator();

    // コピー禁止
    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    /// @brief プールの初期化
    /// @param memory プール用メモリ領域（外部で確保済み）
    /// @param blockSize 各ブロックのサイズ
    /// @param blockCount ブロック数（最大32）
    /// @param isPSRAM プールがPSRAMかどうか
    /// @return 初期化成功ならtrue
    bool initialize(void* memory, size_t blockSize, size_t blockCount, bool isPSRAM = false);

    /// @brief メモリ確保（プールから）
    /// @param size 確保サイズ
    /// @return 確保したメモリへのポインタ（失敗時はnullptr）
    void* allocate(size_t size);

    /// @brief メモリ解放（プールへ）
    /// @param ptr 解放するメモリのポインタ
    /// @return プール内のポインタならtrue
    bool deallocate(void* ptr);

    /// @brief プールがPSRAMかどうか
    bool isPSRAM() const { return isPSRAM_; }

    /// @brief 初期化済みかどうか
    bool isInitialized() const { return initialized_; }

    /// @brief ブロックサイズ取得
    size_t blockSize() const { return blockSize_; }

    /// @brief ブロック数取得
    size_t blockCount() const { return blockCount_; }

    /// @brief 使用中ブロック数取得
    size_t usedBlockCount() const;

    /// @brief 空きブロック数取得
    size_t freeBlockCount() const { return blockCount_ - usedBlockCount(); }

    /// @brief 統計情報取得
    const PoolStats& stats() const { return stats_; }

    /// @brief 統計情報リセット
    void resetStats() { stats_.reset(); }

    /// @brief ピーク使用ブロック数のみリセット
    void resetPeakStats() { stats_.peakUsedBlocks = 0; }

private:
    void* poolMemory_ = nullptr;        // プール用メモリ領域（外部管理）
    size_t blockSize_ = 0;              // ブロックサイズ
    size_t blockCount_ = 0;             // ブロック数
    bool isPSRAM_ = false;              // PSRAMかどうか
    uint32_t allocatedBitmap_ = 0;      // ブロック使用状況
    uint8_t blockCounts_[32] = {};      // 各ブロックの確保ブロック数（連続確保対応）
    bool searchFromHead_ = true;        // 探索方向（交互に切り替え）
    PoolStats stats_;
    bool initialized_ = false;
};

// ========================================================================
// PoolAllocatorAdapter - IAllocatorインターフェースアダプタ
// ========================================================================
//
// PoolAllocatorをIAllocatorインターフェースでラップします。
// - プールから確保できない場合はDefaultAllocatorにフォールバック
// - FLEXIMG_DEBUG_PERF_METRICS定義時は統計情報を記録
//
// 使用例:
//   PoolAllocator pool;
//   pool.initialize(memory, 512, 32, false);
//   PoolAllocatorAdapter adapter(pool);
//   renderer.setAllocator(&adapter);
//

class PoolAllocatorAdapter : public IAllocator {
public:
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    /// @brief 統計情報（デバッグビルド時のみ有効）
    struct Stats {
        size_t poolHits = 0;        ///< プールから確保成功
        size_t poolMisses = 0;      ///< プールから確保失敗（フォールバック）
        size_t poolDeallocs = 0;    ///< プールへ解放
        size_t defaultDeallocs = 0; ///< DefaultAllocatorへ解放
        size_t lastAllocSize = 0;   ///< 最後の確保サイズ

        void reset() {
            poolHits = poolMisses = poolDeallocs = defaultDeallocs = 0;
            lastAllocSize = 0;
        }
    };
#endif

    /// @brief コンストラクタ
    /// @param pool 使用するPoolAllocator
    /// @param allowFallback プール確保失敗時にDefaultAllocatorへフォールバックするか
    explicit PoolAllocatorAdapter(PoolAllocator& pool, bool allowFallback = true)
        : pool_(pool), allowFallback_(allowFallback) {}

    void* allocate(size_t bytes, size_t /* alignment */ = 16) override {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        stats_.lastAllocSize = bytes;
#endif
        void* ptr = pool_.allocate(bytes);
        if (ptr) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            stats_.poolHits++;
#endif
            return ptr;
        }

        // プールから確保できない場合
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        stats_.poolMisses++;
#endif
        if (allowFallback_) {
            return DefaultAllocator::instance().allocate(bytes);
        }
        return nullptr;
    }

    void deallocate(void* ptr) override {
        if (pool_.deallocate(ptr)) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            stats_.poolDeallocs++;
#endif
        } else {
            // プール外のポインタはDefaultAllocatorで解放
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            stats_.defaultDeallocs++;
#endif
            if (allowFallback_) {
                DefaultAllocator::instance().deallocate(ptr);
            }
        }
    }

    const char* name() const override { return "PoolAllocatorAdapter"; }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    /// @brief 統計情報取得（デバッグビルド時のみ）
    const Stats& stats() const { return stats_; }

    /// @brief 統計情報リセット（デバッグビルド時のみ）
    void resetStats() { stats_.reset(); }
#endif

private:
    PoolAllocator& pool_;
    bool allowFallback_;
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    Stats stats_;
#endif
};

} // namespace memory
} // namespace core
} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {
namespace core {
namespace memory {

PoolAllocator::~PoolAllocator() {
    // poolMemory_ は外部管理なので解放しない
}

bool PoolAllocator::initialize(void* memory, size_t blockSize, size_t blockCount, bool isPSRAM) {
    if (initialized_ || !memory || blockSize == 0 || blockCount == 0) {
        return false;
    }

    if (blockCount > 32) {
        return false;  // uint32_t制限
    }

    poolMemory_ = memory;
    blockSize_ = blockSize;
    blockCount_ = blockCount;
    isPSRAM_ = isPSRAM;
    allocatedBitmap_ = 0;
    for (size_t i = 0; i < 32; ++i) {
        blockCounts_[i] = 0;
    }

    initialized_ = true;
    return true;
}

void* PoolAllocator::allocate(size_t size) {
    if (!initialized_ || size == 0) {
        return nullptr;
    }

    stats_.totalAllocations++;

    // 必要なブロック数を計算
    size_t blocksNeeded = (size + blockSize_ - 1) / blockSize_;

    if (blocksNeeded > blockCount_) {
        stats_.misses++;
        return nullptr;
    }

    // 必要なビットパターンを作成
    uint32_t needBitmap = (1U << blocksNeeded) - 1;

    // 探索方向を決定（交互に切り替えてフラグメンテーション軽減）
    int start = searchFromHead_ ? 0 : static_cast<int>(blockCount_ - blocksNeeded);
    int end = searchFromHead_ ? static_cast<int>(blockCount_ - blocksNeeded + 1) : -1;
    int step = searchFromHead_ ? 1 : -1;

    searchFromHead_ = !searchFromHead_;  // 次回は逆方向

    // ビットマップで連続空きブロックを探索
    for (int i = start; i != end; i += step) {
        uint32_t shiftedNeed = needBitmap << i;

        if ((allocatedBitmap_ & shiftedNeed) == 0) {
            // 空きブロック発見
            allocatedBitmap_ |= shiftedNeed;
            blockCounts_[i] = static_cast<uint8_t>(blocksNeeded);  // 確保ブロック数を記録
            stats_.hits++;
            stats_.allocatedBitmap = allocatedBitmap_;

            // ピーク使用ブロック数を更新
            size_t currentUsed = usedBlockCount();
            if (currentUsed > stats_.peakUsedBlocks) {
                stats_.peakUsedBlocks = currentUsed;
            }

            return static_cast<uint8_t*>(poolMemory_) + (static_cast<size_t>(i) * blockSize_);
        }
    }

    stats_.misses++;
    return nullptr;
}

bool PoolAllocator::deallocate(void* ptr) {
    if (!initialized_ || !ptr) {
        return false;
    }

    // プール内のポインタか判定
    uint8_t* poolStart = static_cast<uint8_t*>(poolMemory_);
    size_t poolSize = blockSize_ * blockCount_;
    uint8_t* poolEnd = poolStart + poolSize;
    uint8_t* p = static_cast<uint8_t*>(ptr);

    if (p < poolStart || p >= poolEnd) {
        return false;  // プール外
    }

    // ブロックインデックス計算
    size_t blockIndex = static_cast<size_t>(p - poolStart) / blockSize_;

    if (blockIndex >= blockCount_) {
        return false;  // 範囲外
    }

    // ビットが立っているか確認（確保済みか）
    if ((allocatedBitmap_ & (1U << blockIndex)) == 0) {
        return false;  // 二重解放
    }

    // 確保ブロック数を取得
    uint8_t blocksToFree = blockCounts_[blockIndex];
    if (blocksToFree == 0) {
        blocksToFree = 1;  // フォールバック（通常は起きない）
    }

    stats_.totalDeallocations++;

    // 確保時のブロック数分のビットをクリア
    uint32_t freeBitmap = ((1U << blocksToFree) - 1) << blockIndex;
    allocatedBitmap_ &= ~freeBitmap;
    blockCounts_[blockIndex] = 0;  // 記録をクリア
    stats_.allocatedBitmap = allocatedBitmap_;

    return true;
}

size_t PoolAllocator::usedBlockCount() const {
    size_t count = 0;
    uint32_t bitmap = allocatedBitmap_;
    while (bitmap) {
        count += bitmap & 1;
        bitmap >>= 1;
    }
    return count;
}

} // namespace memory
} // namespace core
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_CORE_MEMORY_POOL_ALLOCATOR_H
