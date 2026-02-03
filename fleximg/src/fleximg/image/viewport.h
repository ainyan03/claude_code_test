#ifndef FLEXIMG_VIEWPORT_H
#define FLEXIMG_VIEWPORT_H

#include <cassert>
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
// copyQuadDDA → フォーマット変換 → bilinearBlend_RGBA8888 のパイプライン
// copyQuadDDA未対応フォーマットは最近傍にフォールバック
// edgeFadeMask: EdgeFadeFlagsの値。フェード有効な辺のみ境界ピクセルのアルファを0化
// srcAux: パレット情報等（Index8のパレット展開に使用）
void copyRowDDABilinear(
    void* dst,
    const ViewPort& src,
    int count,
    int_fixed srcX,
    int_fixed srcY,
    int_fixed incrX,
    int_fixed incrY,
    uint8_t edgeFadeMask = EdgeFade_All,  // デフォルト: 全辺フェードアウト有効
    const PixelAuxInfo* srcAux = nullptr  // パレット情報等
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

    // view_ops::copy は同一フォーマット間の矩形コピー専用。
    // 異フォーマット間変換は resolveConverter / convertFormat を直接使用すること。
    FLEXIMG_ASSERT(src.formatID == dst.formatID,
                   "view_ops::copy requires matching formats; use convertFormat for conversion");

    size_t bpp = static_cast<size_t>(dst.bytesPerPixel());
    for (int y = 0; y < height; ++y) {
        const uint8_t* srcRow = static_cast<const uint8_t*>(src.pixelAt(srcX, srcY + y));
        uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(dstX, dstY + y));
        std::memcpy(dstRow, srcRow, static_cast<size_t>(width) * bpp);
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

} // namespace view_ops

namespace view_ops {

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

    // DDAParam を構築（copyRowDDAでは srcWidth/srcHeight/weights/safe範囲 は使用しない）
    DDAParam param = { src.stride, 0, 0, srcX, srcY, incrX, incrY, nullptr, 0, 0, 0 };

    // フォーマットの関数ポインタを呼び出し
    if (src.formatID && src.formatID->copyRowDDA) {
        src.formatID->copyRowDDA(
            static_cast<uint8_t*>(dst),
            static_cast<const uint8_t*>(src.data),
            count,
            &param
        );
    }
}

// ============================================================================
// バイリニア補間関数（RGBA8888固定）
// ============================================================================
//
// copyQuadDDAで抽出した4ピクセルデータからバイリニア補間を実行する。
// 入力: quadPixels = [p00,p10,p01,p11] × count（各ピクセル4bytes、RGBA8888）
//       境界外ピクセルは呼び出し前にゼロ埋めされていること
// 出力: dst = 補間結果 × count（各4bytes、RGBA8888）
// 注意: 両ポインタは4バイトアライメントが必要

__attribute__((noinline))
static void bilinearBlend_RGBA8888(
    uint32_t* __restrict__ dst,
    const uint32_t* __restrict__ quadPixels,
    const BilinearWeight* __restrict__ weights,
    int count
) {
    for (int i = 0; i < count; ++i) {
        // 4点を32bitでロード（境界外ピクセルは事前にゼロ埋め済み）
        uint32_t q00 = quadPixels[0];
        uint32_t q10 = quadPixels[1];
        uint32_t q01 = quadPixels[2];
        uint32_t q11 = quadPixels[3];
        quadPixels += 4;

        uint32_t fx = weights->fx;
        uint32_t fy = weights->fy;
        ++weights;
        uint32_t ifx = 256 - fx;
        uint32_t ify = 256 - fy;

        // R,B（偶数バイト位置）をマスク
        uint32_t q00_rb = q00 & 0xFF00FF;
        uint32_t q10_rb = q10 & 0xFF00FF;
        uint32_t q01_rb = q01 & 0xFF00FF;
        uint32_t q11_rb = q11 & 0xFF00FF;

        // G,A（奇数バイト位置）をシフト＆マスク
        uint32_t q00_ga = (q00 >> 8) & 0xFF00FF;
        uint32_t q10_ga = (q10 >> 8) & 0xFF00FF;
        uint32_t q01_ga = (q01 >> 8) & 0xFF00FF;
        uint32_t q11_ga = (q11 >> 8) & 0xFF00FF;

        // X方向補間（R,B同時）→ 8bit精度に丸める
        uint32_t top_rb = ((q00_rb * ifx + q10_rb * fx) >> 8) & 0xFF00FF;
        uint32_t bottom_rb = ((q01_rb * ifx + q11_rb * fx) >> 8) & 0xFF00FF;

        // X方向補間（G,A同時）→ 8bit精度に丸める
        uint32_t top_ga = ((q00_ga * ifx + q10_ga * fx) >> 8) & 0xFF00FF;
        uint32_t bottom_ga = ((q01_ga * ifx + q11_ga * fx) >> 8) & 0xFF00FF;

        // Y方向補間（R,B同時）
        uint32_t result_rb = (top_rb * ify + bottom_rb * fy) >> 8;

        // Y方向補間（G,A同時）- 16bit×2形式のまま保持
        uint32_t result_ga = top_ga * ify + bottom_ga * fy;

        // 結果を出力（リトルエンディアン: G,Aは上位バイトが正しい位置に来る）
        auto dstBytes = reinterpret_cast<uint8_t*>(dst);
        *dst = result_ga;
        dstBytes[0] = static_cast<uint8_t>(result_rb);        // R
        dstBytes[2] = static_cast<uint8_t>(result_rb >> 16);  // B

        ++dst;
    }
}

