#ifndef VIEWPORT_H
#define VIEWPORT_H

#include <cstddef>
#include <cstdint>
#include "pixel_format.h"
#include "pixel_format_registry.h"
#include "image_allocator.h"

namespace ImageTransform {

// 前方宣言（循環参照回避）
struct Image;

// ========================================================================
// ViewPort - 統一画像型（組込み環境対応、ビューポート機能付き）
// ========================================================================
//
// ViewPortは、統一画像型として設計された新しい画像データ構造です。
// - 任意のピクセルフォーマットに対応（8bit, 16bit, パック形式等）
// - カスタムアロケータ対応（組込み環境でのメモリ管理）
// - ビューポート機能（メモリコピーなしでサブ領域を参照）
// - RAII原則に従った安全なメモリ管理
//
// 使用例:
//   ViewPort img(800, 600, PixelFormatIDs::RGBA16_Premultiplied);
//   ViewPort roi = img.createSubView(100, 100, 640, 480);
//   processImage(roi);  // サブ領域のみ処理
//
struct ViewPort {
    // ========================================================================
    // メモリ管理
    // ========================================================================
    void* data;               // 生データポインタ（型に依存しない）
    size_t capacity;          // 確保済みバイト数（ルート画像のみ）
    ImageAllocator* allocator; // メモリアロケータ（ルート画像のみ所有）
    bool ownsData;            // メモリ所有権フラグ（true=ルート、false=ビュー）

    // ========================================================================
    // フォーマット情報
    // ========================================================================
    PixelFormatID formatID;   // ピクセルフォーマット（bit深度含む）

    // ========================================================================
    // 画像サイズとレイアウト
    // ========================================================================
    int width, height;        // 論理サイズ（このビューのサイズ）
    size_t stride;            // 行ごとのバイト数（パディング対応）

    // ========================================================================
    // ビューポート機能
    // ========================================================================
    int offsetX, offsetY;     // 親画像内のオフセット（ピクセル単位）
    ViewPort* parent;         // nullptr = ルート画像、非null = サブビュー

    // ========================================================================
    // コンストラクタ / デストラクタ
    // ========================================================================

    // デフォルトコンストラクタ（空の画像）
    ViewPort();

    // ルート画像コンストラクタ
    // w, h: 画像サイズ（ピクセル）
    // fmtID: ピクセルフォーマット
    // alloc: メモリアロケータ（デフォルトはDefaultAllocator）
    ViewPort(int w, int h, PixelFormatID fmtID,
             ImageAllocator* alloc = &DefaultAllocator::getInstance());

    // デストラクタ（ルート画像のみメモリ解放）
    ~ViewPort();

    // ========================================================================
    // コピー / ムーブセマンティクス
    // ========================================================================

    // コピーコンストラクタ（ディープコピー）
    // ルート画像: 新しいメモリを確保してデータをコピー
    // ビュー: ビューとしてコピー（親は共有）
    ViewPort(const ViewPort& other);

    // コピー代入演算子
    ViewPort& operator=(const ViewPort& other);

    // ムーブコンストラクタ（所有権移転、高速）
    ViewPort(ViewPort&& other) noexcept;

    // ムーブ代入演算子
    ViewPort& operator=(ViewPort&& other) noexcept;

    // ========================================================================
    // ビューポート作成
    // ========================================================================

    // サブビュー作成（メモリコピーなし）
    // x, y: サブビューの左上座標（親画像内）
    // w, h: サブビューのサイズ
    // 戻り値: サブビューのViewPort（親画像のメモリを参照）
    ViewPort createSubView(int x, int y, int w, int h);

    // const版（読み取り専用ビュー）
    ViewPort createSubView(int x, int y, int w, int h) const;

    // ========================================================================
    // ピクセルアクセス
    // ========================================================================

    // ピクセルのアドレスを取得
    // x, y: ピクセル座標（このビュー内）
    // 戻り値: ピクセルデータの先頭アドレス
    void* getPixelAddress(int x, int y);
    const void* getPixelAddress(int x, int y) const;

    // 型安全なピクセルアクセス（テンプレート）
    // T: ピクセル型（uint8_t, uint16_t等）
    // 注意: formatIDと一致する型を指定する必要がある
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

    // フォーマット記述子を取得
    const PixelFormatDescriptor& getFormatDescriptor() const;

    // 1ピクセルあたりのバイト数
    size_t getBytesPerPixel() const;

    // 1行のバイト数
    size_t getRowBytes() const;

    // 画像全体のバイト数
    size_t getTotalBytes() const;

    // ========================================================================
    // 判定
    // ========================================================================

    // ルート画像かどうか
    bool isRootImage() const { return parent == nullptr; }

    // サブビューかどうか
    bool isSubView() const { return parent != nullptr; }

    // 有効な画像データを持っているか
    bool isValid() const { return data != nullptr && width > 0 && height > 0; }

    // ========================================================================
    // Image からの変換（移行用ヘルパー）
    // ========================================================================

    // Image（8bit RGBA）からViewPortを作成
    static ViewPort fromImage(const Image& img);

    // ========================================================================
    // Image への変換（移行用ヘルパー）
    // ========================================================================

    // ViewPortからImage（8bit RGBA）を作成
    // 注意: formatIDがRGBA8系でない場合は変換が必要
    Image toImage() const;

private:
    // ========================================================================
    // 内部ヘルパー
    // ========================================================================

    // メモリを確保（ルート画像用）
    void allocateMemory();

    // メモリを解放（ルート画像用）
    void deallocateMemory();

    // ディープコピー実行
    void deepCopy(const ViewPort& other);
};

} // namespace ImageTransform

#endif // VIEWPORT_H
