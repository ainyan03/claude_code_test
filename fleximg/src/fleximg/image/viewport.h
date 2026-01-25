#ifndef FLEXIMG_VIEWPORT_H
#define FLEXIMG_VIEWPORT_H

#include <cstddef>
#include <cstdint>
#include "../core/common.h"
#include "../core/types.h"
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

// 矩形コピー
void copy(ViewPort& dst, int dstX, int dstY,
          const ViewPort& src, int srcX, int srcY,
          int width, int height);

// 矩形クリア
void clear(ViewPort& dst, int x, int y, int width, int height);

// ========================================================================
// DDA転写関数
// ========================================================================
//
// アフィン変換等で使用するDDA（Digital Differential Analyzer）方式の
// ピクセル転写関数群。将来のbit-packed format対応を見据え、
// ViewPortから必要情報を取得する設計。
//

// DDA行転写（最近傍補間）
// dst: 出力先メモリ（行バッファ）
// src: ソース全体のViewPort（フォーマット・サイズ情報含む）
// count: 転写ピクセル数
// srcX, srcY: ソース開始座標（Q16.16固定小数点）
// incrX, incrY: 1ピクセルあたりの増分（Q16.16固定小数点）
void copyRowDDA(
    void* dst,
    const ViewPort& src,
    int count,
    int_fixed srcX,
    int_fixed srcY,
    int_fixed incrX,
    int_fixed incrY
);

// DDA行転写（バイリニア補間）
// 現状はRGBA8888専用、非対応フォーマットは最近傍にフォールバック
void copyRowDDABilinear(
    void* dst,
    const ViewPort& src,
    int count,
    int_fixed srcX,
    int_fixed srcY,
    int_fixed incrX,
    int_fixed incrY
);

// アフィン変換転写（DDA方式）
// 複数行を一括処理する高レベル関数
void affineTransform(
    ViewPort& dst,
    const ViewPort& src,
    int_fixed invTx,
    int_fixed invTy,
    const Matrix2x2_fixed& invMatrix,
    int_fixed rowOffsetX,
    int_fixed rowOffsetY,
    int_fixed dxOffsetX,
    int_fixed dxOffsetY
);

} // namespace view_ops

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

#include <cstring>
#include <algorithm>
#include "../operations/transform.h"

namespace FLEXIMG_NAMESPACE {
namespace view_ops {

void copy(ViewPort& dst, int dstX, int dstY,
          const ViewPort& src, int srcX, int srcY,
          int width, int height) {
    if (!dst.isValid() || !src.isValid()) return;

    // クリッピング
    if (srcX < 0) { dstX -= srcX; width += srcX; srcX = 0; }
    if (srcY < 0) { dstY -= srcY; height += srcY; srcY = 0; }
    if (dstX < 0) { srcX -= dstX; width += dstX; dstX = 0; }
    if (dstY < 0) { srcY -= dstY; height += dstY; dstY = 0; }
    width = std::min(width, std::min(src.width - srcX, dst.width - dstX));
    height = std::min(height, std::min(src.height - srcY, dst.height - dstY));
    if (width <= 0 || height <= 0) return;

    // 同一フォーマットならmemcpy
    if (src.formatID == dst.formatID) {
        size_t bpp = static_cast<size_t>(dst.bytesPerPixel());
        for (int y = 0; y < height; ++y) {
            const uint8_t* srcRow = static_cast<const uint8_t*>(src.pixelAt(srcX, srcY + y));
            uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(dstX, dstY + y));
            std::memcpy(dstRow, srcRow, static_cast<size_t>(width) * bpp);
        }
        return;
    }

