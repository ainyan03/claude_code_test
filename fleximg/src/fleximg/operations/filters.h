#ifndef FLEXIMG_OPERATIONS_FILTERS_H
#define FLEXIMG_OPERATIONS_FILTERS_H

#include "../core/common.h"
#include "../image/viewport.h"

namespace FLEXIMG_NAMESPACE {
namespace filters {

// ========================================================================
// ラインフィルタ共通定義
// ========================================================================
//
// スキャンライン処理用の共通パラメータ構造体と関数型です。
// FilterNodeBase で使用され、派生クラスの共通化を実現します。
//

/// ラインフィルタ共通パラメータ
struct LineFilterParams {
    float value1 = 0.0f;  ///< brightness amount, alpha scale 等
    float value2 = 0.0f;  ///< 将来の拡張用
};

/// ラインフィルタ関数型（RGBA8_Straight形式、インプレース処理）
using LineFilterFunc = void(*)(uint8_t* pixels, int count, const LineFilterParams& params);

// ========================================================================
// ラインフィルタ関数（スキャンライン処理用）
// ========================================================================
//
// 1行分のピクセルデータ（RGBA8_Straight形式）を処理します。
// インプレース編集を前提とし、入力と出力は同一バッファです。
//

/// 明るさ調整（ラインフィルタ版）
/// params.value1: 明るさ調整量（-1.0〜1.0、0.5で+127相当）
void brightness_line(uint8_t* pixels, int count, const LineFilterParams& params);

/// グレースケール変換（ラインフィルタ版）
/// パラメータ未使用（将来の拡張用に引数は維持）
void grayscale_line(uint8_t* pixels, int count, const LineFilterParams& params);

/// アルファ調整（ラインフィルタ版）
/// params.value1: アルファスケール（0.0〜1.0）
void alpha_line(uint8_t* pixels, int count, const LineFilterParams& params);

// ========================================================================
// フィルタ操作（ViewPort版、移行期間中は維持）
// ========================================================================
//
// 画像に対するフィルタ処理を行います。
// 入力と出力は同じサイズ・フォーマットを前提とします。
// 8bit RGBA (Straight) 形式で処理します。
//

// ------------------------------------------------------------------------
// brightness - 明るさ調整
// ------------------------------------------------------------------------
//
// RGBチャンネルに一定値を加算します。
//
// パラメータ:
// - dst: 出力バッファ（事前確保済み）
// - src: 入力バッファ
// - amount: 明るさ調整量（-1.0〜1.0、0.5で+127相当）
//
void brightness(ViewPort& dst, const ViewPort& src, float amount);

// ------------------------------------------------------------------------
// grayscale - グレースケール変換
// ------------------------------------------------------------------------
//
// RGB平均法によるグレースケール変換を行います。
//
void grayscale(ViewPort& dst, const ViewPort& src);

// ------------------------------------------------------------------------
// boxBlur - ボックスブラー
// ------------------------------------------------------------------------
//
// 入力画像の範囲外を透明（α=0）として扱うボックスブラーです。
// α加重平均を使用し、透明ピクセルの色成分が結果に影響しないようにします。
// スライディングウィンドウ方式で O(width×height) の計算量を実現。
//
// パラメータ:
// - dst: 出力バッファ（事前確保済み、srcと異なるサイズ可）
// - src: 入力バッファ
// - radius: ブラー半径（ピクセル）
// - srcOffsetX: srcのdst座標系でのX方向オフセット（デフォルト0）
// - srcOffsetY: srcのdst座標系でのY方向オフセット（デフォルト0）
//
// 座標系:
//   dst座標 (dx, dy) に対応する src座標は (dx - srcOffsetX, dy - srcOffsetY)
//   src範囲外のピクセルは透明として扱われる
//
void boxBlur(ViewPort& dst, const ViewPort& src, int radius,
             int srcOffsetX = 0, int srcOffsetY = 0);

// ------------------------------------------------------------------------
// alpha - アルファ調整
// ------------------------------------------------------------------------
//
// アルファチャンネルにスケール係数を乗算します。
//
// パラメータ:
// - dst: 出力バッファ（事前確保済み）
// - src: 入力バッファ
// - scale: アルファスケール（0.0〜1.0）
//
void alpha(ViewPort& dst, const ViewPort& src, float scale);

} // namespace filters
} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

#include "../image/image_buffer.h"
#include <algorithm>
#include <cstdint>

