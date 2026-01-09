#ifndef FLEXIMG_IMAGE_ALLOCATOR_H
#define FLEXIMG_IMAGE_ALLOCATOR_H

#include <cstddef>
#include <cstdlib>
#include <cstdint>

#ifdef _WIN32
#include <malloc.h>
#else
#include <stdlib.h>
#endif

#include "common.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// メモリアロケータインターフェース
// ========================================================================

class ImageAllocator {
public:
    virtual ~ImageAllocator() = default;

    // メモリ確保（アライメント考慮）
    virtual void* allocate(size_t bytes, size_t alignment = 16) = 0;

    // メモリ解放
    virtual void deallocate(void* ptr) = 0;

    // アロケータ名（デバッグ用）
    virtual const char* getName() const = 0;
};

// ========================================================================
// デフォルトアロケータ（malloc/free）
// ========================================================================

class DefaultAllocator : public ImageAllocator {
public:
    void* allocate(size_t bytes, size_t alignment = 16) override {
        // アライメントを考慮した確保
        #ifdef _WIN32
            return _aligned_malloc(bytes, alignment);
        #else
            void* ptr = nullptr;
            if (posix_memalign(&ptr, alignment, bytes) != 0) {
                return nullptr;
            }
            return ptr;
        #endif
    }

    void deallocate(void* ptr) override {
        if (!ptr) return;

        #ifdef _WIN32
            _aligned_free(ptr);
        #else
            free(ptr);
        #endif
    }

    const char* getName() const override {
        return "DefaultAllocator";
    }

    // シングルトンパターン
    static DefaultAllocator& getInstance() {
        static DefaultAllocator instance;
        return instance;
    }
};

// ========================================================================
// 固定バッファアロケータ（組込み環境用）
// ========================================================================

class FixedBufferAllocator : public ImageAllocator {
public:
    FixedBufferAllocator(void* buffer, size_t size)
        : buffer_(static_cast<uint8_t*>(buffer)), size_(size), offset_(0) {}

    void* allocate(size_t bytes, size_t alignment = 16) override {
        // アライメント調整
        size_t alignedOffset = (offset_ + alignment - 1) & ~(alignment - 1);

        if (alignedOffset + bytes > size_) {
            return nullptr;  // メモリ不足
        }

        void* ptr = buffer_ + alignedOffset;
        offset_ = alignedOffset + bytes;
        return ptr;
    }

    void deallocate(void* ptr) override {
        // 固定バッファでは個別解放はしない（リセットのみ）
        (void)ptr;  // 未使用警告を抑制
    }

    // バッファをリセット
    void reset() {
        offset_ = 0;
    }

    const char* getName() const override {
        return "FixedBufferAllocator";
    }

    // 現在の使用量を取得
    size_t getUsedBytes() const {
        return offset_;
    }

    // 残り容量を取得
    size_t getAvailableBytes() const {
        return size_ - offset_;
    }

private:
    uint8_t* buffer_;
    size_t size_;
    size_t offset_;
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMAGE_ALLOCATOR_H