    // 異なるフォーマット間のコピー
    for (int y = 0; y < height; ++y) {
        const void* srcRow = src.pixelAt(srcX, srcY + y);
        void* dstRow = dst.pixelAt(dstX, dstY + y);
        convertFormat(srcRow, src.formatID, dstRow, dst.formatID, width);
    }
}

void clear(ViewPort& dst, int x, int y, int width, int height) {
    if (!dst.isValid()) return;

    size_t bpp = static_cast<size_t>(dst.bytesPerPixel());
    for (int row = 0; row < height; ++row) {
        int dy = y + row;
        if (dy < 0 || dy >= dst.height) continue;
        uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(x, dy));
        std::memset(dstRow, 0, static_cast<size_t>(width) * bpp);
    }
}

// ============================================================================
// DDA転写関数 - 実装
// ============================================================================

namespace detail {

// DDA行転写の低レベル実装（テンプレート）
template<size_t BytesPerPixel>
inline void copyRowDDA_Impl(
    uint8_t* dstRow,
    const uint8_t* srcData,
    int32_t srcStride,
    int_fixed srcX,
    int_fixed srcY,
    int_fixed incrX,
    int_fixed incrY,
    int count
) {
    for (int i = 0; i < count; i++) {
        uint32_t sx = static_cast<uint32_t>(srcX) >> INT_FIXED_SHIFT;
        uint32_t sy = static_cast<uint32_t>(srcY) >> INT_FIXED_SHIFT;

        const uint8_t* srcPixel = srcData
            + static_cast<size_t>(sy) * static_cast<size_t>(srcStride)
            + static_cast<size_t>(sx) * BytesPerPixel;

        if constexpr (BytesPerPixel == 8) {
            reinterpret_cast<uint32_t*>(dstRow)[0] =
                reinterpret_cast<const uint32_t*>(srcPixel)[0];
            reinterpret_cast<uint32_t*>(dstRow)[1] =
                reinterpret_cast<const uint32_t*>(srcPixel)[1];
        } else if constexpr (BytesPerPixel == 4) {
            *reinterpret_cast<uint32_t*>(dstRow) =
                *reinterpret_cast<const uint32_t*>(srcPixel);
        } else if constexpr (BytesPerPixel == 2) {
            *reinterpret_cast<uint16_t*>(dstRow) =
                *reinterpret_cast<const uint16_t*>(srcPixel);
        } else if constexpr (BytesPerPixel == 1) {
            *dstRow = *srcPixel;
        } else {
            std::memcpy(dstRow, srcPixel, BytesPerPixel);
        }

        dstRow += BytesPerPixel;
        srcX += incrX;
        srcY += incrY;
    }
}

} // namespace detail

void copyRowDDA(
    void* dst,
    const ViewPort& src,
    int count,
    int_fixed srcX,
    int_fixed srcY,
    int_fixed incrX,
    int_fixed incrY
) {
    if (!src.isValid() || count <= 0) return;

    uint8_t* dstRow = static_cast<uint8_t*>(dst);
    const uint8_t* srcData = static_cast<const uint8_t*>(src.data);
    int32_t srcStride = src.stride;

    switch (getBytesPerPixel(src.formatID)) {
        case 8:
            detail::copyRowDDA_Impl<8>(dstRow, srcData, srcStride,
                srcX, srcY, incrX, incrY, count);
            break;
        case 4:
            detail::copyRowDDA_Impl<4>(dstRow, srcData, srcStride,
                srcX, srcY, incrX, incrY, count);
            break;
        case 3:
            detail::copyRowDDA_Impl<3>(dstRow, srcData, srcStride,
                srcX, srcY, incrX, incrY, count);
            break;
        case 2:
            detail::copyRowDDA_Impl<2>(dstRow, srcData, srcStride,
                srcX, srcY, incrX, incrY, count);
            break;
        case 1:
            detail::copyRowDDA_Impl<1>(dstRow, srcData, srcStride,
                srcX, srcY, incrX, incrY, count);
            break;
        default:
            break;
    }
}

