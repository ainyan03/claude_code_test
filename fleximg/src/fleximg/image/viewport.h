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

// BPP → ネイティブ型マッピング（ロード・ストア分離用）
// BPP 1, 2, 4 はネイティブ型で直接ロード・ストア可能
// BPP 3 はネイティブ型が存在しないため byte 単位で処理
template<size_t BPP> struct PixelType {};
template<> struct PixelType<1> { using type = uint8_t; };
template<> struct PixelType<2> { using type = uint16_t; };
template<> struct PixelType<4> { using type = uint32_t; };

// DDA行転写: Y座標一定パス（ソース行が同一の場合）
// srcRowBase = srcData + sy * srcStride（呼び出し前に計算済み）
// 4ピクセル単位展開でループオーバーヘッドを削減
template<size_t BytesPerPixel>
inline void copyRowDDA_ConstY(
    uint8_t* __restrict__ dstRow,
    const uint8_t* __restrict__ srcRowBase,
    int_fixed srcX,
    int_fixed incrX,
    int count
) {
    // 端数を先に処理し、4ピクセルループを最後に連続実行する
    int remainder = count & 3;

    if constexpr (BytesPerPixel == 3) {
        for (int i = 0; i < remainder; i++) {
            // BPP==3: byte単位でロード・ストア分離（3bytes × 4pixels）
            size_t s0 = static_cast<size_t>(srcX >> INT_FIXED_SHIFT) * 3; srcX += incrX;
            uint8_t p00 = srcRowBase[s0], p01 = srcRowBase[s0+1], p02 = srcRowBase[s0+2];
            dstRow[0]  = p00; dstRow[1]  = p01; dstRow[2]  = p02;
            dstRow += BytesPerPixel;
        }
        int count4 = count >> 2;
        for (int i = 0; i < count4; i++) {
            // BPP==3: byte単位でロード・ストア分離（3bytes × 4pixels）
            size_t s0 = static_cast<size_t>(srcX >> INT_FIXED_SHIFT) * 3; srcX += incrX;
            size_t s1 = static_cast<size_t>(srcX >> INT_FIXED_SHIFT) * 3; srcX += incrX;
            size_t s2 = static_cast<size_t>(srcX >> INT_FIXED_SHIFT) * 3; srcX += incrX;
            size_t s3 = static_cast<size_t>(srcX >> INT_FIXED_SHIFT) * 3; srcX += incrX;
            uint8_t p00 = srcRowBase[s0], p01 = srcRowBase[s0+1], p02 = srcRowBase[s0+2];
            dstRow[0]  = p00; dstRow[1]  = p01; dstRow[2]  = p02;
            uint8_t p10 = srcRowBase[s1], p11 = srcRowBase[s1+1], p12 = srcRowBase[s1+2];
            dstRow[3]  = p10; dstRow[4]  = p11; dstRow[5]  = p12;
            uint8_t p20 = srcRowBase[s2], p21 = srcRowBase[s2+1], p22 = srcRowBase[s2+2];
            dstRow[6]  = p20; dstRow[7]  = p21; dstRow[8]  = p22;
            uint8_t p30 = srcRowBase[s3], p31 = srcRowBase[s3+1], p32 = srcRowBase[s3+2];
            dstRow[9]  = p30; dstRow[10] = p31; dstRow[11] = p32;
            dstRow += BytesPerPixel * 4;
        }
    } else {
        using T = typename PixelType<BytesPerPixel>::type;
        auto src = reinterpret_cast<const T*>(srcRowBase);
        auto dst = reinterpret_cast<T*>(dstRow);
        for (int i = 0; i < remainder; i++) {
            // BPP 1, 2, 4: ネイティブ型でロード・ストア分離
            auto p0 = src[srcX >> INT_FIXED_SHIFT]; srcX += incrX;
            dst[0] = p0;
            dst += 1;
        }
        int count4 = count >> 2;
        for (int i = 0; i < count4; i++) {
            // BPP 1, 2, 4: ネイティブ型でロード・ストア分離
            auto p0 = src[srcX >> INT_FIXED_SHIFT]; srcX += incrX;
            auto p1 = src[srcX >> INT_FIXED_SHIFT]; srcX += incrX;
            auto p2 = src[srcX >> INT_FIXED_SHIFT]; srcX += incrX;
            auto p3 = src[srcX >> INT_FIXED_SHIFT]; srcX += incrX;
            dst[0] = p0;
            dst[1] = p1;
            dst[2] = p2;
            dst[3] = p3;
            dst += 4;
        }
    }
}

