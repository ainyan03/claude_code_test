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
    uint8_t edgeFadeMask,
    const PixelAuxInfo* srcAux
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

// 1chバイリニア補間が使用可能かを判定するヘルパー
// Alpha8, Grayscale8 等の単一チャンネル非インデックスフォーマットに適用
// edgeFadeMask != 0 の場合、Alphaチャンネルのみ対応（値を直接0にできるため）
inline bool canUseSingleChannelBilinear(PixelFormatID formatID, uint8_t edgeFadeMask) {
    if (!formatID) return false;
    const int bpp = (formatID->bitsPerPixel + 7) / 8;
    return (bpp == 1)
        && (formatID->channelCount == 1)
        && !formatID->isIndexed
        && (edgeFadeMask == 0
            || formatID->channels[0].type == ChannelType::Alpha);
}

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

    // DDAParam を構築（copyRowDDAでは srcWidth/srcHeight/weights は使用しない）
    DDAParam param = { src.stride, 0, 0, srcX, srcY, incrX, incrY, nullptr, nullptr };

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
    const BilinearWeightXY* __restrict__ weightsXY,
    int count
) {
    for (int i = 0; i < count; ++i) {
        uint_fast8_t fy = weightsXY->fy;
        uint_fast8_t fx = weightsXY->fx;
        ++weightsXY;

        uint32_t f = static_cast<uint32_t>(fx) * ((256 - fy) | (static_cast<uint32_t>(fy) << 16));
        uint8_t q11f = static_cast<uint8_t>(f >> 24);
        uint8_t q10f = static_cast<uint8_t>(f >> 8);
        uint8_t q01f = static_cast<uint8_t>(((256 - fx) * fy) >> 8);
        uint_fast16_t q00f = 256 - (q11f + q01f + q10f);

        // 4点を32bitでロード（境界外ピクセルは事前にゼロ埋め済み）
        uint32_t q00 = quadPixels[0];
        uint32_t q10 = quadPixels[1];
        uint32_t q01 = quadPixels[2];
        uint32_t q11 = quadPixels[3];
        quadPixels += 4;

        // R,B（偶数バイト位置）をマスク
        uint32_t result_rb = q00f * (q00 & 0xFF00FF);
        // G,A（奇数バイト位置）をシフト＆マスク
        uint32_t result_ga = q00f * ((q00 >> 8) & 0xFF00FF);

        result_rb += q10f * (q10 & 0xFF00FF);
        result_ga += q10f * ((q10 >> 8) & 0xFF00FF);

        result_rb += q01f * (q01 & 0xFF00FF);
        result_ga += q01f * ((q01 >> 8) & 0xFF00FF);

        result_rb += q11f * (q11 & 0xFF00FF);
        result_ga += q11f * ((q11 >> 8) & 0xFF00FF);

        // 結果を出力（リトルエンディアン: G,Aは上位バイトが正しい位置に来る）
        auto dstBytes = reinterpret_cast<uint8_t*>(dst);
        *dst = result_ga;
        dstBytes[0] = static_cast<uint8_t>(result_rb >> 8);   // R
        dstBytes[2] = static_cast<uint8_t>(result_rb >> 24);  // B

        ++dst;
    }
}

// ============================================================================
// バイリニア補間関数（1チャンネル固定: Alpha8/Grayscale8用）
// ============================================================================
//
// copyQuadDDAで抽出した4ピクセルデータからバイリニア補間を実行する。
// 入力: quadPixels = [p00,p10,p01,p11] × count（各ピクセル1byte）
//       境界外ピクセルは呼び出し前にゼロ埋めされていること
// 出力: dst = 補間結果 × count（各1byte）