void copyRowDDABilinear(
    void* dst,
    const ViewPort& src,
    int count,
    int_fixed srcX,
    int_fixed srcY,
    int_fixed incrX,
    int_fixed incrY
) {
    if (!src.isValid() || count <= 0) return;

    // 現状はRGBA8888専用、それ以外は最近傍フォールバック
    if (getBytesPerPixel(src.formatID) != 4) {
        copyRowDDA(dst, src, count, srcX, srcY, incrX, incrY);
        return;
    }

    constexpr int BPP = 4;  // RGBA8888 = 4 bytes per pixel
    uint8_t* dstRow = static_cast<uint8_t*>(dst);
    const uint8_t* srcData = static_cast<const uint8_t*>(src.data);
    const int32_t srcStride = src.stride;
    const int32_t srcLastX = src.width - 1;
    const int32_t srcLastY = src.height - 1;

    for (int i = 0; i < count; i++) {
        // 整数部（ピクセル座標）
        int32_t sx = srcX >> INT_FIXED_SHIFT;
        int32_t sy = srcY >> INT_FIXED_SHIFT;

        // 小数部を 0-255 に正規化（補間の重み）
        uint32_t fx = (static_cast<uint32_t>(srcX) >> 8) & 0xFF;
        uint32_t fy = (static_cast<uint32_t>(srcY) >> 8) & 0xFF;

        // 4点のポインタを取得（境界クランプ）
        const uint8_t* p00 = srcData
            + static_cast<size_t>(sy) * static_cast<size_t>(srcStride)
            + static_cast<size_t>(sx) * BPP;
        const uint8_t* p10 = (sx >= srcLastX) ? p00 : p00 + BPP;
        const uint8_t* p01 = (sy >= srcLastY) ? p00 : p00 + srcStride;
        const uint8_t* p11 = (sx >= srcLastX) ? p01 : p01 + BPP;

        // バイリニア補間（各チャンネル）
        uint32_t ifx = 256 - fx;
        uint32_t ify = 256 - fy;

        for (int c = 0; c < 4; c++) {
            uint32_t top    = p00[c] * ifx + p10[c] * fx;
            uint32_t bottom = p01[c] * ifx + p11[c] * fx;
            dstRow[c] = static_cast<uint8_t>((top * ify + bottom * fy) >> 16);
        }

        dstRow += BPP;
        srcX += incrX;
        srcY += incrY;
    }
}

void affineTransform(
    ViewPort& dst,
    const ViewPort& src,
    int_fixed invTx,
    int_fixed invTy,
    const Matrix2x2_fixed& invMatrix,
    int_fixed rowOffsetX,
    int_fixed rowOffsetY,
    int_fixed dxOffsetX,
    int_fixed dxOffsetY
) {
    if (!dst.isValid() || !src.isValid()) return;
    if (!invMatrix.valid) return;

    const int outW = dst.width;
    const int outH = dst.height;

    const int_fixed incrX = invMatrix.a;
    const int_fixed incrY = invMatrix.c;
    const int_fixed invB = invMatrix.b;
    const int_fixed invD = invMatrix.d;

    for (int dy = 0; dy < outH; dy++) {
        int_fixed rowBaseX = invB * dy + invTx + rowOffsetX;
        int_fixed rowBaseY = invD * dy + invTy + rowOffsetY;

        auto [xStart, xEnd] = transform::calcValidRange(incrX, rowBaseX, src.width, outW);
        auto [yStart, yEnd] = transform::calcValidRange(incrY, rowBaseY, src.height, outW);
        int dxStart = std::max({0, xStart, yStart});
        int dxEnd = std::min({outW - 1, xEnd, yEnd});

        if (dxStart > dxEnd) continue;

        int_fixed srcX = incrX * dxStart + rowBaseX + dxOffsetX;
        int_fixed srcY = incrY * dxStart + rowBaseY + dxOffsetY;
        int count = dxEnd - dxStart + 1;

        void* dstRow = dst.pixelAt(dxStart, dy);

        copyRowDDA(dstRow, src, count, srcX, srcY, incrX, incrY);
    }
}

} // namespace view_ops
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_VIEWPORT_H