namespace FLEXIMG_NAMESPACE {
namespace filters {

// ========================================================================
// ラインフィルタ関数（スキャンライン処理用）
// ========================================================================

void brightness_line(uint8_t* pixels, int count, const LineFilterParams& params) {
    int adjustment = static_cast<int>(params.value1 * 255.0f);

    for (int x = 0; x < count; x++) {
        int pixelOffset = x * 4;
        // RGB各チャンネルに明るさ調整を適用
        for (int c = 0; c < 3; c++) {
            int value = static_cast<int>(pixels[pixelOffset + c]) + adjustment;
            pixels[pixelOffset + c] = static_cast<uint8_t>(std::max(0, std::min(255, value)));
        }
        // Alphaはそのまま維持
    }
}

void grayscale_line(uint8_t* pixels, int count, const LineFilterParams& params) {
    (void)params;  // 将来の拡張用に引数は維持

    for (int x = 0; x < count; x++) {
        int pixelOffset = x * 4;
        // グレースケール変換（平均法）
        uint8_t gray = static_cast<uint8_t>(
            (static_cast<uint16_t>(pixels[pixelOffset]) +
             static_cast<uint16_t>(pixels[pixelOffset + 1]) +
             static_cast<uint16_t>(pixels[pixelOffset + 2])) / 3
        );
        pixels[pixelOffset]     = gray;  // R
        pixels[pixelOffset + 1] = gray;  // G
        pixels[pixelOffset + 2] = gray;  // B
        // Alphaはそのまま維持
    }
}

void alpha_line(uint8_t* pixels, int count, const LineFilterParams& params) {
    uint32_t alphaScale = static_cast<uint32_t>(params.value1 * 256.0f);

    for (int x = 0; x < count; x++) {
        int pixelOffset = x * 4;
        // RGBはそのまま、Alphaのみスケール
        uint32_t a = pixels[pixelOffset + 3];
        pixels[pixelOffset + 3] = static_cast<uint8_t>((a * alphaScale) >> 8);
    }
}

// ========================================================================
// ViewPort版フィルタ関数（移行期間中は維持）
// ========================================================================

// ========================================================================
// brightness - 明るさ調整
// ========================================================================

void brightness(ViewPort& dst, const ViewPort& src, float amount) {
    if (!dst.isValid() || !src.isValid()) return;
    if (dst.width != src.width || dst.height != src.height) return;

    int adjustment = static_cast<int>(amount * 255.0f);

    for (int y = 0; y < src.height; y++) {
        const uint8_t* srcRow = static_cast<const uint8_t*>(src.pixelAt(0, y));
        uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(0, y));

        for (int x = 0; x < src.width; x++) {
            int pixelOffset = x * 4;
            // RGB各チャンネルに明るさ調整を適用
            for (int c = 0; c < 3; c++) {
                int value = static_cast<int>(srcRow[pixelOffset + c]) + adjustment;
                dstRow[pixelOffset + c] = static_cast<uint8_t>(std::max(0, std::min(255, value)));
            }
            // Alphaはそのままコピー
            dstRow[pixelOffset + 3] = srcRow[pixelOffset + 3];
        }
    }
}

// ========================================================================
// grayscale - グレースケール変換
// ========================================================================

void grayscale(ViewPort& dst, const ViewPort& src) {
    if (!dst.isValid() || !src.isValid()) return;
    if (dst.width != src.width || dst.height != src.height) return;

    for (int y = 0; y < src.height; y++) {
        const uint8_t* srcRow = static_cast<const uint8_t*>(src.pixelAt(0, y));
        uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(0, y));

        for (int x = 0; x < src.width; x++) {
            int pixelOffset = x * 4;
            // グレースケール変換（平均法）
            uint8_t gray = static_cast<uint8_t>(
                (static_cast<uint16_t>(srcRow[pixelOffset]) +
                 static_cast<uint16_t>(srcRow[pixelOffset + 1]) +
                 static_cast<uint16_t>(srcRow[pixelOffset + 2])) / 3
            );
            dstRow[pixelOffset]     = gray;  // R
            dstRow[pixelOffset + 1] = gray;  // G
            dstRow[pixelOffset + 2] = gray;  // B
            dstRow[pixelOffset + 3] = srcRow[pixelOffset + 3];  // Alpha
        }
    }
}

// ========================================================================
// boxBlur - ボックスブラー
// ========================================================================
//
// スライディングウィンドウ方式 + α加重平均
// 計算量: O(width × height) - radius に依存しない
//

