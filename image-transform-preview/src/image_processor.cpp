#include "image_processor.h"
#include "image_types.h"
#include "filter_registry.h"
#include <algorithm>
#include <cstring>
#include <memory>

namespace ImageTransform {

// ========================================================================
// ImageProcessor実装
// ========================================================================

ImageProcessor::ImageProcessor(int canvasWidth, int canvasHeight)
    : canvasWidth(canvasWidth), canvasHeight(canvasHeight) {
}

void ImageProcessor::setCanvasSize(int width, int height) {
    canvasWidth = width;
    canvasHeight = height;
}

// ViewPort（Premultiplied）画像の合成（超高速版：除算なし）
ViewPort ImageProcessor::mergeImages(const std::vector<const ViewPort*>& images, double dstOriginX, double dstOriginY,
                                     int outputWidth, int outputHeight) const {
    // 出力サイズの決定（0以下の場合はcanvasサイズを使用）
    int outW = (outputWidth > 0) ? outputWidth : canvasWidth;
    int outH = (outputHeight > 0) ? outputHeight : canvasHeight;

    ViewPort result(outW, outH, PixelFormatIDs::RGBA16_Premultiplied);

    // キャンバスを透明で初期化
    std::memset(result.data, 0, result.getTotalBytes());

    // 合成の基準点（dstOrigin）
    // 各画像の srcOrigin がこの点に揃うように配置
    double refX = dstOriginX;
    double refY = dstOriginY;

    // 各画像を順番に合成（プリマルチプライド前提なので単純加算）
    for (size_t i = 0; i < images.size(); i++) {
        const ViewPort* img = images[i];
        if (!img || !img->isValid()) continue;

        // srcOrigin ベースの配置: 画像の srcOrigin が基準点に来るようにオフセット
        // オフセット = 基準点 - srcOrigin
        int offsetX = static_cast<int>(refX - img->srcOriginX);
        int offsetY = static_cast<int>(refY - img->srcOriginY);

        // ループ範囲を事前計算（内側ループの条件分岐を削減）
        int yStart = std::max(0, -offsetY);
        int yEnd = std::min(img->height, outH - offsetY);
        int xStart = std::max(0, -offsetX);
        int xEnd = std::min(img->width, outW - offsetX);

        // 範囲が無効な場合はスキップ
        if (yStart >= yEnd || xStart >= xEnd) continue;

        for (int y = yStart; y < yEnd; y++) {
            // 行の先頭ポインタを取得（ループ外で1回だけ）
            const uint16_t* srcRow = img->getPixelPtr<uint16_t>(0, y);
            uint16_t* dstRow = result.getPixelPtr<uint16_t>(0, y + offsetY);

            for (int x = xStart; x < xEnd; x++) {
                // 単純なオフセット計算（getPixelPtrを呼ばない）
                const uint16_t* srcPixel = srcRow + x * 4;  // RGBA16 = 4 channels
                uint16_t* dstPixel = dstRow + (x + offsetX) * 4;

                uint16_t srcA = srcPixel[3];

                // ソースが完全透明 → 何もしない
                if (srcA == 0) {
                    continue;
                }

                uint16_t srcR = srcPixel[0];
                uint16_t srcG = srcPixel[1];
                uint16_t srcB = srcPixel[2];
                uint16_t dstA = dstPixel[3];

                // 合成が必要な場合のみ加算（srcが半透明 かつ dstが不透明）
                if (srcA != 65535 && dstA != 0) {
                    // プリマルチプライド合成: src over dst
                    // out = src + dst * (1 - src_a)
                    uint16_t invSrcA = 65535 - srcA;
                    srcR += (dstPixel[0] * invSrcA) >> 16;
                    srcG += (dstPixel[1] * invSrcA) >> 16;
                    srcB += (dstPixel[2] * invSrcA) >> 16;
                    srcA += (dstA * invSrcA) >> 16;
                }

                // 統合された代入
                dstPixel[0] = srcR;
                dstPixel[1] = srcG;
                dstPixel[2] = srcB;
                dstPixel[3] = srcA;
            }
        }
    }

    // 合成結果の srcOrigin は基準点に設定
    result.srcOriginX = refX;
    result.srcOriginY = refY;

    return result;
}

// ========================================
// アフィン変換（固定小数点演算による高速実装）
// ========================================
//
// 入力画像に対してアフィン変換を適用し、出力バッファに描画する。
// 逆変換方式: 出力の各ピクセル位置から入力のサンプル位置を逆算する。
//
// パラメータ:
//   input         : 入力画像（ViewPort）
//   matrix        : 変換行列 [a b tx; c d ty; 0 0 1]
//   originX/Y     : 変換の中心点（この点を中心に回転・拡縮が適用される）
//   outputOffsetX/Y: 出力バッファ内でのレンダリング位置オフセット
//
// 座標変換の流れ:
//   1. 出力座標(dx, dy)からオフセットを減算
//   2. 原点を基準とした相対座標に変換
//   3. 逆行列を適用して入力座標を算出
//   4. 入力画像の範囲内ならピクセルをコピー
//
ViewPort ImageProcessor::applyTransform(const ViewPort& input, const AffineMatrix& matrix, double originX, double originY,
                                        double outputOffsetX, double outputOffsetY,
                                        int outputWidth, int outputHeight) const {
    // 出力サイズの決定（0以下の場合はcanvasサイズを使用）
    int outW = (outputWidth > 0) ? outputWidth : canvasWidth;
    int outH = (outputHeight > 0) ? outputHeight : canvasHeight;

    ViewPort output(outW, outH, PixelFormatIDs::RGBA16_Premultiplied);
    std::memset(output.data, 0, output.getTotalBytes());

    // 逆行列を計算（出力→入力の座標変換）
    double det = matrix.a * matrix.d - matrix.b * matrix.c;
    if (std::abs(det) < 1e-10) {
        return output;  // 特異行列の場合は空画像を返す
    }

    double invDet = 1.0 / det;
    double invA = matrix.d * invDet;
    double invB = -matrix.b * invDet;
    double invC = -matrix.c * invDet;
    double invD = matrix.a * invDet;
    double invTx = (-matrix.d * matrix.tx + matrix.b * matrix.ty) * invDet;
    double invTy = (matrix.c * matrix.tx - matrix.a * matrix.ty) * invDet;

    // 固定小数点の小数部ビット数
    constexpr int FIXED_POINT_BITS = 16;
    constexpr int32_t FIXED_POINT_SCALE = 1 << FIXED_POINT_BITS;

    // 固定小数点形式に変換（lroundで四捨五入して精度向上）
    int32_t fixedInvA  = std::lround(invA * FIXED_POINT_SCALE);
    int32_t fixedInvB  = std::lround(invB * FIXED_POINT_SCALE);
    int32_t fixedInvC  = std::lround(invC * FIXED_POINT_SCALE);
    int32_t fixedInvD  = std::lround(invD * FIXED_POINT_SCALE);
    int32_t fixedInvTx = std::lround(invTx * FIXED_POINT_SCALE);
    int32_t fixedInvTy = std::lround(invTy * FIXED_POINT_SCALE);

    // ----------------------------------------
    // 原点中心の変換を固定小数点で適用
    // ----------------------------------------
    // 数学的には: T(origin) × M × T(-origin) の逆変換
    // = T(origin) × M^-1 × T(-origin)
    //
    // 展開すると:
    //   sx = invA*(dx-origin) + invB*(dy-origin) + origin
    //      = invA*dx + invB*dy + (origin - invA*origin - invB*origin)
    //
    // 最後の項が原点オフセットとなる
    int32_t originXInt = std::lround(originX);
    int32_t originYInt = std::lround(originY);
    fixedInvTx += (originXInt << FIXED_POINT_BITS) - originXInt * fixedInvA - fixedInvB * originYInt;
    fixedInvTy += (originYInt << FIXED_POINT_BITS) - originYInt * fixedInvD - fixedInvC * originXInt;

    // ----------------------------------------
    // 出力座標系オフセットの適用
    // ----------------------------------------
    // 出力バッファの(offset, offset)から描画を開始するために、
    // 逆変換の平行移動成分を調整する
    // output(dx, dy) → input(M^-1 × (dx - offset, dy - offset))
    if (outputOffsetX != 0 || outputOffsetY != 0) {
        int32_t offsetXInt = std::lround(outputOffsetX);
        int32_t offsetYInt = std::lround(outputOffsetY);
        fixedInvTx -= offsetXInt * fixedInvA + offsetYInt * fixedInvB;
        fixedInvTy -= offsetXInt * fixedInvC + offsetYInt * fixedInvD;
    }

    // ----------------------------------------
    // 有効描画範囲の事前計算（calcValidRange）
    // ----------------------------------------
    //
    // 各行で入力画像の範囲内になる dx の開始・終了位置を事前計算し、
    // 内側ループの範囲チェック条件分岐を削減する。
    //
    // 座標計算: 出力ピクセル中心 (dx+0.5, dy+0.5) から逆変換 → floor で整数化
    //   sx = floor((fixedInvA * (dx+0.5) + fixedInvB * (dy+0.5) + fixedInvTx) / SCALE)
    //
    // 引数:
    //   coeff    : dx の係数（fixedInvA または fixedInvC）
    //   base     : 行ごとの基準値（rowBaseX/Y、dy+0.5成分を含む）
    //   minVal   : 許容最小値（0）
    //   maxVal   : 許容最大値（width-1 または height-1）
    // 戻り値: {dxStart, dxEnd}（有効範囲がない場合は dxStart > dxEnd）
    //
    auto calcValidRange = [FIXED_POINT_BITS, FIXED_POINT_SCALE](
        int32_t coeff, int32_t base, int minVal, int maxVal, int canvasSize
    ) -> std::pair<int, int> {
        // dx+0.5 成分のオフセット
        int32_t coeffHalf = coeff >> 1;

        if (coeff == 0) {
            // dx に依存しない → 全列で同じ入力座標
            int val = base >> FIXED_POINT_BITS;
            if (base < 0 && (base & (FIXED_POINT_SCALE - 1)) != 0) val--;
            return (val >= minVal && val <= maxVal)
                ? std::make_pair(0, canvasSize - 1)
                : std::make_pair(1, 0);  // 無効
        }

        // floor(val) >= minVal ⇔ val >= minVal
        // floor(val) <= maxVal ⇔ val < maxVal + 1
        double baseWithHalf = base + coeffHalf;
        double minThreshold = (double)minVal * FIXED_POINT_SCALE;
        double maxThreshold = (double)(maxVal + 1) * FIXED_POINT_SCALE;
        double dxForMin = (minThreshold - baseWithHalf) / coeff;
        double dxForMax = (maxThreshold - baseWithHalf) / coeff;

        int dxStart, dxEnd;
        if (coeff > 0) {
            dxStart = (int)std::ceil(dxForMin);
            dxEnd = (int)std::ceil(dxForMax) - 1;
        } else {
            dxStart = (int)std::ceil(dxForMax);
            dxEnd = (int)std::ceil(dxForMin) - 1;
        }
        return {dxStart, dxEnd};
    };

    // ----------------------------------------
    // ピクセルスキャン（DDAアルゴリズム）
    // ----------------------------------------
    const int inputStride = input.stride / sizeof(uint16_t);

    // 0.5成分の逆変換オフセット（出力ピクセル中心からサンプリング）
    const int32_t rowOffsetX = fixedInvB >> 1;  // (dy+0.5)のX成分
    const int32_t rowOffsetY = fixedInvD >> 1;  // (dy+0.5)のY成分
    const int32_t dxOffsetX = fixedInvA >> 1;   // (dx+0.5)のX成分
    const int32_t dxOffsetY = fixedInvC >> 1;   // (dx+0.5)のY成分

    for (int dy = 0; dy < outH; dy++) {
        // 行基準座標（dy+0.5の逆変換を含む）
        int32_t rowBaseX = fixedInvB * dy + fixedInvTx + rowOffsetX;
        int32_t rowBaseY = fixedInvD * dy + fixedInvTy + rowOffsetY;

        // 入力画像範囲内となる dx の有効範囲を事前計算
        auto [xStart, xEnd] = calcValidRange(fixedInvA, rowBaseX, 0, input.width - 1, outW);
        auto [yStart, yEnd] = calcValidRange(fixedInvC, rowBaseY, 0, input.height - 1, outW);
        int dxStart = std::max({0, xStart, yStart});
        int dxEnd = std::min({outW - 1, xEnd, yEnd});

        if (dxStart > dxEnd) continue;

        // 開始位置での入力座標（dx+0.5の逆変換を含む）
        int32_t srcX_fixed = fixedInvA * dxStart + rowBaseX + dxOffsetX;
        int32_t srcY_fixed = fixedInvC * dxStart + rowBaseY + dxOffsetY;

        uint16_t* dstRow = output.getPixelPtr<uint16_t>(dxStart, dy);
        const uint16_t* inputData = static_cast<const uint16_t*>(input.data);

        for (int dx = dxStart; dx <= dxEnd; dx++) {
            // floor で整数座標に変換（unsignedキャストで負の値を範囲外に）
            uint32_t sx = (uint32_t)srcX_fixed >> FIXED_POINT_BITS;
            uint32_t sy = (uint32_t)srcY_fixed >> FIXED_POINT_BITS;

            if (sx < (uint32_t)input.width && sy < (uint32_t)input.height) {
                const uint16_t* srcPixel = inputData + sy * inputStride + sx * 4;
                dstRow[0] = srcPixel[0];
                dstRow[1] = srcPixel[1];
                dstRow[2] = srcPixel[2];
                dstRow[3] = srcPixel[3];
            }

            dstRow += 4;
            srcX_fixed += fixedInvA;
            srcY_fixed += fixedInvC;
        }
    }

    return output;
}

// ViewPortベースフィルタ処理（FilterRegistryを使用）
// params: フィルタパラメータ配列（各フィルタが必要なパラメータをインデックスで取得）
ViewPort ImageProcessor::applyFilter(const ViewPort& input, const std::string& filterType, const std::vector<float>& params) const {
    // FilterRegistryからフィルタを生成
    auto filter = FilterRegistry::getInstance().createFilter(filterType, params);

    // フィルタを適用
    if (filter) {
        return filter->apply(input);
    }

    // 未知のフィルタタイプの場合は入力をそのまま返す
    return ViewPort(input);
}

// ピクセルフォーマット変換
ViewPort ImageProcessor::convertPixelFormat(const ViewPort& input, PixelFormatID targetFormat) const {
    // 同じフォーマットの場合は変換不要
    if (input.formatID == targetFormat) {
        return ViewPort(input);
    }

    ViewPort output(input.width, input.height, targetFormat);

    // srcOrigin を継承
    output.srcOriginX = input.srcOriginX;
    output.srcOriginY = input.srcOriginY;

    // よく使う変換パスの直接最適化
    // RGBA8_Straight → RGBA16_Premultiplied
    if (input.formatID == PixelFormatIDs::RGBA8_Straight &&
        targetFormat == PixelFormatIDs::RGBA16_Premultiplied) {
        for (int y = 0; y < input.height; y++) {
            const uint8_t* srcRow = input.getPixelPtr<uint8_t>(0, y);
            uint16_t* dstRow = output.getPixelPtr<uint16_t>(0, y);
            for (int x = 0; x < input.width; x++) {
                int idx = x * 4;
                uint16_t r8 = srcRow[idx];
                uint16_t g8 = srcRow[idx + 1];
                uint16_t b8 = srcRow[idx + 2];
                uint16_t a8 = srcRow[idx + 3];
                // 8bit → 16bit + premultiply
                uint16_t a16 = (a8 << 8) | a8;
                dstRow[idx]     = (r8 * a16) >> 8;
                dstRow[idx + 1] = (g8 * a16) >> 8;
                dstRow[idx + 2] = (b8 * a16) >> 8;
                dstRow[idx + 3] = a16;
            }
        }
        return output;
    }

    // RGBA16_Premultiplied → RGBA8_Straight
    if (input.formatID == PixelFormatIDs::RGBA16_Premultiplied &&
        targetFormat == PixelFormatIDs::RGBA8_Straight) {
        for (int y = 0; y < input.height; y++) {
            const uint16_t* srcRow = input.getPixelPtr<uint16_t>(0, y);
            uint8_t* dstRow = output.getPixelPtr<uint8_t>(0, y);
            for (int x = 0; x < input.width; x++) {
                int idx = x * 4;
                uint16_t r16 = srcRow[idx];
                uint16_t g16 = srcRow[idx + 1];
                uint16_t b16 = srcRow[idx + 2];
                uint16_t a16 = srcRow[idx + 3];
                // unpremultiply + 16bit → 8bit
                if (a16 > 0) {
                    uint32_t r_unpre = ((uint32_t)r16 * 65535) / a16;
                    uint32_t g_unpre = ((uint32_t)g16 * 65535) / a16;
                    uint32_t b_unpre = ((uint32_t)b16 * 65535) / a16;
                    dstRow[idx]     = std::min(r_unpre >> 8, 255u);
                    dstRow[idx + 1] = std::min(g_unpre >> 8, 255u);
                    dstRow[idx + 2] = std::min(b_unpre >> 8, 255u);
                } else {
                    dstRow[idx] = dstRow[idx + 1] = dstRow[idx + 2] = 0;
                }
                dstRow[idx + 3] = a16 >> 8;
            }
        }
        return output;
    }

    // その他の変換: レジストリ経由（標準形式を経由）
    PixelFormatRegistry& registry = PixelFormatRegistry::getInstance();

    // 行ごとに変換（ストライドの違いを吸収）
    for (int y = 0; y < input.height; y++) {
        const void* srcRow = input.getPixelPtr<uint8_t>(0, y);
        void* dstRow = output.getPixelPtr<uint8_t>(0, y);

        registry.convert(
            srcRow, input.formatID,
            dstRow, targetFormat,
            input.width  // 1行分のピクセル数
        );
    }

    return output;
}

} // namespace ImageTransform
