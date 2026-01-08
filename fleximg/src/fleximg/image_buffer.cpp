#include "image_buffer.h"
#include "viewport.h"
#include <cassert>
#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// コンストラクタ / デストラクタ
// ========================================================================

ImageBuffer::ImageBuffer()
    : data(nullptr), capacity(0), allocator(nullptr),
      formatID(PixelFormatIDs::RGBA16_Premultiplied),
      width(0), height(0), stride(0) {
}

ImageBuffer::ImageBuffer(int w, int h, PixelFormatID fmtID, ImageAllocator* alloc)
    : data(nullptr), capacity(0), allocator(alloc),
      formatID(fmtID),
      width(w), height(h), stride(0) {

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
    : data(nullptr), capacity(0), allocator(other.allocator),
      formatID(other.formatID),
      width(other.width), height(other.height), stride(other.stride) {

    if (other.data) {
        deepCopy(other);
    }
}

ImageBuffer& ImageBuffer::operator=(const ImageBuffer& other) {
    if (this != &other) {
        deallocateMemory();

        allocator = other.allocator;
        formatID = other.formatID;
        width = other.width;
        height = other.height;
        stride = other.stride;

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
    : data(other.data), capacity(other.capacity), allocator(other.allocator),
      formatID(other.formatID),
      width(other.width), height(other.height), stride(other.stride) {

    other.data = nullptr;
    other.capacity = 0;
}

ImageBuffer& ImageBuffer::operator=(ImageBuffer&& other) noexcept {
    if (this != &other) {
        deallocateMemory();

        data = other.data;
        capacity = other.capacity;
        allocator = other.allocator;
        formatID = other.formatID;
        width = other.width;
        height = other.height;
        stride = other.stride;

        other.data = nullptr;
        other.capacity = 0;
    }
    return *this;
}

// ========================================================================
// ViewPort取得
// ========================================================================

ViewPort ImageBuffer::view() {
    return ViewPort(data, formatID, stride, width, height);
}

ViewPort ImageBuffer::view() const {
    return ViewPort(const_cast<void*>(data), formatID, stride, width, height);
}

ViewPort ImageBuffer::subView(int x, int y, int w, int h) {
    return view().subView(x, y, w, h);
}

ViewPort ImageBuffer::subView(int x, int y, int w, int h) const {
    return view().subView(x, y, w, h);
}

// ========================================================================
// ピクセルアクセス
// ========================================================================

void* ImageBuffer::getPixelAddress(int x, int y) {
    size_t bytesPerPixel = getBytesPerPixel();
    uint8_t* basePtr = static_cast<uint8_t*>(data);
    return basePtr + (y * stride + x * bytesPerPixel);
}

const void* ImageBuffer::getPixelAddress(int x, int y) const {
    size_t bytesPerPixel = getBytesPerPixel();
    const uint8_t* basePtr = static_cast<const uint8_t*>(data);
    return basePtr + (y * stride + x * bytesPerPixel);
}

// ========================================================================
// フォーマット情報
// ========================================================================

const PixelFormatDescriptor& ImageBuffer::getFormatDescriptor() const {
    const PixelFormatDescriptor* desc = PixelFormatRegistry::getInstance().getFormat(formatID);
    if (!desc) {
        throw std::runtime_error("ImageBuffer: unknown pixel format ID");
    }
    return *desc;
}

size_t ImageBuffer::getBytesPerPixel() const {
    const PixelFormatDescriptor& desc = getFormatDescriptor();
    if (desc.pixelsPerUnit > 1) {
        return (desc.bytesPerUnit + desc.pixelsPerUnit - 1) / desc.pixelsPerUnit;
    }
    return (desc.bitsPerPixel + 7) / 8;
}

size_t ImageBuffer::getRowBytes() const {
    return stride > 0 ? stride : width * getBytesPerPixel();
}

size_t ImageBuffer::getTotalBytes() const {
    return height * getRowBytes();
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
