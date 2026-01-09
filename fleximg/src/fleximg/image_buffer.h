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
#include "perf_metrics.h"

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
        : view_(), capacity_(0),
          allocator_(&DefaultAllocator::getInstance()) {}

    // サイズ指定コンストラクタ
    ImageBuffer(int w, int h, PixelFormatID fmt = PixelFormatIDs::RGBA8_Straight,
                ImageAllocator* alloc = &DefaultAllocator::getInstance())
        : view_(nullptr, fmt, 0, w, h), capacity_(0), allocator_(alloc) {
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
        : view_(nullptr, other.view_.formatID, 0, other.view_.width, other.view_.height),
          capacity_(0), allocator_(other.allocator_) {
        if (other.isValid()) {
            allocate();
            copyFrom(other);
        }
    }

    // コピー代入
    ImageBuffer& operator=(const ImageBuffer& other) {
        if (this != &other) {
            deallocate();
            view_.formatID = other.view_.formatID;
            view_.width = other.view_.width;
            view_.height = other.view_.height;
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
        : view_(other.view_), capacity_(other.capacity_),
          allocator_(other.allocator_) {
        other.view_.data = nullptr;
        other.view_.width = other.view_.height = 0;
        other.view_.stride = 0;
        other.capacity_ = 0;
    }

    // ムーブ代入
    ImageBuffer& operator=(ImageBuffer&& other) noexcept {
        if (this != &other) {
            deallocate();
            view_ = other.view_;
            capacity_ = other.capacity_;
            allocator_ = other.allocator_;

            other.view_.data = nullptr;
            other.view_.width = other.view_.height = 0;
            other.view_.stride = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    // ========================================
    // ビュー取得
    // ========================================

    // 値で返す（安全性重視、呼び出し側での変更がImageBufferに影響しない）
    ViewPort view() { return view_; }
    ViewPort view() const { return view_; }

    // 参照で返す（効率重視、直接操作可能）
    ViewPort& viewRef() { return view_; }
    const ViewPort& viewRef() const { return view_; }

    ViewPort subView(int x, int y, int w, int h) const {
        return view_ops::subView(view_, x, y, w, h);
    }

    // ========================================
    // アクセサ（ViewPortに委譲）
    // ========================================

    bool isValid() const { return view_.isValid(); }

    int width() const { return view_.width; }
    int height() const { return view_.height; }
    size_t stride() const { return view_.stride; }
    PixelFormatID formatID() const { return view_.formatID; }

    void* data() { return view_.data; }
    const void* data() const { return view_.data; }

    void* pixelAt(int x, int y) { return view_.pixelAt(x, y); }
    const void* pixelAt(int x, int y) const { return view_.pixelAt(x, y); }

    size_t bytesPerPixel() const { return view_.bytesPerPixel(); }
    size_t totalBytes() const { return view_.height * view_.stride; }

private:
    ViewPort view_;           // コンポジション: 画像データへのビュー
    size_t capacity_;
    ImageAllocator* allocator_;

    void allocate() {
        size_t bpp = getBytesPerPixel(view_.formatID);
        view_.stride = view_.width * bpp;
        capacity_ = view_.stride * view_.height;
        if (capacity_ > 0 && allocator_) {
            view_.data = allocator_->allocate(capacity_);
            if (view_.data) {
                std::memset(view_.data, 0, capacity_);
#ifdef FLEXIMG_DEBUG_PERF_METRICS
                PerfMetrics::instance().recordAlloc(capacity_, view_.width, view_.height);
#endif
            }
        }
    }

    void deallocate() {
        if (view_.data && allocator_) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            PerfMetrics::instance().recordFree(capacity_);
#endif
            allocator_->deallocate(view_.data);
        }
        view_.data = nullptr;
        capacity_ = 0;
    }

    void copyFrom(const ImageBuffer& other) {
        if (!isValid() || !other.isValid()) return;
        size_t copyBytes = std::min(view_.stride, other.view_.stride);
        int copyHeight = std::min(view_.height, other.view_.height);
        for (int y = 0; y < copyHeight; ++y) {
            std::memcpy(
                static_cast<uint8_t*>(view_.data) + y * view_.stride,
                static_cast<const uint8_t*>(other.view_.data) + y * other.view_.stride,
                copyBytes
            );
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMAGE_BUFFER_H
