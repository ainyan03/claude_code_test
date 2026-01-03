#include "viewport.h"
#include "image_types.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace ImageTransform {

// ========================================================================
// コンストラクタ / デストラクタ
// ========================================================================

ViewPort::ViewPort()
    : data(nullptr), capacity(0), allocator(nullptr), ownsData(false),
      formatID(PixelFormatIDs::RGBA16_Premultiplied),
      width(0), height(0), stride(0),
      offsetX(0), offsetY(0), parent(nullptr) {
}

ViewPort::ViewPort(int w, int h, PixelFormatID fmtID, ImageAllocator* alloc)
    : data(nullptr), capacity(0), allocator(alloc), ownsData(true),
      formatID(fmtID),
      width(w), height(h), stride(0),
      offsetX(0), offsetY(0), parent(nullptr) {

    if (w <= 0 || h <= 0) {
        throw std::invalid_argument("ViewPort: width and height must be positive");
    }

    if (!allocator) {
        throw std::invalid_argument("ViewPort: allocator cannot be null");
    }

    allocateMemory();
}

ViewPort::~ViewPort() {
    deallocateMemory();
}

// ========================================================================
// コピー / ムーブセマンティクス
// ========================================================================

ViewPort::ViewPort(const ViewPort& other)
    : data(nullptr), capacity(0), allocator(other.allocator), ownsData(other.ownsData),
      formatID(other.formatID),
      width(other.width), height(other.height), stride(other.stride),
      offsetX(other.offsetX), offsetY(other.offsetY), parent(other.parent) {

    if (other.ownsData && other.data) {
        // ルート画像: ディープコピー
        deepCopy(other);
    } else if (!other.ownsData && other.data) {
        // ビュー: シャローコピー（親を共有）
        data = other.data;
    }
}

ViewPort& ViewPort::operator=(const ViewPort& other) {
    if (this != &other) {
        // 既存のメモリを解放
        deallocateMemory();

        // フィールドをコピー
        allocator = other.allocator;
        ownsData = other.ownsData;
        formatID = other.formatID;
        width = other.width;
        height = other.height;
        stride = other.stride;
        offsetX = other.offsetX;
        offsetY = other.offsetY;
        parent = other.parent;

        if (other.ownsData && other.data) {
            // ルート画像: ディープコピー
            deepCopy(other);
        } else if (!other.ownsData && other.data) {
            // ビュー: シャローコピー
            data = other.data;
        } else {
            data = nullptr;
            capacity = 0;
        }
    }
    return *this;
}

ViewPort::ViewPort(ViewPort&& other) noexcept
    : data(other.data), capacity(other.capacity), allocator(other.allocator),
      ownsData(other.ownsData),
      formatID(other.formatID),
      width(other.width), height(other.height), stride(other.stride),
      offsetX(other.offsetX), offsetY(other.offsetY), parent(other.parent) {

    // 所有権を移転
    other.data = nullptr;
    other.capacity = 0;
    other.ownsData = false;
    other.parent = nullptr;
}

ViewPort& ViewPort::operator=(ViewPort&& other) noexcept {
    if (this != &other) {
        // 既存のメモリを解放
        deallocateMemory();

        // 所有権を移転
        data = other.data;
        capacity = other.capacity;
        allocator = other.allocator;
        ownsData = other.ownsData;
        formatID = other.formatID;
        width = other.width;
        height = other.height;
        stride = other.stride;
        offsetX = other.offsetX;
        offsetY = other.offsetY;
        parent = other.parent;

        other.data = nullptr;
        other.capacity = 0;
        other.ownsData = false;
        other.parent = nullptr;
    }
    return *this;
}

// ========================================================================
// ビューポート作成
// ========================================================================

ViewPort ViewPort::createSubView(int x, int y, int w, int h) {
    if (x < 0 || y < 0 || w <= 0 || h <= 0) {
        throw std::invalid_argument("ViewPort::createSubView: invalid coordinates or size");
    }

    if (x + w > width || y + h > height) {
        throw std::out_of_range("ViewPort::createSubView: sub-view exceeds parent bounds");
    }

    ViewPort subView;
    subView.data = getPixelAddress(x, y);
    subView.capacity = 0;  // ビューはメモリを所有しない
    subView.allocator = nullptr;  // ビューはアロケータを所有しない
    subView.ownsData = false;
    subView.formatID = formatID;
    subView.width = w;
    subView.height = h;
    subView.stride = stride;  // 親と同じストライド
    subView.offsetX = offsetX + x;
    subView.offsetY = offsetY + y;
    subView.parent = this;

    return subView;
}

