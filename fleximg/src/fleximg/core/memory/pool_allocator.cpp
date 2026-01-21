/**
 * @file pool_allocator.cpp
 * @brief プールアロケータの実装
 */

#include "pool_allocator.h"

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
