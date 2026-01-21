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
    uint32_t allocatedBitmap = 0;   // 現在の使用状況（デバッグ用）

    void reset() {
        totalAllocations = 0;
        totalDeallocations = 0;
        hits = 0;
        misses = 0;
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

} // namespace memory
} // namespace core
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_CORE_MEMORY_POOL_ALLOCATOR_H
