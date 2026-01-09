#ifndef FLEXIMG_VIEWPORT_H
#define FLEXIMG_VIEWPORT_H

#include <cstddef>
#include <cstdint>
#include "common.h"
#include "pixel_format.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// ViewPort - 純粋ビュー（軽量POD）
// ========================================================================
//
// 画像データへの軽量なビューです。
// - メモリを所有しない（参照のみ）
// - 最小限のフィールドとメソッドのみ
// - 操作はフリー関数（view_ops名前空間）で提供
//

struct ViewPort {
    void* data = nullptr;
    PixelFormatID formatID = PixelFormatIDs::RGBA8_Straight;
    size_t stride = 0;
    int width = 0;
    int height = 0;

    // デフォルトコンストラクタ
    ViewPort() = default;

    // 直接初期化
    ViewPort(void* d, PixelFormatID fmt, size_t str, int w, int h)
        : data(d), formatID(fmt), stride(str), width(w), height(h) {}

    // 簡易初期化（strideを自動計算）
    ViewPort(void* d, int w, int h, PixelFormatID fmt = PixelFormatIDs::RGBA8_Straight)
        : data(d), formatID(fmt), stride(w * getBytesPerPixel(fmt)), width(w), height(h) {}

    // 有効判定
    bool isValid() const { return data != nullptr && width > 0 && height > 0; }

    // ピクセルアドレス取得
    void* pixelAt(int x, int y) {
        return static_cast<uint8_t*>(data) + y * stride + x * getBytesPerPixel(formatID);
    }

    const void* pixelAt(int x, int y) const {
        return static_cast<const uint8_t*>(data) + y * stride + x * getBytesPerPixel(formatID);
    }

    // バイト情報
    size_t bytesPerPixel() const { return getBytesPerPixel(formatID); }
    size_t rowBytes() const { return stride > 0 ? stride : width * bytesPerPixel(); }
};

// ========================================================================
// view_ops - ViewPort操作（フリー関数）
// ========================================================================

namespace view_ops {

// サブビュー作成
inline ViewPort subView(const ViewPort& v, int x, int y, int w, int h) {
    size_t bpp = v.bytesPerPixel();
    void* subData = static_cast<uint8_t*>(v.data) + y * v.stride + x * bpp;
    return ViewPort(subData, v.formatID, v.stride, w, h);
}

// ブレンド操作（実装はviewport.cppで提供）
void blendFirst(ViewPort& dst, int dstX, int dstY,
                const ViewPort& src, int srcX, int srcY,
                int width, int height);

void blendOnto(ViewPort& dst, int dstX, int dstY,
               const ViewPort& src, int srcX, int srcY,
               int width, int height);

// 矩形コピー
void copy(ViewPort& dst, int dstX, int dstY,
          const ViewPort& src, int srcX, int srcY,
          int width, int height);

// 矩形クリア
void clear(ViewPort& dst, int x, int y, int width, int height);

} // namespace view_ops

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_VIEWPORT_H
