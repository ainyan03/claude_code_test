#include "image_buffer.h"
#include <cassert>
#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// コンストラクタ / デストラクタ
// ========================================================================

ImageBuffer::ImageBuffer()
    : ViewPort(), capacity(0), allocator(nullptr) {
}

ImageBuffer::ImageBuffer(int w, int h, PixelFormatID fmtID, ImageAllocator* alloc)
    : ViewPort(nullptr, fmtID, 0, w, h), capacity(0), allocator(alloc) {

    if (w <= 0 || h <= 0) {
        throw std::invalid_argument("ImageBuffer: width and height must be positive");
    }

    if (!allocator) {
        throw std::invalid_argument("ImageBuffer: allocator cannot be null");
    }

    allocateMemory();
}

ImageBuffer::~ImageBuffer() {
    deallocateMemory();
}

// ========================================================================
// コピー / ムーブセマンティクス
// ========================================================================

ImageBuffer::ImageBuffer(const ImageBuffer& other)
    : ViewPort(nullptr, other.formatID, other.stride, other.width, other.height),
      capacity(0), allocator(other.allocator) {

    if (other.data) {
        deepCopy(other);
    }
}

ImageBuffer& ImageBuffer::operator=(const ImageBuffer& other) {
    if (this != &other) {
        deallocateMemory();

        // ViewPort部分をコピー
        formatID = other.formatID;
        stride = other.stride;
        width = other.width;
        height = other.height;

        // ImageBuffer固有部分
        allocator = other.allocator;

        if (other.data) {
            deepCopy(other);
        } else {
            data = nullptr;
            capacity = 0;
        }
    }
    return *this;
}

ImageBuffer::ImageBuffer(ImageBuffer&& other) noexcept
    : ViewPort(other.data, other.formatID, other.stride, other.width, other.height),
      capacity(other.capacity), allocator(other.allocator) {

    other.data = nullptr;
    other.capacity = 0;
}

ImageBuffer& ImageBuffer::operator=(ImageBuffer&& other) noexcept {
    if (this != &other) {
        deallocateMemory();

        // ViewPort部分をムーブ
        data = other.data;
        formatID = other.formatID;
        stride = other.stride;
        width = other.width;
        height = other.height;

        // ImageBuffer固有部分をムーブ
        capacity = other.capacity;
        allocator = other.allocator;

        other.data = nullptr;
        other.capacity = 0;
    }
    return *this;
}

// ========================================================================
// 変換
// ========================================================================

ImageBuffer ImageBuffer::convertTo(PixelFormatID targetFormat) const {
    if (formatID == targetFormat) {
        return ImageBuffer(*this);
    }

    ImageBuffer result(width, height, targetFormat, allocator);

    PixelFormatRegistry& registry = PixelFormatRegistry::getInstance();
    for (int y = 0; y < height; y++) {
        const void* srcRow = getPixelPtr<uint8_t>(0, y);
        void* dstRow = result.getPixelPtr<uint8_t>(0, y);
        registry.convert(srcRow, formatID, dstRow, targetFormat, width);
    }

    return result;
}

ImageBuffer ImageBuffer::fromExternalData(const void* externalData, int w, int h,
                                           PixelFormatID fmtID,
                                           ImageAllocator* alloc) {
    ImageBuffer buf(w, h, fmtID, alloc);
    if (externalData) {
        std::memcpy(buf.data, externalData, buf.getTotalBytes());
    }
    return buf;
}

// ========================================================================
// 内部ヘルパー
// ========================================================================

void ImageBuffer::allocateMemory() {
    const PixelFormatDescriptor& desc = getFormatDescriptor();

    size_t rowBytes = width * getBytesPerPixel();
    stride = (rowBytes + 15) & ~15;  // 16バイト境界

    capacity = stride * height;
    data = allocator->allocate(capacity, 16);

    if (!data) {
        throw std::bad_alloc();
    }

    std::memset(data, 0, capacity);
}

void ImageBuffer::deallocateMemory() {
    if (data && allocator) {
        allocator->deallocate(data);
        data = nullptr;
        capacity = 0;
    }
}

void ImageBuffer::deepCopy(const ImageBuffer& other) {
    allocateMemory();

    const uint8_t* srcPtr = static_cast<const uint8_t*>(other.data);
    uint8_t* dstPtr = static_cast<uint8_t*>(data);

    size_t bytesPerPixel = getBytesPerPixel();
    size_t copyBytes = width * bytesPerPixel;

    for (int y = 0; y < height; y++) {
        std::memcpy(dstPtr + y * stride,
                    srcPtr + y * other.stride,
                    copyBytes);
    }
}

} // namespace FLEXIMG_NAMESPACE
