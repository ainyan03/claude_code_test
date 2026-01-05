#include "viewport.h"
#include "image_types.h"
#include <cassert>
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
      offsetX(0), offsetY(0), parent(nullptr),
      srcOriginX(0.0), srcOriginY(0.0) {
}

ViewPort::ViewPort(int w, int h, PixelFormatID fmtID, ImageAllocator* alloc)
    : data(nullptr), capacity(0), allocator(alloc), ownsData(true),
      formatID(fmtID),
      width(w), height(h), stride(0),
      offsetX(0), offsetY(0), parent(nullptr),
      srcOriginX(0.0), srcOriginY(0.0) {

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
      offsetX(other.offsetX), offsetY(other.offsetY), parent(other.parent),
      srcOriginX(other.srcOriginX), srcOriginY(other.srcOriginY) {

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
        srcOriginX = other.srcOriginX;
        srcOriginY = other.srcOriginY;

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
      offsetX(other.offsetX), offsetY(other.offsetY), parent(other.parent),
      srcOriginX(other.srcOriginX), srcOriginY(other.srcOriginY) {

    // 所有権を移転
    other.data = nullptr;
    other.capacity = 0;
    other.ownsData = false;
    other.parent = nullptr;
    other.srcOriginX = 0.0;
    other.srcOriginY = 0.0;
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
        srcOriginX = other.srcOriginX;
        srcOriginY = other.srcOriginY;

        other.data = nullptr;
        other.capacity = 0;
        other.ownsData = false;
        other.parent = nullptr;
        other.srcOriginX = 0.0;
        other.srcOriginY = 0.0;
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
    // サブビューの原点は親からの相対位置で調整
    subView.srcOriginX = srcOriginX - x;
    subView.srcOriginY = srcOriginY - y;

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
// 外部データからの構築
// ========================================================================

ViewPort ViewPort::fromExternalData(const void* externalData, int w, int h,
                                     PixelFormatID fmtID,
                                     ImageAllocator* alloc) {
    // ViewPortを作成（メモリ自動確保）
    ViewPort vp(w, h, fmtID, alloc);

    // 外部データからコピー
    if (externalData) {
        size_t totalBytes = vp.getTotalBytes();
        std::memcpy(vp.data, externalData, totalBytes);
    }

    return vp;
}

// ========================================================================
// Image からの変換
// ========================================================================

ViewPort ViewPort::fromImage(const Image& img) {
    ViewPort vp;
    vp.width = img.width;
    vp.height = img.height;
    vp.formatID = PixelFormatIDs::RGBA8_Straight;
    vp.stride = img.width * 4;  // Image に合わせる（パディングなし）
    vp.allocator = &DefaultAllocator::getInstance();
    vp.ownsData = true;
    vp.offsetX = 0;
    vp.offsetY = 0;
    vp.parent = nullptr;
    vp.srcOriginX = 0.0;
    vp.srcOriginY = 0.0;
    vp.capacity = vp.stride * vp.height;
    vp.data = vp.allocator->allocate(vp.capacity, 16);

    if (!vp.data) {
        throw std::bad_alloc();
    }

    // データをコピー（stride が同じなので一括コピー可能）
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

    if (formatID == PixelFormatIDs::RGBA8_Straight) {
        // 同じ形式の場合は直接コピー
        for (int y = 0; y < height; y++) {
            const uint8_t* srcRow = getPixelPtr<uint8_t>(0, y);
            uint8_t* dstRow = &img.data[y * width * 4];
            std::memcpy(dstRow, srcRow, width * 4);
        }
    } else if (formatID == PixelFormatIDs::RGBA16_Premultiplied) {
        // RGBA16_Premultiplied → RGBA8_Straight 変換（unpremultiply + 16bit→8bit）
        for (int y = 0; y < height; y++) {
            const uint16_t* srcRow = getPixelPtr<uint16_t>(0, y);
            uint8_t* dstRow = &img.data[y * width * 4];
            for (int x = 0; x < width; x++) {
                int idx = x * 4;
                uint16_t r16 = srcRow[idx];
                uint16_t g16 = srcRow[idx + 1];
                uint16_t b16 = srcRow[idx + 2];
                uint16_t a16 = srcRow[idx + 3];
                // unpremultiply + 16bit → 8bit
                if (a16 > 0) {
                    uint32_t r_unpre = ((uint32_t)r16 * 65535) / a16;
                    uint32_t g_unpre = ((uint32_t)g16 * 65535) / a16;
                    uint32_t b_unpre = ((uint32_t)b16 * 65535) / a16;
                    dstRow[idx]     = std::min(r_unpre >> 8, 255u);
                    dstRow[idx + 1] = std::min(g_unpre >> 8, 255u);
                    dstRow[idx + 2] = std::min(b_unpre >> 8, 255u);
                } else {
                    dstRow[idx] = dstRow[idx + 1] = dstRow[idx + 2] = 0;
                }
                dstRow[idx + 3] = a16 >> 8;
            }
        }
    } else if (formatID == PixelFormatIDs::RGBA16_Straight) {
        // RGBA16_Straight → RGBA8_Straight 変換（16bit→8bit のみ）
        for (int y = 0; y < height; y++) {
            const uint16_t* srcRow = getPixelPtr<uint16_t>(0, y);
            uint8_t* dstRow = &img.data[y * width * 4];
            for (int x = 0; x < width; x++) {
                int idx = x * 4;
                dstRow[idx]     = srcRow[idx]     >> 8;
                dstRow[idx + 1] = srcRow[idx + 1] >> 8;
                dstRow[idx + 2] = srcRow[idx + 2] >> 8;
                dstRow[idx + 3] = srcRow[idx + 3] >> 8;
            }
        }
    } else {
        // その他の形式は未対応
        throw std::runtime_error("ViewPort::toImage: unsupported format");
    }

    return img;
}

// ========================================================================
// フォーマット変換
// ========================================================================

ViewPort ViewPort::convertTo(PixelFormatID targetFormat) const {
    // 既に同じフォーマットの場合はコピーを返す
    if (formatID == targetFormat) {
        return ViewPort(*this);
    }

    // 新しいViewPortを作成
    ViewPort result(width, height, targetFormat);

    // 原点情報を引き継ぐ
    result.srcOriginX = srcOriginX;
    result.srcOriginY = srcOriginY;

    // PixelFormatRegistryを使って行ごとに変換
    PixelFormatRegistry& registry = PixelFormatRegistry::getInstance();
    for (int y = 0; y < height; y++) {
        const void* srcRow = getPixelPtr<uint8_t>(0, y);
        void* dstRow = result.getPixelPtr<uint8_t>(0, y);
        registry.convert(srcRow, formatID, dstRow, targetFormat, width);
    }

    return result;
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

    // ストライドの妥当性を検証（バッファオーバーフロー防止）
    assert(stride >= static_cast<int>(copyBytes) && "stride must be >= width * bytesPerPixel");
    assert(other.stride >= static_cast<int>(copyBytes) && "source stride must be >= width * bytesPerPixel");

    for (int y = 0; y < height; y++) {
        std::memcpy(dstPtr + y * stride,
                    srcPtr + y * other.stride,
                    copyBytes);
    }
}

} // namespace ImageTransform