__attribute__((noinline))
static void bilinearBlend_1ch(
    uint8_t* __restrict__ dst,
    const uint8_t* __restrict__ quadPixels,
    const BilinearWeightXY* __restrict__ weightsXY,
    int count
) {
    for (int i = 0; i < count; ++i) {
        // 重み計算
        uint32_t q4 = *reinterpret_cast<const uint32_t*>(quadPixels);
        uint_fast32_t fy = weightsXY->fy;
        uint_fast32_t fx = weightsXY->fx;
        ++weightsXY;
        uint32_t left = (q4 & 0x00FF00FF); // 左側ピクセルの抽出
        uint32_t right = (q4 >> 8) & 0x00FF00FF; // 右側ピクセルの抽出
        uint32_t tb = left * (256-fx) + right * fx; // 左右補間
        uint32_t top = (tb & 0x0000FFFF) * (256-fy);
        uint32_t bottom = (tb >> 16) * fy;
        uint32_t combined = top + bottom;
        uint16_t result = static_cast<uint16_t>(combined >> 16);
        quadPixels += 4;
        dst[i] = static_cast<uint8_t>(result);
    }
}

// ============================================================================
// copyRowDDABilinear
// ============================================================================
//
// 処理フロー（チャンクループ）:
//   a. copyQuadDDA: 4ピクセル抽出 + edgeFlags生成
//   b. convertFormat: フォーマット変換（RGBA8_Straight以外の場合）
//   c. edgeFlags適用: 境界ピクセルのアルファを0化
//   d. bilinearBlend_RGBA8888: バイリニア補間
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

    // ========================================================================
    // 1chパス: Alpha8/Grayscale8等の単一チャンネルフォーマット向け高速パス
    // フォーマット変換不要、メモリ使用量1/4、演算量約1/4
    // ========================================================================
    if (canUseSingleChannelBilinear(src.formatID, edgeFadeMask)) {
        constexpr int CHUNK_SIZE = 64;

        // 1ch用一時バッファ（RGBA8パスの1/4サイズ）
        uint8_t quadBuffer1ch[CHUNK_SIZE * 4];       // 256 bytes（1byte × 4 × 64）
        BilinearWeightXY weightsXY[CHUNK_SIZE];      // 128 bytes
        uint8_t edgeFlagsChunk[CHUNK_SIZE];          // 64 bytes

        uint8_t* dstPtr = static_cast<uint8_t*>(dst);
        const uint8_t* srcData = static_cast<const uint8_t*>(src.data);

        DDAParam param = {
            src.stride, src.width, src.height,
            srcX, srcY, incrX, incrY,
            weightsXY, edgeFlagsChunk
        };

        for (int offset = 0; offset < count; offset += CHUNK_SIZE) {
            int chunk = (count - offset < CHUNK_SIZE) ? (count - offset) : CHUNK_SIZE;

            // 4ピクセル抽出（1bpp: copyQuadDDA出力がそのまま使える）
            src.formatID->copyQuadDDA(quadBuffer1ch, srcData, chunk, &param);

            // 境界ピクセルの値を0化（Alpha8のエッジフェード）
            if (edgeFadeMask) {
                for (int i = 0; i < chunk; ++i) {
                    uint8_t flags = edgeFlagsChunk[i] & edgeFadeMask;
                    if (flags) {
                        uint8_t* q = &quadBuffer1ch[i * 4];
                        if (flags & (EdgeFade_Left | EdgeFade_Top))    { q[0] = 0; }
                        if (flags & (EdgeFade_Right | EdgeFade_Top))   { q[1] = 0; }
                        if (flags & (EdgeFade_Left | EdgeFade_Bottom)) { q[2] = 0; }
                        if (flags & (EdgeFade_Right | EdgeFade_Bottom)){ q[3] = 0; }
                    }
                }
            }

            // 1chバイリニア補間
            bilinearBlend_1ch(dstPtr, quadBuffer1ch, weightsXY, chunk);

            dstPtr += chunk;
            param.srcX += incrX * chunk;
            param.srcY += incrY * chunk;
        }
        return;
    }

    // ========================================================================
    // RGBA8パス: 通常のマルチチャンネルフォーマット
    // ========================================================================

    // チャンク処理用定数
    constexpr int CHUNK_SIZE = 64;
    constexpr int RGBA8_BPP = 4;

    // 一時バッファ（スタック確保）
    // copyQuadDDA出力とconvertFormat出力を共有（末尾詰め配置でin-place変換可能）
    uint32_t quadBuffer[CHUNK_SIZE * 4];         // 1024 bytes（RGBA8888 × 4 × 64）
    BilinearWeightXY weightsXY[CHUNK_SIZE];      // 128 bytes (2 * 64)
    uint8_t edgeFlagsChunk[CHUNK_SIZE];          // 64 bytes（チャンク用）

    // 元フォーマットのBPP（末尾詰め配置用）
    // bit-packedの場合、copyQuadDDAはIndex8形式で出力するため、bpp=1とする
    const int srcBpp = (src.formatID->pixelsPerUnit > 1)
                     ? 1  // bit-packed: copyQuadDDAがIndex8で出力
                     : ((src.formatID->bitsPerPixel + 7) / 8);  // 通常フォーマット

    // フォーマット変換が必要な場合、ループ外で一度だけresolveConverter呼び出し
    // bit-packedの場合、copyQuadDDAの出力はIndex8形式なので、Index8→RGBA8の変換を使う
    FormatConverter converter;
    PixelFormatID converterSrcFormat = (src.formatID->pixelsPerUnit > 1)
                                     ? PixelFormatIDs::Index8 : src.formatID;
    if (converterSrcFormat != PixelFormatIDs::RGBA8_Straight) {
        converter = resolveConverter(converterSrcFormat, PixelFormatIDs::RGBA8_Straight, srcAux);
    }

    uint32_t* dstPtr = static_cast<uint32_t*>(dst);
    const uint8_t* srcData = static_cast<const uint8_t*>(src.data);

    // DDAParam を構築
    DDAParam param = {
        src.stride,
        src.width,
        src.height,
        srcX,
        srcY,
        incrX,
        incrY,
        weightsXY,
        edgeFlagsChunk
    };

    for (int offset = 0; offset < count; offset += CHUNK_SIZE) {
        int chunk = (count - offset < CHUNK_SIZE) ? (count - offset) : CHUNK_SIZE;

        // 4ピクセル抽出 + edgeFlags生成（末尾詰め配置でin-place変換可能）
        int srcQuadSize = srcBpp * 4 * chunk;
        int dstQuadSize = RGBA8_BPP * 4 * chunk;
        auto quadPtr = reinterpret_cast<uint8_t*>(quadBuffer) + (dstQuadSize - srcQuadSize);
        src.formatID->copyQuadDDA(quadPtr, srcData, chunk, &param);

        // フォーマット変換（必要な場合、in-place）
        if (converter) {
            converter(quadBuffer, quadPtr, chunk * 4);
        }
        uint32_t* quadRGBA = quadBuffer;

        // 境界ピクセルのアルファ0化（edgeFlagsに基づく）
        if (edgeFadeMask) {
            auto quad = reinterpret_cast<uint8_t*>(quadRGBA) + 3;
            for (int i = 0; i < chunk; ++i) {
                uint8_t flags = edgeFlagsChunk[i] & edgeFadeMask;
                if (flags) {
                    if (flags & (EdgeFade_Left | EdgeFade_Top)) { quad[0] = 0; }
                    if (flags & (EdgeFade_Right | EdgeFade_Top)) { quad[4] = 0; }
                    if (flags & (EdgeFade_Left | EdgeFade_Bottom)) { quad[8] = 0; }
                    if (flags & (EdgeFade_Right | EdgeFade_Bottom)) { quad[12] = 0; }
                }
                quad += 4 * 4;
            }
        }

        // バイリニア補間
        bilinearBlend_RGBA8888(dstPtr, quadRGBA, weightsXY, chunk);

        // 次のチャンクへ
        dstPtr += chunk;
        param.srcX += incrX * chunk;
        param.srcY += incrY * chunk;
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
