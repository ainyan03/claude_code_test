/**
 * @file buffer_handle.h
 * @brief RAII方式のバッファハンドル
 *
 * メモリの確保/解放を自動管理し、リソースリークを防ぎます。
 */

#ifndef FLEXIMG_CORE_MEMORY_BUFFER_HANDLE_H
#define FLEXIMG_CORE_MEMORY_BUFFER_HANDLE_H

#include <cstddef>
#include <utility>

#include "../common.h"
#include "platform.h"

namespace FLEXIMG_NAMESPACE {
namespace core {
namespace memory {

// ========================================================================
// BufferHandle - RAII方式のバッファハンドル
// ========================================================================

class BufferHandle {
public:
    /// @brief デフォルトコンストラクタ
    BufferHandle() = default;

    /// @brief サイズ指定コンストラクタ
    /// @param size 確保するサイズ
    /// @param options 確保オプション
    explicit BufferHandle(size_t size, const AllocateOptions& options = {})
        : size_(size) {
        ptr_ = getPlatformMemory().allocate(size, options);
        if (!ptr_) {
            size_ = 0;
        }
    }

    /// @brief デストラクタ（自動解放）
    ~BufferHandle() {
        reset();
    }

    /// @brief ムーブコンストラクタ
    BufferHandle(BufferHandle&& other) noexcept
        : ptr_(other.ptr_)
        , size_(other.size_) {
        other.ptr_ = nullptr;
        other.size_ = 0;
    }

    /// @brief ムーブ代入演算子
    BufferHandle& operator=(BufferHandle&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            size_ = other.size_;
            other.ptr_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    /// @brief コピー禁止
    BufferHandle(const BufferHandle&) = delete;
    BufferHandle& operator=(const BufferHandle&) = delete;

    /// @brief データポインタ取得
    void* data() { return ptr_; }
    const void* data() const { return ptr_; }

    /// @brief サイズ取得
    size_t size() const { return size_; }

    /// @brief 有効性チェック
    explicit operator bool() const { return ptr_ != nullptr; }

    /// @brief リセット（解放）
    void reset() {
        if (ptr_) {
            getPlatformMemory().deallocate(ptr_);
            ptr_ = nullptr;
            size_ = 0;
        }
    }

    /// @brief 所有権の放棄（解放されなくなる）
    void* release() {
        void* p = ptr_;
        ptr_ = nullptr;
        size_ = 0;
        return p;
    }

private:
    void* ptr_ = nullptr;
    size_t size_ = 0;
};

} // namespace memory
} // namespace core
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_CORE_MEMORY_BUFFER_HANDLE_H
