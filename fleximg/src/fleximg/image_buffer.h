#ifndef FLEXIMG_IMAGE_BUFFER_H
#define FLEXIMG_IMAGE_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include "common.h"
#include "pixel_format.h"
#include "viewport.h"
#include "image_allocator.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// ImageBuffer - メモリ所有画像（コンポジション、RAII）
// ========================================================================
//
// 画像データを所有するクラスです。
// - ViewPortを継承しない（コンポジション）
// - view()でViewPortを取得
// - RAIIによる安全なメモリ管理
//

class ImageBuffer {
public:
    // ========================================
    // コンストラクタ / デストラクタ
    // ========================================

    // デフォルトコンストラクタ（空の画像）
    ImageBuffer()
        : data_(nullptr), formatID_(PixelFormatIDs::RGBA8_Straight),
          stride_(0), width_(0), height_(0), capacity_(0),
          allocator_(&DefaultAllocator::getInstance()) {}

    // サイズ指定コンストラクタ
    ImageBuffer(int w, int h, PixelFormatID fmt = PixelFormatIDs::RGBA8_Straight,
                ImageAllocator* alloc = &DefaultAllocator::getInstance())
        : data_(nullptr), formatID_(fmt), stride_(0), width_(w), height_(h),
          capacity_(0), allocator_(alloc) {
        allocate();
    }

    // デストラクタ
    ~ImageBuffer() {
        deallocate();
    }

    // ========================================
    // コピー / ムーブセマンティクス
    // ========================================

    // コピーコンストラクタ（ディープコピー）
    ImageBuffer(const ImageBuffer& other)
        : data_(nullptr), formatID_(other.formatID_), stride_(0),
          width_(other.width_), height_(other.height_), capacity_(0),
          allocator_(other.allocator_) {
        if (other.isValid()) {
            allocate();
            copyFrom(other);
        }
    }

    // コピー代入
    ImageBuffer& operator=(const ImageBuffer& other) {
        if (this != &other) {
            deallocate();
            formatID_ = other.formatID_;
            width_ = other.width_;
            height_ = other.height_;
            allocator_ = other.allocator_;
            if (other.isValid()) {
                allocate();
                copyFrom(other);
            }
        }
        return *this;
    }

    // ムーブコンストラクタ
    ImageBuffer(ImageBuffer&& other) noexcept
        : data_(other.data_), formatID_(other.formatID_), stride_(other.stride_),
          width_(other.width_), height_(other.height_), capacity_(other.capacity_),
          allocator_(other.allocator_) {
        other.data_ = nullptr;
        other.width_ = other.height_ = 0;
        other.stride_ = other.capacity_ = 0;
    }

    // ムーブ代入
    ImageBuffer& operator=(ImageBuffer&& other) noexcept {
        if (this != &other) {
            deallocate();
            data_ = other.data_;
            formatID_ = other.formatID_;
            stride_ = other.stride_;
            width_ = other.width_;
            height_ = other.height_;
            capacity_ = other.capacity_;
            allocator_ = other.allocator_;

            other.data_ = nullptr;
            other.width_ = other.height_ = 0;
            other.stride_ = other.capacity_ = 0;
        }
        return *this;
    }

    // ========================================
    // ビュー取得
    // ========================================

    ViewPort view() {
        return ViewPort(data_, formatID_, stride_, width_, height_);
    }

    ViewPort view() const {
        return ViewPort(const_cast<void*>(static_cast<const void*>(data_)),
                        formatID_, stride_, width_, height_);
    }

    ViewPort subView(int x, int y, int w, int h) const {
        return view_ops::subView(view(), x, y, w, h);
    }

    // ========================================
    // アクセサ
    // ========================================

    bool isValid() const { return data_ != nullptr && width_ > 0 && height_ > 0; }

    int width() const { return width_; }
    int height() const { return height_; }
    size_t stride() const { return stride_; }
    PixelFormatID formatID() const { return formatID_; }

    void* data() { return data_; }
    const void* data() const { return data_; }

    void* pixelAt(int x, int y) {
        return static_cast<uint8_t*>(data_) + y * stride_ + x * getBytesPerPixel(formatID_);
    }

    const void* pixelAt(int x, int y) const {
        return static_cast<const uint8_t*>(data_) + y * stride_ + x * getBytesPerPixel(formatID_);
    }

    size_t bytesPerPixel() const { return getBytesPerPixel(formatID_); }
    size_t totalBytes() const { return height_ * stride_; }

private:
    void* data_;
    PixelFormatID formatID_;
    size_t stride_;
    int width_, height_;
    size_t capacity_;
    ImageAllocator* allocator_;

    void allocate() {
        size_t bpp = getBytesPerPixel(formatID_);
        stride_ = width_ * bpp;
        capacity_ = stride_ * height_;
        if (capacity_ > 0 && allocator_) {
            data_ = allocator_->allocate(capacity_);
            if (data_) {
                std::memset(data_, 0, capacity_);
            }
        }
    }

    void deallocate() {
        if (data_ && allocator_) {
            allocator_->deallocate(data_);
        }
        data_ = nullptr;
        capacity_ = 0;
    }

    void copyFrom(const ImageBuffer& other) {
        if (!isValid() || !other.isValid()) return;
        size_t copyBytes = std::min(stride_, other.stride_);
        int copyHeight = std::min(height_, other.height_);
        for (int y = 0; y < copyHeight; ++y) {
            std::memcpy(
                static_cast<uint8_t*>(data_) + y * stride_,
                static_cast<const uint8_t*>(other.data_) + y * other.stride_,
                copyBytes
            );
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMAGE_BUFFER_H