ViewPort ViewPort::createSubView(int x, int y, int w, int h) const {
    // const版は非const版を呼び出す（const_cast使用）
    return const_cast<ViewPort*>(this)->createSubView(x, y, w, h);
}

// ========================================================================
// ピクセルアクセス
// ========================================================================

void* ViewPort::getPixelAddress(int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height) {
        throw std::out_of_range("ViewPort::getPixelAddress: coordinates out of range");
    }

    size_t bytesPerPixel = getBytesPerPixel();
    uint8_t* basePtr = static_cast<uint8_t*>(data);
    return basePtr + (y * stride + x * bytesPerPixel);
}

const void* ViewPort::getPixelAddress(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height) {
        throw std::out_of_range("ViewPort::getPixelAddress: coordinates out of range");
    }

    size_t bytesPerPixel = getBytesPerPixel();
    const uint8_t* basePtr = static_cast<const uint8_t*>(data);
    return basePtr + (y * stride + x * bytesPerPixel);
}

// ========================================================================
// フォーマット情報
// ========================================================================

const PixelFormatDescriptor& ViewPort::getFormatDescriptor() const {
    const PixelFormatDescriptor* desc = PixelFormatRegistry::getInstance().getFormat(formatID);
    if (!desc) {
        throw std::runtime_error("ViewPort: unknown pixel format ID");
    }
    return *desc;
}

size_t ViewPort::getBytesPerPixel() const {
    const PixelFormatDescriptor& desc = getFormatDescriptor();

    // ビットパック形式の場合
    if (desc.pixelsPerUnit > 1) {
        // ピクセルあたりの平均バイト数（切り上げ）
        return (desc.bytesPerUnit + desc.pixelsPerUnit - 1) / desc.pixelsPerUnit;
    }

    // 通常の形式
    return (desc.bitsPerPixel + 7) / 8;
}

size_t ViewPort::getRowBytes() const {
    if (stride > 0) {
        return stride;
    }

    // ストライドが設定されていない場合は計算
    return width * getBytesPerPixel();
}

size_t ViewPort::getTotalBytes() const {
    return height * getRowBytes();
}

// ========================================================================
// Image からの変換
// ========================================================================

ViewPort ViewPort::fromImage(const Image& img) {
    ViewPort vp(img.width, img.height, PixelFormatIDs::RGBA8_Straight);

    // データをコピー
    if (!img.data.empty()) {
        std::memcpy(vp.data, img.data.data(), img.data.size());
    }

    return vp;
}

// ========================================================================
// Image への変換
// ========================================================================

Image ViewPort::toImage() const {
    Image img(width, height);

    if (formatID == PixelFormatIDs::RGBA8_Straight ||
        formatID == PixelFormatIDs::RGBA8_Premultiplied) {
        // 同じ形式の場合は直接コピー
        for (int y = 0; y < height; y++) {
            const uint8_t* srcRow = getPixelPtr<uint8_t>(0, y);
            uint8_t* dstRow = &img.data[y * width * 4];
            std::memcpy(dstRow, srcRow, width * 4);
        }
    } else {
        // 異なる形式の場合は変換が必要
        // TODO: PixelFormatRegistryを使用した変換
        throw std::runtime_error("ViewPort::toImage: format conversion not yet implemented");
    }

    return img;
}

// ========================================================================
// 内部ヘルパー
// ========================================================================

void ViewPort::allocateMemory() {
    const PixelFormatDescriptor& desc = getFormatDescriptor();

    // ストライドを計算（行のバイト数、16バイトアライメント）
    size_t rowBytes = width * getBytesPerPixel();
    stride = (rowBytes + 15) & ~15;  // 16バイト境界に切り上げ

    // 必要なバイト数を計算
    capacity = stride * height;

    // メモリを確保（16バイトアライメント）
    data = allocator->allocate(capacity, 16);

    if (!data) {
        throw std::bad_alloc();
    }

    // メモリをゼロクリア
    std::memset(data, 0, capacity);
}

void ViewPort::deallocateMemory() {
    if (ownsData && data && allocator) {
        allocator->deallocate(data);
        data = nullptr;
        capacity = 0;
    }
}

void ViewPort::deepCopy(const ViewPort& other) {
    // メモリを確保
    allocateMemory();

    // データをコピー（行ごとにコピー、ストライドを考慮）
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

} // namespace ImageTransform
