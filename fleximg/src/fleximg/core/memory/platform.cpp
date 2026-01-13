/**
 * @file platform.cpp
 * @brief プラットフォームメモリのデフォルト実装
 */

#include "platform.h"
#include "allocator.h"

namespace FLEXIMG_NAMESPACE {
namespace core {
namespace memory {

// グローバルプラットフォームメモリインスタンス
static IPlatformMemory* s_platformMemory = nullptr;

IPlatformMemory& getPlatformMemory() {
    if (!s_platformMemory) {
        s_platformMemory = &DefaultPlatformMemory::instance();
    }
    return *s_platformMemory;
}

void setPlatformMemory(IPlatformMemory* platformMemory) {
    s_platformMemory = platformMemory;
}

// DefaultPlatformMemory の実装
void* DefaultPlatformMemory::allocate(size_t size, const AllocateOptions& options) {
    return DefaultAllocator::instance().allocate(size, options.alignment);
}

void DefaultPlatformMemory::deallocate(void* ptr) {
    DefaultAllocator::instance().deallocate(ptr);
}

} // namespace memory
} // namespace core
} // namespace FLEXIMG_NAMESPACE
