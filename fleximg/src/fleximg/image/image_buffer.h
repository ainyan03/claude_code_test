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
#include "data_range.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// InitPolicy - ImageBuffer初期化ポリシー
// ========================================================================
enum class InitPolicy : uint8_t {
    Zero,          // ゼロクリア
    Uninitialized, // 初期化スキップ（全ピクセル上書き時に使用）
    DebugPattern   // デバッグ用パターン値で埋める（未初期化使用の検出用）
};

// デフォルト初期化ポリシー
// - リリースビルド: Uninitialized（パフォーマンス優先）
// - デバッグビルド: DebugPattern（未初期化使用のバグ検出）
#ifdef NDEBUG
constexpr InitPolicy DefaultInitPolicy = InitPolicy::Uninitialized;
#else
constexpr InitPolicy DefaultInitPolicy = InitPolicy::DebugPattern;
#endif

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
          auxInfo_(), origin_(), initPolicy_(DefaultInitPolicy) {}

    // サイズ指定コンストラクタ
    // alloc = nullptr の場合、DefaultAllocator を使用
    ImageBuffer(int w, int h, PixelFormatID fmt = PixelFormatIDs::RGBA8_Straight,
                InitPolicy init = DefaultInitPolicy,
                core::memory::IAllocator* alloc = nullptr)
        : view_(nullptr, fmt, 0, static_cast<int16_t>(w), static_cast<int16_t>(h))
        , capacity_(0)
        , allocator_(alloc ? alloc : &core::memory::DefaultAllocator::instance())
        , auxInfo_(), origin_(), initPolicy_(init) {
        allocate();
    }

    // 外部ViewPortを参照（メモリ所有しない）
    // 使用例: ImageBuffer ref(someViewPort);
    explicit ImageBuffer(ViewPort view)
        : view_(view)
        , capacity_(0)
        , allocator_(nullptr)  // nullなのでデストラクタで解放しない
        , auxInfo_(), origin_(), initPolicy_(InitPolicy::Zero)
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
        , auxInfo_(other.auxInfo_)
        , origin_(other.origin_)
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
            auxInfo_ = other.auxInfo_;
            origin_ = other.origin_;
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
          allocator_(other.allocator_), auxInfo_(other.auxInfo_),
          origin_(other.origin_), initPolicy_(other.initPolicy_) {
        other.view_.data = nullptr;
        other.view_.width = other.view_.height = 0;
        other.view_.stride = 0;
        other.capacity_ = 0;
        other.auxInfo_ = PixelAuxInfo();
        other.origin_ = Point();
    }

    // ムーブ代入
    ImageBuffer& operator=(ImageBuffer&& other) noexcept {
        if (this != &other) {
            deallocate();
            view_ = other.view_;
            capacity_ = other.capacity_;
            allocator_ = other.allocator_;
            initPolicy_ = other.initPolicy_;
            auxInfo_ = other.auxInfo_;
            origin_ = other.origin_;

            other.view_.data = nullptr;
            other.view_.width = other.view_.height = 0;
            other.view_.stride = 0;
            other.capacity_ = 0;
            other.auxInfo_ = PixelAuxInfo();
            other.origin_ = Point();
        }
        return *this;
    }

    // ========================================
    // リセット
    // ========================================

    /// @brief バッファを解放してクリア（一時オブジェクト生成なし）
    /// @note ムーブ代入より軽量。プールでの一括解放に最適。
    void reset() {
        deallocate();
        view_.width = 0;
        view_.height = 0;
        view_.stride = 0;
        view_.formatID = nullptr;
        allocator_ = nullptr;
        auxInfo_ = PixelAuxInfo();
        origin_ = Point();
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

    ViewPort subView(int_fast16_t x, int_fast16_t y, int_fast16_t w, int_fast16_t h) const {
        return view_ops::subView(view_, x, y, w, h);
    }

    // サブビューを持つ参照モードImageBufferを作成
    ImageBuffer subBuffer(int_fast16_t x, int_fast16_t y, int_fast16_t w, int_fast16_t h) const {
        return ImageBuffer(view_ops::subView(view_, x, y, w, h));
    }

    // ビューの有効範囲を縮小（メモリ所有権は維持）
    // subViewと同じシグネチャ: (x, y, width, height)
    void cropView(int_fast16_t x, int_fast16_t y, int_fast16_t w, int_fast16_t h) {
        view_ = view_ops::subView(view_, x, y, w, h);
    }

    // ========================================
    // アクセサ（ViewPortに委譲）
    // ========================================

    bool isValid() const { return view_.isValid(); }

    // メモリを所有しているか（false=参照モード、編集禁止）
    bool ownsMemory() const { return allocator_ != nullptr; }

    // アロケータを設定（参照モードのバッファに対して、変換時に使用するアロケータを指定）
    void setAllocator(core::memory::IAllocator* alloc) { allocator_ = alloc; }

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
    //
    // alloc:
    //   新バッファ作成時に使用するアロケータ（オプション）
    //   - nullptr（デフォルト）: 自身のallocator_を使用
    //   - non-null: 指定されたアロケータを使用
    //   注: 参照モードバッファにsetAllocator()を呼ぶと、デストラクタが
    //       非所有メモリを解放しようとするバグがあるため、このパラメータで
    //       新バッファのアロケータを安全に指定できる
    ImageBuffer toFormat(PixelFormatID target,
                         FormatConversion mode = FormatConversion::CopyIfNeeded,
                         core::memory::IAllocator* alloc = nullptr,
                         const FormatConverter* converter = nullptr) && {
        // 新バッファ用アロケータを決定
        core::memory::IAllocator* newAlloc = alloc ? alloc : allocator_;

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
                               InitPolicy::Uninitialized, newAlloc);
            if (isValid() && copied.isValid()) {
                view_ops::copy(copied.view_, 0, 0, view_, 0, 0, view_.width, view_.height);
            }
            return copied;
        }
        // フォーマット不一致: 常に変換（新バッファ作成）
        ImageBuffer converted(view_.width, view_.height, target,
                              InitPolicy::Uninitialized, newAlloc);
        if (isValid() && converted.isValid()) {
            // 変換パスを事前解決し、行単位で変換（ストライドを正しく処理）
            // 外部からコンバータが渡されていればそれを使用、なければ自前で解決
            FormatConverter resolved;
            if (converter) {
                resolved = *converter;
            } else {
                bool hasAuxData = (auxInfo_.palette != nullptr)
                              || (auxInfo_.colorKeyRGBA8 != auxInfo_.colorKeyReplace);
                const PixelAuxInfo* auxPtr = hasAuxData ? &auxInfo_ : nullptr;
                resolved = resolveConverter(view_.formatID, target, auxPtr);
            }
            if (resolved) {
                for (int y = 0; y < view_.height; ++y) {
                    const uint8_t* srcRow = static_cast<const uint8_t*>(view_.data)
                                            + y * view_.stride;
                    uint8_t* dstRow = static_cast<uint8_t*>(converted.view_.data)
                                      + y * converted.view_.stride;
                    resolved(dstRow, srcRow, view_.width);
                }
            }
        }
        return converted;
    }

    // ========================================
    // 補助情報（パレット、カラーキー等）
    // ========================================

    const PixelAuxInfo& auxInfo() const { return auxInfo_; }
    PixelAuxInfo& auxInfo() { return auxInfo_; }

    // パレット設定（PaletteData 経由）
    void setPalette(const PaletteData& pal) {
        auxInfo_.palette = pal.data;
        auxInfo_.paletteFormat = pal.format;
        auxInfo_.paletteColorCount = pal.colorCount;
    }

    // パレット設定（個別引数）
    void setPalette(const void* data, PixelFormatID fmt, uint16_t count) {
        auxInfo_.palette = data;
        auxInfo_.paletteFormat = fmt;
        auxInfo_.paletteColorCount = count;
    }

    // ========================================
    // Origin（Q16.16ワールド座標）
    // ========================================

    /// @brief originを取得（Q16.16精度）
    Point origin() const { return origin_; }

    /// @brief originを設定（Q16.16精度）
    void setOrigin(Point p) { origin_ = p; }

    /// @brief originのX座標を取得（Q16.16精度）
    int_fixed originX() const { return origin_.x; }

    /// @brief originのY座標を取得（Q16.16精度）
    int_fixed originY() const { return origin_.y; }

    // ========================================
    // X座標オフセット（整数精度ヘルパー）
    // ========================================

    /// @brief X座標オフセットを取得（originの整数部）
    int16_t startX() const { return static_cast<int16_t>(from_fixed(origin_.x)); }

    /// @brief X終端座標を取得（startX + width）
    int16_t endX() const { return static_cast<int16_t>(startX() + width()); }

    /// @brief X座標オフセットを設定（整数精度）
    void setStartX(int16_t x) { origin_.x = to_fixed(x); }

    /// @brief X座標オフセットを加算（整数精度）
    void addOffset(int16_t offset) { origin_.x += to_fixed(offset); }

    /// @brief ソースバッファのデータを自身にunder合成
    /// @param src ソースバッファ（ワールド座標origin設定済み）
    /// @return 成功時true
    bool blendFrom(const ImageBuffer& src);

