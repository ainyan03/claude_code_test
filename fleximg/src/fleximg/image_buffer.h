#ifndef FLEXIMG_IMAGE_BUFFER_H
#define FLEXIMG_IMAGE_BUFFER_H

#include <cstddef>
#include <cstdint>

#include "common.h"
#include "pixel_format.h"
#include "pixel_format_registry.h"
#include "image_allocator.h"

namespace FLEXIMG_NAMESPACE {

// 前方宣言
struct ViewPort;

// ========================================================================
// ImageBuffer - メモリ所有画像（RAII）
// ========================================================================
//
// ImageBufferは、メモリを所有する画像データ構造です。
// - 任意のピクセルフォーマットに対応
// - カスタムアロケータ対応（組込み環境でのメモリ管理）
// - RAII原則に従った安全なメモリ管理
// - ViewPort（純粋ビュー）への変換機能
//
// 使用例:
//   ImageBuffer img(800, 600, PixelFormatIDs::RGBA16_Premultiplied);
//   ViewPort view = img.view();
//   processImage(view);
//
struct ImageBuffer {
    // ========================================================================
    // メモリ管理
    // ========================================================================
    void* data;               // 生データポインタ
    size_t capacity;          // 確保済みバイト数
    ImageAllocator* allocator; // メモリアロケータ

    // ========================================================================
    // フォーマット情報
    // ========================================================================
    PixelFormatID formatID;   // ピクセルフォーマット

    // ========================================================================
    // 画像サイズとレイアウト
    // ========================================================================
    int width, height;        // 画像サイズ
    size_t stride;            // 行ごとのバイト数（パディング対応）

    // ========================================================================
    // コンストラクタ / デストラクタ
    // ========================================================================

    // デフォルトコンストラクタ（空の画像）
    ImageBuffer();

    // サイズ指定コンストラクタ
    ImageBuffer(int w, int h, PixelFormatID fmtID,
                ImageAllocator* alloc = &DefaultAllocator::getInstance());

    // デストラクタ（メモリ解放）
    ~ImageBuffer();

    // ========================================================================
    // コピー / ムーブセマンティクス
    // ========================================================================

    // コピーコンストラクタ（ディープコピー）
    ImageBuffer(const ImageBuffer& other);

    // コピー代入演算子
    ImageBuffer& operator=(const ImageBuffer& other);

    // ムーブコンストラクタ（所有権移転）
    ImageBuffer(ImageBuffer&& other) noexcept;

    // ムーブ代入演算子
    ImageBuffer& operator=(ImageBuffer&& other) noexcept;

    // ========================================================================
    // ViewPort取得
    // ========================================================================

    // 全体へのビューを取得
    ViewPort view();
    ViewPort view() const;

    // サブビュー作成
    ViewPort subView(int x, int y, int w, int h);
    ViewPort subView(int x, int y, int w, int h) const;

    // ========================================================================
    // ピクセルアクセス
    // ========================================================================

    void* getPixelAddress(int x, int y);
    const void* getPixelAddress(int x, int y) const;

    template<typename T>
    T* getPixelPtr(int x, int y) {
        return static_cast<T*>(getPixelAddress(x, y));
    }

    template<typename T>
    const T* getPixelPtr(int x, int y) const {
        return static_cast<const T*>(getPixelAddress(x, y));
    }

    // ========================================================================
    // フォーマット情報
    // ========================================================================

    const PixelFormatDescriptor& getFormatDescriptor() const;
    size_t getBytesPerPixel() const;
    size_t getRowBytes() const;
    size_t getTotalBytes() const;

    // ========================================================================
    // 判定
    // ========================================================================

    bool isValid() const { return data != nullptr && width > 0 && height > 0; }

    // ========================================================================
    // 変換
    // ========================================================================

    // 指定フォーマットに変換
    ImageBuffer convertTo(PixelFormatID targetFormat) const;

    // 外部データから構築
    static ImageBuffer fromExternalData(const void* externalData, int w, int h,
                                         PixelFormatID fmtID,
                                         ImageAllocator* alloc = &DefaultAllocator::getInstance());

private:
    void allocateMemory();
    void deallocateMemory();
    void deepCopy(const ImageBuffer& other);
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMAGE_BUFFER_H