void boxBlur(ViewPort& dst, const ViewPort& src, int radius,
             int srcOffsetX, int srcOffsetY) {
    if (!dst.isValid()) return;
    if (radius <= 0) {
        // 半径0の場合: srcをdstにコピー（オフセット考慮）
        // まず全体を透明に
        for (int y = 0; y < dst.height; y++) {
            uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(0, y));
            std::memset(dstRow, 0, static_cast<size_t>(dst.width) * 4);
        }
        if (src.isValid()) {
            // srcの範囲をコピー
            int copyX = std::max(0, srcOffsetX);
            int copyY = std::max(0, srcOffsetY);
            int srcStartX = std::max(0, -srcOffsetX);
            int srcStartY = std::max(0, -srcOffsetY);
            int copyW = std::min(dst.width - copyX, src.width - srcStartX);
            int copyH = std::min(dst.height - copyY, src.height - srcStartY);
            if (copyW > 0 && copyH > 0) {
                view_ops::copy(dst, copyX, copyY, src, srcStartX, srcStartY, copyW, copyH);
            }
        }
        return;
    }

    int dstW = dst.width;
    int dstH = dst.height;
    int srcW = src.isValid() ? src.width : 0;
    int srcH = src.isValid() ? src.height : 0;
    // カーネルサイズ（radius*2+1、int16_t範囲で十分）
    auto count = static_cast<uint_fast16_t>(2 * radius + 1);

    // 中間バッファ（水平ブラー結果）- dst と同サイズ
    ImageBuffer temp(dstW, dstH, dst.formatID);
    ViewPort tempView = temp.view();

    // ========================================
    // パス1: 水平ブラー（スライディングウィンドウ）
    // ========================================
    for (int y = 0; y < dstH; y++) {
        int srcY = y - srcOffsetY;
        uint8_t* tempRow = static_cast<uint8_t*>(tempView.pixelAt(0, y));

        // src範囲外の行は全透明
        if (srcY < 0 || srcY >= srcH || !src.isValid()) {
            std::memset(tempRow, 0, static_cast<size_t>(dstW) * 4);
            continue;
        }

        const uint8_t* srcRow = static_cast<const uint8_t*>(src.pixelAt(0, srcY));
        uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;

        // 初期化: x=0 のウィンドウ [-radius, radius]
        for (int nx = -radius; nx <= radius; nx++) {
            int srcX = nx - srcOffsetX;
            if (srcX >= 0 && srcX < srcW) {
                int off = srcX * 4;
                uint32_t a = srcRow[off + 3];
                sumR += srcRow[off] * a;
                sumG += srcRow[off + 1] * a;
                sumB += srcRow[off + 2] * a;
                sumA += a;
            }
        }

        // x=0 の出力
        if (sumA > 0) {
            tempRow[0] = static_cast<uint8_t>(sumR / sumA);
            tempRow[1] = static_cast<uint8_t>(sumG / sumA);
            tempRow[2] = static_cast<uint8_t>(sumB / sumA);
            tempRow[3] = static_cast<uint8_t>(sumA / count);
        } else {
            tempRow[0] = tempRow[1] = tempRow[2] = tempRow[3] = 0;
        }

        // スライディング: x = 1 to dstW-1
        for (int x = 1; x < dstW; x++) {
            int oldSrcX = (x - radius - 1) - srcOffsetX;  // 出ていく
            int newSrcX = (x + radius) - srcOffsetX;       // 入ってくる

            // 出ていくピクセルを減算
            if (oldSrcX >= 0 && oldSrcX < srcW) {
                int off = oldSrcX * 4;
                uint32_t a = srcRow[off + 3];
                sumR -= srcRow[off] * a;
                sumG -= srcRow[off + 1] * a;
                sumB -= srcRow[off + 2] * a;
                sumA -= a;
            }

            // 入ってくるピクセルを加算
            if (newSrcX >= 0 && newSrcX < srcW) {
                int off = newSrcX * 4;
                uint32_t a = srcRow[off + 3];
                sumR += srcRow[off] * a;
                sumG += srcRow[off + 1] * a;
                sumB += srcRow[off + 2] * a;
                sumA += a;
            }

            int outOff = x * 4;
            if (sumA > 0) {
                tempRow[outOff] = static_cast<uint8_t>(sumR / sumA);
                tempRow[outOff + 1] = static_cast<uint8_t>(sumG / sumA);
                tempRow[outOff + 2] = static_cast<uint8_t>(sumB / sumA);
                tempRow[outOff + 3] = static_cast<uint8_t>(sumA / count);
            } else {
                tempRow[outOff] = tempRow[outOff + 1] = tempRow[outOff + 2] = tempRow[outOff + 3] = 0;
            }
        }
    }

    // ========================================
    // パス2: 垂直ブラー（スライディングウィンドウ）
    // ========================================
    // 中間バッファはdstサイズで全域に値が入っているので範囲チェック不要
    for (int x = 0; x < dstW; x++) {
        int pixelOffset = x * 4;
        uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;

        // 初期化: y=0 のウィンドウ [-radius, radius]
        for (int ny = -radius; ny <= radius; ny++) {
            int tempY = ny;
            if (tempY >= 0 && tempY < dstH) {
                const uint8_t* tempRow = static_cast<const uint8_t*>(tempView.pixelAt(0, tempY));
                uint32_t a = tempRow[pixelOffset + 3];
                sumR += tempRow[pixelOffset] * a;
                sumG += tempRow[pixelOffset + 1] * a;
                sumB += tempRow[pixelOffset + 2] * a;
                sumA += a;
            }
        }

        // y=0 の出力
        {
            uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(0, 0));
            if (sumA > 0) {
                dstRow[pixelOffset] = static_cast<uint8_t>(sumR / sumA);
                dstRow[pixelOffset + 1] = static_cast<uint8_t>(sumG / sumA);
                dstRow[pixelOffset + 2] = static_cast<uint8_t>(sumB / sumA);
                dstRow[pixelOffset + 3] = static_cast<uint8_t>(sumA / count);
            } else {
                dstRow[pixelOffset] = dstRow[pixelOffset + 1] = dstRow[pixelOffset + 2] = dstRow[pixelOffset + 3] = 0;
            }
        }

        // スライディング: y = 1 to dstH-1
        for (int y = 1; y < dstH; y++) {
            int oldY = y - radius - 1;  // 出ていく
            int newY = y + radius;       // 入ってくる

            // 出ていくピクセルを減算
            if (oldY >= 0 && oldY < dstH) {
                const uint8_t* tempRow = static_cast<const uint8_t*>(tempView.pixelAt(0, oldY));
                uint32_t a = tempRow[pixelOffset + 3];
                sumR -= tempRow[pixelOffset] * a;
                sumG -= tempRow[pixelOffset + 1] * a;
                sumB -= tempRow[pixelOffset + 2] * a;
                sumA -= a;
            }

            // 入ってくるピクセルを加算
            if (newY >= 0 && newY < dstH) {
                const uint8_t* tempRow = static_cast<const uint8_t*>(tempView.pixelAt(0, newY));
                uint32_t a = tempRow[pixelOffset + 3];
                sumR += tempRow[pixelOffset] * a;
                sumG += tempRow[pixelOffset + 1] * a;
                sumB += tempRow[pixelOffset + 2] * a;
                sumA += a;
            }

            uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(0, y));
            if (sumA > 0) {
                dstRow[pixelOffset] = static_cast<uint8_t>(sumR / sumA);
                dstRow[pixelOffset + 1] = static_cast<uint8_t>(sumG / sumA);
                dstRow[pixelOffset + 2] = static_cast<uint8_t>(sumB / sumA);
                dstRow[pixelOffset + 3] = static_cast<uint8_t>(sumA / count);
            } else {
                dstRow[pixelOffset] = dstRow[pixelOffset + 1] = dstRow[pixelOffset + 2] = dstRow[pixelOffset + 3] = 0;
            }
        }
    }
}

// ========================================================================
// alpha - アルファ調整
// ========================================================================

void alpha(ViewPort& dst, const ViewPort& src, float scale) {
    if (!dst.isValid() || !src.isValid()) return;
    if (dst.width != src.width || dst.height != src.height) return;

    uint32_t alphaScale = static_cast<uint32_t>(scale * 256.0f);

    for (int y = 0; y < src.height; y++) {
        const uint8_t* srcRow = static_cast<const uint8_t*>(src.pixelAt(0, y));
        uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(0, y));

        for (int x = 0; x < src.width; x++) {
            int pixelOffset = x * 4;
            // RGBはそのまま、Alphaのみスケール
            dstRow[pixelOffset]     = srcRow[pixelOffset];      // R
            dstRow[pixelOffset + 1] = srcRow[pixelOffset + 1];  // G
            dstRow[pixelOffset + 2] = srcRow[pixelOffset + 2];  // B
            uint32_t a = srcRow[pixelOffset + 3];
            dstRow[pixelOffset + 3] = static_cast<uint8_t>((a * alphaScale) >> 8);  // A
        }
    }
}

} // namespace filters
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_OPERATIONS_FILTERS_H