private:
    ViewPort view_;           // コンポジション: 画像データへのビュー
    size_t capacity_;
    core::memory::IAllocator* allocator_;
    PixelAuxInfo auxInfo_;    // 補助情報（パレット、カラーキー等）
    Point origin_;            // バッファ原点（Q16.16ワールド座標）
    InitPolicy initPolicy_;

    void allocate() {
        auto bpp = getBytesPerPixel(view_.formatID);
        view_.stride = static_cast<int32_t>(view_.width * bpp);
        capacity_ = static_cast<size_t>(view_.stride) * static_cast<size_t>(view_.height);
        if (capacity_ > 0 && allocator_) {
            view_.data = allocator_->allocate(capacity_);
            FLEXIMG_REQUIRE(view_.data != nullptr, "Memory allocation failed");
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

// ========================================================================
// ImageBuffer::blendFrom() 実装
// ========================================================================

inline bool ImageBuffer::blendFrom(const ImageBuffer& src) {
    if (!isValid() || !src.isValid() || !view_.data) return false;

    const int16_t dstStartX = startX();
    const int16_t srcStartX = src.startX();

    // クリッピング: srcのうちdstバッファ範囲内にある部分
    const int16_t clippedStart = std::max(srcStartX, dstStartX);
    const int16_t clippedEnd = std::min(src.endX(), endX());
    if (clippedStart >= clippedEnd) return true;  // 範囲外、何もしない

    const size_t dstBpp = static_cast<size_t>(getBytesPerPixel(view_.formatID));
    const size_t srcBpp = static_cast<size_t>(getBytesPerPixel(src.view().formatID));
    uint8_t* dstRow = static_cast<uint8_t*>(view_.data);
    const uint8_t* srcRow = static_cast<const uint8_t*>(src.view().data);
    PixelFormatID srcFmt = src.view().formatID;
    const PixelAuxInfo* srcAux = &src.auxInfo();

    auto blendFunc = srcFmt->blendUnderStraight;
    if (blendFunc) {
        // 直接ブレンド（RGBA8_Straight等、blendUnderStraight実装済みフォーマット）
        void* dstPtr = dstRow + static_cast<size_t>(clippedStart - dstStartX) * dstBpp;
        const void* srcPtr = srcRow + static_cast<size_t>(clippedStart - srcStartX) * srcBpp;
        blendFunc(dstPtr, srcPtr, clippedEnd - clippedStart, srcAux);
    } else {
        // フォールバック: チャンク単位でRGBA8_Straightに変換してからブレンド
        // resolveConverterはループ外で一度だけ呼び出し
        auto converter = resolveConverter(srcFmt, PixelFormatIDs::RGBA8_Straight, srcAux);
        if (!converter) return false;
        auto straightBlend = PixelFormatIDs::RGBA8_Straight->blendUnderStraight;
        constexpr int16_t CHUNK_SIZE = 64;
        uint8_t tempBuf[CHUNK_SIZE * 4];
        int16_t remaining = static_cast<int16_t>(clippedEnd - clippedStart);
        int16_t cursor = clippedStart;
        while (remaining > 0) {
            int16_t chunk = std::min(remaining, CHUNK_SIZE);
            const void* srcPtr = srcRow + static_cast<size_t>(cursor - srcStartX) * srcBpp;
            converter(tempBuf, srcPtr, chunk);
            void* dstPtr = dstRow + static_cast<size_t>(cursor - dstStartX) * dstBpp;
            straightBlend(dstPtr, tempBuf, chunk, nullptr);
            cursor = static_cast<int16_t>(cursor + chunk);
            remaining = static_cast<int16_t>(remaining - chunk);
        }
    }
    return true;
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMAGE_BUFFER_H