// DDA行転写: X座標一定パス（ソース列が同一の場合）
// srcColBase = srcData + sx * BPP（呼び出し前に加算済み）
// 4ピクセル単位展開でループオーバーヘッドを削減
template<size_t BytesPerPixel>
inline void copyRowDDA_ConstX(
    uint8_t* __restrict__ dstRow,
    const uint8_t* __restrict__ srcColBase,
    int32_t srcStride,
    int_fixed srcY,
    int_fixed incrY,
    int count
) {
    // 端数を先に処理し、4ピクセルループを最後に連続実行する
    int remainder = count & 3;
    if constexpr (BytesPerPixel == 3) {
        for (int i = 0; i < remainder; i++) {
            int32_t sy = srcY >> INT_FIXED_SHIFT; srcY += incrY;
            const uint8_t* r = srcColBase + static_cast<size_t>(sy * srcStride);
            uint8_t p0 = r[0], p1 = r[1], p2 = r[2];
            dstRow[0] = p0; dstRow[1] = p1; dstRow[2] = p2;
            dstRow += BytesPerPixel;
        }
    } else {
        using T = typename PixelType<BytesPerPixel>::type;
        auto dst = reinterpret_cast<T*>(dstRow);
        for (int i = 0; i < remainder; i++) {
            int32_t sy = srcY >> INT_FIXED_SHIFT; srcY += incrY;
            auto p = *reinterpret_cast<const T*>(srcColBase + static_cast<size_t>(sy * srcStride));
            dst[0] = p;
            dst += 1;
        }
        dstRow = reinterpret_cast<uint8_t*>(dst);
    }

    int count4 = count >> 2;
    for (int i = 0; i < count4; i++) {
        int32_t sy0 = srcY >> INT_FIXED_SHIFT; srcY += incrY;
        int32_t sy1 = srcY >> INT_FIXED_SHIFT; srcY += incrY;
        int32_t sy2 = srcY >> INT_FIXED_SHIFT; srcY += incrY;
        int32_t sy3 = srcY >> INT_FIXED_SHIFT; srcY += incrY;

        if constexpr (BytesPerPixel == 3) {
            // BPP==3: byte単位でピクセルごとにロード・ストア
            const uint8_t* r0 = srcColBase + static_cast<size_t>(sy0 * srcStride);
            uint8_t p00 = r0[0], p01 = r0[1], p02 = r0[2];
            dstRow[0] = p00; dstRow[1] = p01; dstRow[2] = p02;
            const uint8_t* r1 = srcColBase + static_cast<size_t>(sy1 * srcStride);
            uint8_t p10 = r1[0], p11 = r1[1], p12 = r1[2];
            dstRow[3] = p10; dstRow[4] = p11; dstRow[5] = p12;
            const uint8_t* r2 = srcColBase + static_cast<size_t>(sy2 * srcStride);
            uint8_t p20 = r2[0], p21 = r2[1], p22 = r2[2];
            dstRow[6] = p20; dstRow[7] = p21; dstRow[8] = p22;
            const uint8_t* r3 = srcColBase + static_cast<size_t>(sy3 * srcStride);
            uint8_t p30 = r3[0], p31 = r3[1], p32 = r3[2];
            dstRow[9] = p30; dstRow[10] = p31; dstRow[11] = p32;
        } else {
            // BPP 1, 2, 4: ネイティブ型でロード・ストア分離
            using T = typename PixelType<BytesPerPixel>::type;
            auto p0 = *reinterpret_cast<const T*>(srcColBase + static_cast<size_t>(sy0 * srcStride));
            auto p1 = *reinterpret_cast<const T*>(srcColBase + static_cast<size_t>(sy1 * srcStride));
            auto p2 = *reinterpret_cast<const T*>(srcColBase + static_cast<size_t>(sy2 * srcStride));
            auto p3 = *reinterpret_cast<const T*>(srcColBase + static_cast<size_t>(sy3 * srcStride));
            reinterpret_cast<T*>(dstRow)[0] = p0;
            reinterpret_cast<T*>(dstRow)[1] = p1;
            reinterpret_cast<T*>(dstRow)[2] = p2;
            reinterpret_cast<T*>(dstRow)[3] = p3;
        }
        dstRow += BytesPerPixel * 4;
    }
}

