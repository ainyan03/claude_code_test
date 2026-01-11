#include "filters.h"
#include "../image_buffer.h"
#include <algorithm>
#include <cstdint>

namespace FLEXIMG_NAMESPACE {
namespace filters {

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
// boxBlur - ボックスブラー（2パス）
// ========================================================================

void boxBlur(ViewPort& dst, const ViewPort& src, int radius) {
    if (!dst.isValid() || !src.isValid()) return;
    if (dst.width != src.width || dst.height != src.height) return;
    if (radius <= 0) {
        // 半径0の場合は単純コピー
        view_ops::copy(dst, 0, 0, src, 0, 0, src.width, src.height);
        return;
    }

    int width = src.width;
    int height = src.height;

    // 中間バッファ（水平ブラー結果）
    ImageBuffer temp(width, height, src.formatID);
    ViewPort tempView = temp.view();

    // パス1: 水平方向のブラー
    for (int y = 0; y < height; y++) {
        const uint8_t* srcRow = static_cast<const uint8_t*>(src.pixelAt(0, y));
        uint8_t* dstRow = static_cast<uint8_t*>(tempView.pixelAt(0, y));

        for (int x = 0; x < width; x++) {
            uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;

            int xStart = std::max(0, x - radius);
            int xEnd = std::min(width - 1, x + radius);
            int count = xEnd - xStart + 1;

            for (int nx = xStart; nx <= xEnd; nx++) {
                int pixelOffset = nx * 4;
                sumR += srcRow[pixelOffset];
                sumG += srcRow[pixelOffset + 1];
                sumB += srcRow[pixelOffset + 2];
                sumA += srcRow[pixelOffset + 3];
            }

            int outOffset = x * 4;
            dstRow[outOffset]     = sumR / count;
            dstRow[outOffset + 1] = sumG / count;
            dstRow[outOffset + 2] = sumB / count;
            dstRow[outOffset + 3] = sumA / count;
        }
    }

    // パス2: 垂直方向のブラー
    for (int y = 0; y < height; y++) {
        uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(0, y));

        int yStart = std::max(0, y - radius);
        int yEnd = std::min(height - 1, y + radius);
        int count = yEnd - yStart + 1;

        for (int x = 0; x < width; x++) {
            uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            int pixelOffset = x * 4;

            for (int ny = yStart; ny <= yEnd; ny++) {
                const uint8_t* tmpRow = static_cast<const uint8_t*>(tempView.pixelAt(0, ny));
                sumR += tmpRow[pixelOffset];
                sumG += tmpRow[pixelOffset + 1];
                sumB += tmpRow[pixelOffset + 2];
                sumA += tmpRow[pixelOffset + 3];
            }

            dstRow[pixelOffset]     = sumR / count;
            dstRow[pixelOffset + 1] = sumG / count;
            dstRow[pixelOffset + 2] = sumB / count;
            dstRow[pixelOffset + 3] = sumA / count;
        }
    }
}

// ========================================================================
// boxBlurWithPadding - 透明拡張ボックスブラー
// ========================================================================
//
// スライディングウィンドウ方式 + α加重平均
// 計算量: O(width × height) - radius に依存しない
//

void boxBlurWithPadding(ViewPort& dst, const ViewPort& src,
                        int srcOffsetX, int srcOffsetY, int radius) {
    if (!dst.isValid()) return;
    if (radius <= 0) {
        // 半径0の場合: srcをdstにコピー（オフセット考慮）
        // まず全体を透明に
        for (int y = 0; y < dst.height; y++) {
            uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(0, y));
            std::memset(dstRow, 0, dst.width * 4);
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
    int count = 2 * radius + 1;

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
            std::memset(tempRow, 0, dstW * 4);
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
