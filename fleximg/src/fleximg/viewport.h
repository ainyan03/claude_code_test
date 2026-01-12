#ifndef FLEXIMG_VIEWPORT_H
#define FLEXIMG_VIEWPORT_H

#include <cstddef>
#include <cstdint>
#include "common.h"
#include "pixel_format.h"
#include "pixel_format_registry.h"

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
    int32_t stride = 0;     // 負値でY軸反転対応
    int16_t width = 0;
    int16_t height = 0;

    // デフォルトコンストラクタ
    ViewPort() = default;

    // 直接初期化（引数は最速型、メンバ格納時にキャスト）
    ViewPort(void* d, PixelFormatID fmt, int32_t str, int_fast16_t w, int_fast16_t h)
        : data(d), formatID(fmt), stride(str)
        , width(static_cast<int16_t>(w))
        , height(static_cast<int16_t>(h)) {}

    // 簡易初期化（strideを自動計算）
    ViewPort(void* d, int_fast16_t w, int_fast16_t h, PixelFormatID fmt = PixelFormatIDs::RGBA8_Straight)
        : data(d), formatID(fmt)
        , stride(static_cast<int32_t>(w * getBytesPerPixel(fmt)))
        , width(static_cast<int16_t>(w))
        , height(static_cast<int16_t>(h)) {}

    // 有効判定
    bool isValid() const { return data != nullptr && width > 0 && height > 0; }

    // ピクセルアドレス取得（strideが負の場合もサポート）
    void* pixelAt(int x, int y) {
        return static_cast<uint8_t*>(data) + static_cast<int_fast32_t>(y) * stride
               + x * getBytesPerPixel(formatID);
    }

    const void* pixelAt(int x, int y) const {
        return static_cast<const uint8_t*>(data) + static_cast<int_fast32_t>(y) * stride
               + x * getBytesPerPixel(formatID);
    }

    // バイト情報
    int_fast8_t bytesPerPixel() const { return getBytesPerPixel(formatID); }
    uint32_t rowBytes() const {
        return stride > 0 ? static_cast<uint32_t>(stride)
                          : static_cast<uint32_t>(width) * static_cast<uint32_t>(bytesPerPixel());
    }
};

// ========================================================================
// view_ops - ViewPort操作（フリー関数）
// ========================================================================

namespace view_ops {

// サブビュー作成（引数は最速型、32bitマイコンでのビット切り詰め回避）
inline ViewPort subView(const ViewPort& v, int_fast16_t x, int_fast16_t y,
                        int_fast16_t w, int_fast16_t h) {
    auto bpp = v.bytesPerPixel();
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