// DDA行転写の汎用実装（両方非ゼロ、回転を含む変換）
// 4ピクセル単位展開でループオーバーヘッドを削減
template<size_t BytesPerPixel>
inline void copyRowDDA_Impl(
    uint8_t* __restrict__ dstRow,
    const uint8_t* __restrict__ srcData,
    int32_t srcStride,
    int_fixed srcX,
    int_fixed srcY,
    int_fixed incrX,
    int_fixed incrY,
    int count
) {
    // 端数を先に処理し、4ピクセルループを最後に連続実行する
    int remainder = count & 3;
    if constexpr (BytesPerPixel == 3) {
        for (int i = 0; i < remainder; i++) {
            int32_t sx = srcX >> INT_FIXED_SHIFT;
            int32_t sy = srcY >> INT_FIXED_SHIFT;
            srcX += incrX; srcY += incrY;
            const uint8_t* r = srcData + static_cast<size_t>(sy * srcStride) + static_cast<size_t>(sx) * 3;
            uint8_t p0 = r[0], p1 = r[1], p2 = r[2];
            dstRow[0] = p0; dstRow[1] = p1; dstRow[2] = p2;
            dstRow += BytesPerPixel;
        }
    } else {
        using T = typename PixelType<BytesPerPixel>::type;
        auto dst = reinterpret_cast<T*>(dstRow);
        for (int i = 0; i < remainder; i++) {
            int32_t sx = srcX >> INT_FIXED_SHIFT;
            int32_t sy = srcY >> INT_FIXED_SHIFT;
            srcX += incrX; srcY += incrY;
            auto p = reinterpret_cast<const T*>(srcData + static_cast<size_t>(sy * srcStride))[sx];
            dst[0] = p;
            dst += 1;
        }
        dstRow = reinterpret_cast<uint8_t*>(dst);
    }

    int count4 = count >> 2;
    for (int i = 0; i < count4; i++) {
        int32_t sx0 = srcX >> INT_FIXED_SHIFT;
        int32_t sy0 = srcY >> INT_FIXED_SHIFT;
        srcX += incrX; srcY += incrY;
        int32_t sx1 = srcX >> INT_FIXED_SHIFT;
        int32_t sy1 = srcY >> INT_FIXED_SHIFT;
        srcX += incrX; srcY += incrY;
        int32_t sx2 = srcX >> INT_FIXED_SHIFT;
        int32_t sy2 = srcY >> INT_FIXED_SHIFT;
        srcX += incrX; srcY += incrY;
        int32_t sx3 = srcX >> INT_FIXED_SHIFT;
        int32_t sy3 = srcY >> INT_FIXED_SHIFT;
        srcX += incrX; srcY += incrY;

        if constexpr (BytesPerPixel == 3) {
            // BPP==3: byte単位でピクセルごとにロード・ストア
            const uint8_t* r0 = srcData + static_cast<size_t>(sy0 * srcStride) + static_cast<size_t>(sx0) * 3;
            uint8_t p00 = r0[0], p01 = r0[1], p02 = r0[2];
            dstRow[0] = p00; dstRow[1] = p01; dstRow[2] = p02;
            const uint8_t* r1 = srcData + static_cast<size_t>(sy1 * srcStride) + static_cast<size_t>(sx1) * 3;
            uint8_t p10 = r1[0], p11 = r1[1], p12 = r1[2];
            dstRow[3] = p10; dstRow[4] = p11; dstRow[5] = p12;
            const uint8_t* r2 = srcData + static_cast<size_t>(sy2 * srcStride) + static_cast<size_t>(sx2) * 3;
            uint8_t p20 = r2[0], p21 = r2[1], p22 = r2[2];
            dstRow[6] = p20; dstRow[7] = p21; dstRow[8] = p22;
            const uint8_t* r3 = srcData + static_cast<size_t>(sy3 * srcStride) + static_cast<size_t>(sx3) * 3;
            uint8_t p30 = r3[0], p31 = r3[1], p32 = r3[2];
            dstRow[9] = p30; dstRow[10] = p31; dstRow[11] = p32;
        } else {
            // BPP 1, 2, 4: ネイティブ型でロード・ストア分離
            using T = typename PixelType<BytesPerPixel>::type;
            auto p0 = reinterpret_cast<const T*>(srcData + static_cast<size_t>(sy0 * srcStride))[sx0];
            auto p1 = reinterpret_cast<const T*>(srcData + static_cast<size_t>(sy1 * srcStride))[sx1];
            auto p2 = reinterpret_cast<const T*>(srcData + static_cast<size_t>(sy2 * srcStride))[sx2];
            auto p3 = reinterpret_cast<const T*>(srcData + static_cast<size_t>(sy3 * srcStride))[sx3];
            reinterpret_cast<T*>(dstRow)[0] = p0;
            reinterpret_cast<T*>(dstRow)[1] = p1;
            reinterpret_cast<T*>(dstRow)[2] = p2;
            reinterpret_cast<T*>(dstRow)[3] = p3;
        }
        dstRow += BytesPerPixel * 4;
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
    auto bpp = getBytesPerPixel(src.formatID);

    // ソース座標の整数部が全ピクセルで同一か判定（座標は呼び出し側で非負が保証済み）
    int32_t syFirst = srcY >> INT_FIXED_SHIFT;
    int32_t syLast  = (srcY + incrY * count) >> INT_FIXED_SHIFT;
    int32_t sxFirst = srcX >> INT_FIXED_SHIFT;
    int32_t sxLast  = (srcX + incrX * count) >> INT_FIXED_SHIFT;

    if (syFirst == syLast) {
        // Y座標一定パス（高頻度: 回転なし拡大縮小・平行移動、微小Y変動も含む）
        const uint8_t* srcRowBase = srcData
            + static_cast<size_t>(syFirst * srcStride);
        switch (bpp) {
            case 4: detail::copyRowDDA_ConstY<4>(dstRow, srcRowBase, srcX, incrX, count); break;
            case 3: detail::copyRowDDA_ConstY<3>(dstRow, srcRowBase, srcX, incrX, count); break;
            case 2: detail::copyRowDDA_ConstY<2>(dstRow, srcRowBase, srcX, incrX, count); break;
            case 1: detail::copyRowDDA_ConstY<1>(dstRow, srcRowBase, srcX, incrX, count); break;
            default: break;
        }
    } else if (sxFirst == sxLast) {
        // X座標一定パス（微小X変動も含む）
        const uint8_t* srcColBase = srcData
            + static_cast<size_t>(sxFirst * bpp);
        switch (bpp) {
            case 4: detail::copyRowDDA_ConstX<4>(dstRow, srcColBase, srcStride, srcY, incrY, count); break;
            case 3: detail::copyRowDDA_ConstX<3>(dstRow, srcColBase, srcStride, srcY, incrY, count); break;
            case 2: detail::copyRowDDA_ConstX<2>(dstRow, srcColBase, srcStride, srcY, incrY, count); break;
            case 1: detail::copyRowDDA_ConstX<1>(dstRow, srcColBase, srcStride, srcY, incrY, count); break;
            default: break;
        }
    } else {
        // 汎用パス（回転を含む変換）
        switch (bpp) {
            case 4: detail::copyRowDDA_Impl<4>(dstRow, srcData, srcStride, srcX, srcY, incrX, incrY, count); break;
            case 3: detail::copyRowDDA_Impl<3>(dstRow, srcData, srcStride, srcX, srcY, incrX, incrY, count); break;
            case 2: detail::copyRowDDA_Impl<2>(dstRow, srcData, srcStride, srcX, srcY, incrX, incrY, count); break;
            case 1: detail::copyRowDDA_Impl<1>(dstRow, srcData, srcStride, srcX, srcY, incrX, incrY, count); break;
            default: break;
        }
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
