#ifndef FLEXIMG_IMAGE_BUFFER_H
#define FLEXIMG_IMAGE_BUFFER_H

#include <cstddef>
#include <cstdint>

#include "common.h"
#include "pixel_format.h"
#include "pixel_format_registry.h"
#include "image_allocator.h"
#include "viewport.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// ImageBuffer - メモリ所有画像（RAII）
// ========================================================================
//
// ImageBufferは、ViewPortを継承し、メモリを所有する画像データ構造です。
// - ViewPortの全機能を継承（ピクセルアクセス、ブレンド操作等）
// - カスタムアロケータ対応（組込み環境でのメモリ管理）
// - RAII原則に従った安全なメモリ管理
// - ViewPortを期待する関数にそのまま渡せる
//
// 使用例:
//   ImageBuffer img(800, 600, PixelFormatIDs::RGBA16_Premultiplied);
//   processImage(img);  // ViewPort& を受け取る関数に直接渡せる
//
// 注意: ImageBufferをViewPortとしてコピーした場合、元のImageBufferが
// 破棄されるとdanglingポインタになります。長期保持には注意してください。
//
struct ImageBuffer : public ViewPort {
    // ========================================================================
    // メモリ管理（ImageBuffer固有）
    // ========================================================================
    size_t capacity;          // 確保済みバイト数
    ImageAllocator* allocator; // メモリアロケータ

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

    // 全体へのビューを取得（スライシングでViewPort部分を返す）
    ViewPort view() { return static_cast<ViewPort&>(*this); }
    ViewPort view() const { return static_cast<const ViewPort&>(*this); }

    // 注: ピクセルアクセス、フォーマット情報、判定、subView等は
    //     ViewPortから継承されるため、ここでの宣言は不要

    // ========================================================================
    // ImageBuffer固有のユーティリティ
    // ========================================================================

    size_t getTotalBytes() const { return height * getRowBytes(); }

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
