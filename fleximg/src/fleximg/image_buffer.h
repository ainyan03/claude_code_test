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
#include "pixel_format_registry.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// InitPolicy - ImageBuffer初期化ポリシー
// ========================================================================
enum class InitPolicy : uint8_t {
    Zero,          // ゼロクリア（デフォルト）
    Uninitialized, // 初期化スキップ（全ピクセル上書き時に使用）
    DebugPattern   // デバッグ用パターン値で埋める（未初期化使用の検出用）
};

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
          allocator_(&DefaultAllocator::getInstance()),
          initPolicy_(InitPolicy::Zero) {}

    // サイズ指定コンストラクタ
    ImageBuffer(int w, int h, PixelFormatID fmt = PixelFormatIDs::RGBA8_Straight,
                InitPolicy init = InitPolicy::Zero,
                ImageAllocator* alloc = &DefaultAllocator::getInstance())
        : view_(nullptr, fmt, 0, static_cast<int16_t>(w), static_cast<int16_t>(h))
        , capacity_(0), allocator_(alloc), initPolicy_(init) {
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
    // コピー時は初期化スキップ（copyFromで全ピクセル上書きするため）
    ImageBuffer(const ImageBuffer& other)
        : view_(nullptr, other.view_.formatID, 0,
                other.view_.width, other.view_.height)
        , capacity_(0), allocator_(other.allocator_)
        , initPolicy_(InitPolicy::Uninitialized) {
        if (other.isValid()) {
            allocate();
            copyFrom(other);
        }
    }

    // コピー代入
    // コピー時は初期化スキップ（copyFromで全ピクセル上書きするため）
    ImageBuffer& operator=(const ImageBuffer& other) {
        if (this != &other) {
            deallocate();
            view_.formatID = other.view_.formatID;
            view_.width = other.view_.width;
            view_.height = other.view_.height;
            allocator_ = other.allocator_;
            initPolicy_ = InitPolicy::Uninitialized;
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
          allocator_(other.allocator_), initPolicy_(other.initPolicy_) {
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
            initPolicy_ = other.initPolicy_;

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

    int16_t width() const { return view_.width; }
    int16_t height() const { return view_.height; }
    int32_t stride() const { return view_.stride; }
    PixelFormatID formatID() const { return view_.formatID; }

    void* data() { return view_.data; }
    const void* data() const { return view_.data; }

    void* pixelAt(int x, int y) { return view_.pixelAt(x, y); }
    const void* pixelAt(int x, int y) const { return view_.pixelAt(x, y); }

    size_t bytesPerPixel() const { return view_.bytesPerPixel(); }
    uint32_t totalBytes() const {
        // strideが負の場合は絶対値を使用
        int32_t absStride = stride() >= 0 ? stride() : -stride();
        return static_cast<uint32_t>(view_.height) * static_cast<uint32_t>(absStride);
    }

    // ========================================
    // フォーマット変換
    // ========================================

    // 右辺値参照版: 同じフォーマットならムーブ、異なるなら変換
    // 使用例: ImageBuffer working = std::move(input.buffer).toFormat(PixelFormatIDs::RGBA8_Straight);
    ImageBuffer toFormat(PixelFormatID target) && {
        if (view_.formatID == target) {
            return std::move(*this);  // ムーブで返す（コピーなし）
        }
        // 変換して新しいバッファを返す（全ピクセル上書きするため初期化スキップ）
        ImageBuffer converted(view_.width, view_.height, target,
                              InitPolicy::Uninitialized, allocator_);
        if (isValid() && converted.isValid()) {
            int pixelCount = view_.width * view_.height;
            PixelFormatRegistry::getInstance().convert(
                view_.data, view_.formatID,
                converted.view_.data, target,
                pixelCount);
        }
        return converted;
    }

private:
    ViewPort view_;           // コンポジション: 画像データへのビュー
    size_t capacity_;
    ImageAllocator* allocator_;
    InitPolicy initPolicy_;

    void allocate() {
        size_t bpp = getBytesPerPixel(view_.formatID);
        view_.stride = static_cast<int32_t>(view_.width * bpp);
        capacity_ = static_cast<size_t>(view_.stride) * view_.height;
        if (capacity_ > 0 && allocator_) {
            view_.data = allocator_->allocate(capacity_);
            if (view_.data) {
                switch (initPolicy_) {
                    case InitPolicy::Zero:
                        std::memset(view_.data, 0, capacity_);
                        break;
                    case InitPolicy::DebugPattern: {
                        // 確保ごとに異なる値でmemset（未初期化使用のバグ検出用）
                        static uint8_t counter = 0xCD;
                        std::memset(view_.data, counter++, capacity_);
                        break;
                    }
                    case InitPolicy::Uninitialized:
                        // 初期化スキップ
                        break;
                }
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
        int32_t copyBytes = std::min(view_.stride, other.view_.stride);
        int16_t copyHeight = std::min(view_.height, other.view_.height);
        for (int_fast16_t y = 0; y < copyHeight; ++y) {
            std::memcpy(
                static_cast<uint8_t*>(view_.data) + y * view_.stride,
                static_cast<const uint8_t*>(other.view_.data) + y * other.view_.stride,
                static_cast<size_t>(copyBytes)
            );
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMAGE_BUFFER_H