// ============================================================================
// copyRowDDABilinear - 新アーキテクチャ
// ============================================================================
//
// 処理フロー:
// 1. copyQuadDDA: 4ピクセル抽出（元フォーマット）
// 2. convertFormat: フォーマット変換（RGBA8_Straight以外の場合）
// 3. bilinearBlend_RGBA8888: バイリニア補間
//

void copyRowDDABilinear(
    void* dst,
    const ViewPort& src,
    int count,
    int_fixed srcX,
    int_fixed srcY,
    int_fixed incrX,
    int_fixed incrY,
    uint8_t edgeFadeMask,
    const PixelAuxInfo* srcAux
) {
    if (!src.isValid() || count <= 0) return;

    // copyQuadDDA未対応フォーマットは最近傍フォールバック
    if (!src.formatID || !src.formatID->copyQuadDDA) {
        copyRowDDA(dst, src, count, srcX, srcY, incrX, incrY);
        return;
    }

    // チャンク処理用定数
    constexpr int CHUNK_SIZE = 64;

    // 一時バッファ（スタック確保、4バイトアライメント保証）
    uint32_t quadBuffer[CHUNK_SIZE * 4];         // 最大1024 bytes（4bpp × 4 × 64）
    BilinearWeight weights[CHUNK_SIZE];          // 128 bytes
    uint32_t convertedQuad[CHUNK_SIZE * 4];      // 変換用（RGBA8888、1024 bytes）

    // RGBA8_Straightかどうか判定（変換スキップ用）
    const bool needsConversion = (src.formatID != PixelFormatIDs::RGBA8_Straight);

    uint32_t* dstPtr = static_cast<uint32_t*>(dst);
    const uint8_t* srcData = static_cast<const uint8_t*>(src.data);

    for (int offset = 0; offset < count; offset += CHUNK_SIZE) {
        int chunk = (count - offset < CHUNK_SIZE) ? (count - offset) : CHUNK_SIZE;

        // DDAParam を構築
        DDAParam param = {
            src.stride,
            src.width,
            src.height,
            srcX,
            srcY,
            incrX,
            incrY,
            weights,
            0, 0, 0,  // headCount, safeCount, tailCount（prepareCopyQuadDDAで設定）
            edgeFadeMask  // エッジフェードマスク
        };
        param.prepareCopyQuadDDA(chunk);

        // 関数A: 4ピクセル抽出
        auto quadPtr = reinterpret_cast<uint8_t*>(quadBuffer);
        src.formatID->copyQuadDDA(quadPtr, srcData, chunk, &param);

        // フォーマット変換（必要な場合）
        // Index8等のパレットフォーマットはsrcAuxにパレット情報が必要
        uint32_t* quadRGBA = quadBuffer;
        if (needsConversion) {
            auto convertedPtr = reinterpret_cast<uint8_t*>(convertedQuad);
            convertFormat(quadPtr, src.formatID,
                          convertedPtr, PixelFormatIDs::RGBA8_Straight,
                          chunk * 4, srcAux, nullptr);
            quadRGBA = convertedQuad;
        }

        // 境界ピクセルの事前ゼロ埋め（edgeFlagsに基づく）
        // edgeFlagsはcopyQuadDDA_loop内でedgeFadeMaskと照合済み
        if (param.headCount || param.tailCount) {
            auto quad = reinterpret_cast<uint8_t*>(quadRGBA) + 3;
            auto wptr = param.weights;
            for (int step = 0; step < 2; ++step) {
                uint_fast16_t n = step ? param.tailCount : param.headCount;
                for (uint_fast16_t i = 0; i < n; ++i) {
                    uint8_t flags = wptr->edgeFlags;
                    ++wptr;

                    // RGBA8888のAチャネル位置に対応
                    if (flags & 0x01) { quad[0] = 0; }
                    if (flags & 0x02) { quad[4] = 0; }
                    if (flags & 0x04) { quad[8] = 0; }
                    if (flags & 0x08) { quad[12] = 0; }
                    quad += 4 * 4;
                }
                wptr += param.safeCount;
                quad += (param.safeCount * 4 * 4);
            }
        }

        // 関数B: バイリニア補間（edgeFlagsチェック不要）
        bilinearBlend_RGBA8888(dstPtr, quadRGBA, weights, chunk);

        // 次のチャンクへ
        dstPtr += chunk;  // 出力は常にRGBA8888（1 uint32_t per pixel）
        srcX += incrX * chunk;
        srcY += incrY * chunk;
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
