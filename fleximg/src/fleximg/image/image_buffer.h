#ifndef FLEXIMG_IMAGE_BUFFER_H
#define FLEXIMG_IMAGE_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cassert>
#include "../core/common.h"
#include "../core/perf_metrics.h"
#include "../core/memory/allocator.h"
#include "pixel_format.h"
#include "viewport.h"

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
// FormatConversion - toFormat()の変換モード
// ========================================================================
enum class FormatConversion : uint8_t {
    CopyIfNeeded,    // デフォルト: 参照モードならコピー作成
    PreferReference  // 編集しない: フォーマット一致なら参照のまま返す
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
          allocator_(&core::memory::DefaultAllocator::instance()),
          initPolicy_(InitPolicy::Zero) {}

    // サイズ指定コンストラクタ
    ImageBuffer(int w, int h, PixelFormatID fmt = PixelFormatIDs::RGBA8_Straight,
                InitPolicy init = InitPolicy::Zero,
                core::memory::IAllocator* alloc = &core::memory::DefaultAllocator::instance())
        : view_(nullptr, fmt, 0, static_cast<int16_t>(w), static_cast<int16_t>(h))
        , capacity_(0), allocator_(alloc), initPolicy_(init) {
        allocate();
    }

    // 外部ViewPortを参照（メモリ所有しない）
    // 使用例: ImageBuffer ref(someViewPort);
    explicit ImageBuffer(ViewPort view)
        : view_(view)
        , capacity_(0)
        , allocator_(nullptr)  // nullなのでデストラクタで解放しない
        , initPolicy_(InitPolicy::Zero)
    {}

    // デストラクタ
    ~ImageBuffer() {
        deallocate();
    }

    // ========================================
    // コピー / ムーブセマンティクス
    // ========================================

    // コピーコンストラクタ（ディープコピー）
    // 参照モードからのコピーでも新しいメモリを確保（所有モードになる）
    ImageBuffer(const ImageBuffer& other)
        : view_(nullptr, other.view_.formatID, 0,
                other.view_.width, other.view_.height)
        , capacity_(0)
        , allocator_(other.allocator_ ? other.allocator_ : &core::memory::DefaultAllocator::instance())
        , initPolicy_(InitPolicy::Uninitialized) {
        if (other.isValid()) {
            allocate();
            copyFrom(other);
        }
    }

    // コピー代入
    // 参照モードからのコピーでも新しいメモリを確保（所有モードになる）
    ImageBuffer& operator=(const ImageBuffer& other) {
        if (this != &other) {
            deallocate();
            view_.formatID = other.view_.formatID;
            view_.width = other.view_.width;
            view_.height = other.view_.height;
            allocator_ = other.allocator_ ? other.allocator_ : &core::memory::DefaultAllocator::instance();
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

    // サブビューを持つ参照モードImageBufferを作成
    ImageBuffer subBuffer(int x, int y, int w, int h) const {
        return ImageBuffer(view_ops::subView(view_, x, y, w, h));
    }

    // ========================================
    // アクセサ（ViewPortに委譲）
    // ========================================

    bool isValid() const { return view_.isValid(); }

    // メモリを所有しているか（false=参照モード、編集禁止）
    bool ownsMemory() const { return allocator_ != nullptr; }

    int16_t width() const { return view_.width; }
    int16_t height() const { return view_.height; }
    int32_t stride() const { return view_.stride; }
    PixelFormatID formatID() const { return view_.formatID; }

    void* data() { return view_.data; }
    const void* data() const { return view_.data; }

    void* pixelAt(int x, int y) { return view_.pixelAt(x, y); }
    const void* pixelAt(int x, int y) const { return view_.pixelAt(x, y); }

    int_fast8_t bytesPerPixel() const { return view_.bytesPerPixel(); }
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
    //
    // mode:
    //   CopyIfNeeded    - 参照モードならコピー作成（デフォルト、編集する場合）
    //   PreferReference - フォーマット一致なら参照のまま返す（読み取り専用の場合）
    ImageBuffer toFormat(PixelFormatID target,
                         FormatConversion mode = FormatConversion::CopyIfNeeded) && {
        if (view_.formatID == target) {
            // フォーマット一致
            if (mode == FormatConversion::PreferReference) {
                // 参照希望: そのまま返す（参照モードでも所有モードでも）
                return std::move(*this);
            }
            if (ownsMemory()) {
                // 所有モード: そのまま返す
                return std::move(*this);
            }
            // 参照モード + CopyIfNeeded: コピー作成
            ImageBuffer copied(view_.width, view_.height, view_.formatID,
                               InitPolicy::Uninitialized);
            if (isValid() && copied.isValid()) {
                view_ops::copy(copied.view_, 0, 0, view_, 0, 0, view_.width, view_.height);
            }
            return copied;
        }
        // フォーマット不一致: 常に変換（新バッファ作成）
        ImageBuffer converted(view_.width, view_.height, target,
                              InitPolicy::Uninitialized);
        if (isValid() && converted.isValid()) {
            // 行単位で変換（サブビューのストライドを正しく処理）
            for (int y = 0; y < view_.height; ++y) {
                const uint8_t* srcRow = static_cast<const uint8_t*>(view_.data)
                                        + y * view_.stride;
                uint8_t* dstRow = static_cast<uint8_t*>(converted.view_.data)
                                  + y * converted.view_.stride;
                convertFormat(srcRow, view_.formatID, dstRow, target, view_.width);
            }
        }
        return converted;
    }

private:
    ViewPort view_;           // コンポジション: 画像データへのビュー
    size_t capacity_;
    core::memory::IAllocator* allocator_;
    InitPolicy initPolicy_;

    void allocate() {
        auto bpp = getBytesPerPixel(view_.formatID);
        view_.stride = static_cast<int32_t>(view_.width * bpp);
        capacity_ = static_cast<size_t>(view_.stride) * view_.height;
        if (capacity_ > 0 && allocator_) {
            view_.data = allocator_->allocate(capacity_);
            assert(view_.data != nullptr && "Memory allocation failed");
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
