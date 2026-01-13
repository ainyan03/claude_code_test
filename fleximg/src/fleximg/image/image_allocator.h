/**
 * @file image_allocator.h
 * @brief [DEPRECATED] 画像用メモリアロケータ
 *
 * このファイルは非推奨です。将来のバージョンで削除予定です。
 * 新規コードでは core/memory/allocator.h を使用してください。
 *
 * @deprecated core::memory::IAllocator を使用してください
 */

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

#include "../core/common.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// [DEPRECATED] メモリアロケータインターフェース
// 新規コードでは core::memory::IAllocator を使用してください
// ========================================================================

class ImageAllocator {
public:
    virtual ~ImageAllocator() = default;
    virtual void* allocate(size_t bytes, size_t alignment = 16) = 0;
    virtual void deallocate(void* ptr) = 0;
    virtual const char* getName() const = 0;
};

// ========================================================================
// [DEPRECATED] デフォルトアロケータ（malloc/free）
// 新規コードでは core::memory::DefaultAllocator を使用してください
// ========================================================================

class DefaultAllocator : public ImageAllocator {
public:
    void* allocate(size_t bytes, size_t alignment = 16) override {
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

    const char* getName() const override { return "DefaultAllocator"; }

    static DefaultAllocator& getInstance() {
        static DefaultAllocator instance;
        return instance;
    }
};

// ========================================================================
// [DEPRECATED] 固定バッファアロケータ（組込み環境用）
// 将来のバージョンで削除予定
// ========================================================================

class FixedBufferAllocator : public ImageAllocator {
public:
    FixedBufferAllocator(void* buffer, size_t size)
        : buffer_(static_cast<uint8_t*>(buffer)), size_(size), offset_(0) {}

    void* allocate(size_t bytes, size_t alignment = 16) override {
        size_t alignedOffset = (offset_ + alignment - 1) & ~(alignment - 1);
        if (alignedOffset + bytes > size_) return nullptr;
        void* ptr = buffer_ + alignedOffset;
        offset_ = alignedOffset + bytes;
        return ptr;
    }

    void deallocate(void* /*ptr*/) override {}
    void reset() { offset_ = 0; }
    const char* getName() const override { return "FixedBufferAllocator"; }
    size_t getUsedBytes() const { return offset_; }
    size_t getAvailableBytes() const { return size_ - offset_; }

private:
    uint8_t* buffer_;
    size_t size_;
    size_t offset_;
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMAGE_ALLOCATOR_H
