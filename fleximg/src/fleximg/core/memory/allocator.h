/**
 * @file allocator.h
 * @brief 汎用メモリアロケータインターフェース
 */

#ifndef FLEXIMG_CORE_MEMORY_ALLOCATOR_H
#define FLEXIMG_CORE_MEMORY_ALLOCATOR_H

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include <malloc.h>
#else
#include <cstdlib>
#endif

#include "../common.h"

namespace FLEXIMG_NAMESPACE {
namespace core {
namespace memory {

// ========================================================================
// IAllocator - メモリアロケータインターフェース
// ========================================================================

class IAllocator {
public:
    virtual ~IAllocator() = default;

    /// @brief メモリを確保
    /// @param bytes 確保するサイズ（バイト）
    /// @param alignment アライメント（デフォルト16バイト）
    /// @return 確保したメモリへのポインタ（失敗時はnullptr）
    virtual void* allocate(size_t bytes, size_t alignment = 16) = 0;

    /// @brief メモリを解放
    /// @param ptr 解放するメモリのポインタ
    virtual void deallocate(void* ptr) = 0;

    /// @brief アロケータ名を取得（デバッグ用）
    virtual const char* name() const = 0;
};

// ========================================================================
// DefaultAllocator - デフォルトアロケータ（malloc/free）
// ========================================================================

class DefaultAllocator : public IAllocator {
public:
#ifdef FLEXIMG_TRAP_DEFAULT_ALLOCATOR
    // デバッグ用: トラップ有効フラグ
    static bool& trapEnabled() {
        static bool enabled = false;
        return enabled;
    }
#endif

    void* allocate(size_t bytes, size_t alignment = 16) override {
#ifdef FLEXIMG_TRAP_DEFAULT_ALLOCATOR
        // デバッグ用: トラップ有効時にDefaultAllocatorが使われたら停止
        if (trapEnabled()) {
            assert(false && "DefaultAllocator::allocate() called - use backtrace to find caller");
        }
#endif
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

    const char* name() const override { return "DefaultAllocator"; }

    /// @brief シングルトンインスタンス取得
    static DefaultAllocator& instance() {
        static DefaultAllocator s_instance;
        return s_instance;
    }
};

} // namespace memory
} // namespace core
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_CORE_MEMORY_ALLOCATOR_H
