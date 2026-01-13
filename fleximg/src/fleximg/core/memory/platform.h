/**
 * @file platform.h
 * @brief プラットフォーム固有のメモリ確保インターフェース
 *
 * 組込み環境（ESP32等）でのSRAM/PSRAM選択や、
 * メモリ速度優先度の制御を抽象化します。
 */

#ifndef FLEXIMG_CORE_MEMORY_PLATFORM_H
#define FLEXIMG_CORE_MEMORY_PLATFORM_H

#include <cstddef>
#include <cstdint>

#include "../common.h"

namespace FLEXIMG_NAMESPACE {
namespace core {
namespace memory {

// ========================================================================
// メモリ速度の優先度
// ========================================================================

enum class MemorySpeed {
    Fast,      // 高速メモリ優先（SRAM）
    Normal,    // 通常メモリ（SRAM推奨だがPSRAMも可）
    Slow,      // 低速メモリ可（PSRAM可、大容量優先）
};

// ========================================================================
// メモリ確保失敗時のフォールバック戦略
// ========================================================================

enum class MemoryFallback {
    NoFallback,     // フォールバックなし、失敗時はnullptrを返す
    AllowPSRAM,     // PSRAM使用を許可
    AllowAny,       // 任意のメモリ使用を許可
};

// ========================================================================
// メモリ確保オプション
// ========================================================================

struct AllocateOptions {
    MemorySpeed speed = MemorySpeed::Normal;
    MemoryFallback fallback = MemoryFallback::AllowAny;
    size_t alignment = 16;  // デフォルト16バイトアライメント
};

// ========================================================================
// IPlatformMemory - プラットフォーム固有のメモリ確保インターフェース
// ========================================================================

class IPlatformMemory {
public:
    virtual ~IPlatformMemory() = default;

    /// @brief メモリを確保
    /// @param size 確保するサイズ（バイト）
    /// @param options 確保オプション
    /// @return 確保したメモリへのポインタ（失敗時はnullptr）
    virtual void* allocate(size_t size, const AllocateOptions& options) = 0;

    /// @brief メモリを解放
    /// @param ptr 解放するメモリのポインタ
    virtual void deallocate(void* ptr) = 0;

    /// @brief PSRAM が利用可能か
    virtual bool hasPSRAM() const = 0;

    /// @brief 指定したポインタがPSRAM上にあるか
    virtual bool isPSRAM(void* ptr) const = 0;
};

// ========================================================================
// プラットフォームメモリのグローバルアクセス
// ========================================================================

/// @brief プラットフォームメモリの取得
IPlatformMemory& getPlatformMemory();

/// @brief プラットフォームメモリの設定（起動時に1回だけ呼ぶ）
void setPlatformMemory(IPlatformMemory* platformMemory);

// ========================================================================
// DefaultPlatformMemory - デフォルト実装（標準環境用）
// ========================================================================

class DefaultPlatformMemory : public IPlatformMemory {
public:
    void* allocate(size_t size, const AllocateOptions& options) override;
    void deallocate(void* ptr) override;
    bool hasPSRAM() const override { return false; }
    bool isPSRAM(void* /*ptr*/) const override { return false; }

    static DefaultPlatformMemory& instance() {
        static DefaultPlatformMemory s_instance;
        return s_instance;
    }
};

} // namespace memory
} // namespace core
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_CORE_MEMORY_PLATFORM_H
