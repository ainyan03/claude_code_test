#ifndef FLEXIMG_VIEWPORT_H
#define FLEXIMG_VIEWPORT_H

#include <cstddef>
#include <cstdint>

#include "common.h"
#include "pixel_format.h"
#include "pixel_format_registry.h"

namespace FLEXIMG_NAMESPACE {

// 前方宣言
struct ImageBuffer;

// ========================================================================
// ViewPort - 純粋ビュー（軽量、所有権なし）
// ========================================================================
//
// ViewPortは、画像データへの軽量なビューです。
// - メモリを所有しない（参照のみ）
// - 必要最小限の情報のみ保持
// - ブレンド操作を提供
//
// 使用例:
//   ImageBuffer img(800, 600, PixelFormatIDs::RGBA16_Premultiplied);
//   ViewPort view = img.view();
//   view.blendOnto(otherView, 10, 20);
//
struct ViewPort {
    // ========================================================================
    // 基本情報（純粋ビューに必要な最小限）
    // ========================================================================
    void* data;               // 生データポインタ（所有しない）
    PixelFormatID formatID;   // ピクセルフォーマット
    size_t stride;            // 行ごとのバイト数
    int width, height;        // ビューのサイズ

    // ========================================================================
    // コンストラクタ
    // ========================================================================

    // デフォルトコンストラクタ（空のビュー）
    ViewPort()
        : data(nullptr), formatID(PixelFormatIDs::RGBA16_Premultiplied),
          stride(0), width(0), height(0) {}

    // 直接初期化
    ViewPort(void* d, PixelFormatID fmt, size_t str, int w, int h)
        : data(d), formatID(fmt), stride(str), width(w), height(h) {}

    // ========================================================================
    // ピクセルアクセス
    // ========================================================================

    void* getPixelAddress(int x, int y) {
        size_t bytesPerPixel = getBytesPerPixel();
        uint8_t* basePtr = static_cast<uint8_t*>(data);
        return basePtr + (y * stride + x * bytesPerPixel);
    }

    const void* getPixelAddress(int x, int y) const {
        size_t bytesPerPixel = getBytesPerPixel();
        const uint8_t* basePtr = static_cast<const uint8_t*>(data);
        return basePtr + (y * stride + x * bytesPerPixel);
    }

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

    const PixelFormatDescriptor& getFormatDescriptor() const {
        const PixelFormatDescriptor* desc = PixelFormatRegistry::getInstance().getFormat(formatID);
        // 組込み環境を考慮し、例外ではなく静的ダミーを返すことも検討
        static PixelFormatDescriptor dummy;
        return desc ? *desc : dummy;
    }

    size_t getBytesPerPixel() const {
        const PixelFormatDescriptor& desc = getFormatDescriptor();
        if (desc.pixelsPerUnit > 1) {
            return (desc.bytesPerUnit + desc.pixelsPerUnit - 1) / desc.pixelsPerUnit;
        }
        return (desc.bitsPerPixel + 7) / 8;
    }

    size_t getRowBytes() const {
        return stride > 0 ? stride : width * getBytesPerPixel();
    }

    // ========================================================================
    // 判定
    // ========================================================================

    bool isValid() const { return data != nullptr && width > 0 && height > 0; }

    // ========================================================================
    // サブビュー作成
    // ========================================================================

    ViewPort subView(int x, int y, int w, int h) const {
        size_t bytesPerPixel = getBytesPerPixel();
        uint8_t* subData = static_cast<uint8_t*>(data) + (y * stride + x * bytesPerPixel);
        return ViewPort(subData, formatID, stride, w, h);
    }

    // ========================================================================
    // ブレンド操作（RGBA16_Premultiplied専用）
    // ========================================================================

    // 透明キャンバスへの最初の描画（memcpy最適化）
    void blendFirst(const ViewPort& src, int offsetX, int offsetY);

    // 既存画像への合成（アルファブレンド）
    void blendOnto(const ViewPort& src, int offsetX, int offsetY);

    // ========================================================================
    // 変換操作
    // ========================================================================

    // このビューの内容をImageBufferにコピー（必要に応じてフォーマット変換）
    // targetFormat: 変換先フォーマット（0 の場合は現在のフォーマットを維持）
    ImageBuffer toImageBuffer(PixelFormatID targetFormat = 0) const;
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_VIEWPORT_H
