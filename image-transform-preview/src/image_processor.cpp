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
ViewPort ImageProcessor::mergeImages(const std::vector<const ViewPort*>& images, double dstOriginX, double dstOriginY) const {
    ViewPort result(canvasWidth, canvasHeight, PixelFormatIDs::RGBA16_Premultiplied);

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
        int yEnd = std::min(img->height, canvasHeight - offsetY);
        int xStart = std::max(0, -offsetX);
        int xEnd = std::min(img->width, canvasWidth - offsetX);

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

// 行列ベース + 固定小数点アフィン変換（ViewPortベース）
// 純粋な幾何変換のみ（アルファ調整はAlphaFilterを使用）
// originX, originY: 変換の中心点（この点を中心に回転・拡縮が適用される）
ViewPort ImageProcessor::applyTransform(const ViewPort& input, const AffineMatrix& matrix, double originX, double originY) const {
    ViewPort output(canvasWidth, canvasHeight, PixelFormatIDs::RGBA16_Premultiplied);
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

    // 固定小数点の小数部ビット数（デバッグ用に変更可能）
    constexpr int FIXED_POINT_BITS = 16;  // 通常は16、デバッグ時は4など
    constexpr int32_t FIXED_POINT_SCALE = 1 << FIXED_POINT_BITS;

    // 固定小数点形式に変換（roundで四捨五入して精度向上）
    int32_t fixedInvA  = static_cast<int32_t>(std::round(invA * FIXED_POINT_SCALE));
    int32_t fixedInvB  = static_cast<int32_t>(std::round(invB * FIXED_POINT_SCALE));
    int32_t fixedInvC  = static_cast<int32_t>(std::round(invC * FIXED_POINT_SCALE));
    int32_t fixedInvD  = static_cast<int32_t>(std::round(invD * FIXED_POINT_SCALE));
    int32_t fixedInvTx = static_cast<int32_t>(std::round(invTx * FIXED_POINT_SCALE));
    int32_t fixedInvTy = static_cast<int32_t>(std::round(invTy * FIXED_POINT_SCALE));

    // 原点中心の変換を固定小数点で適用: T(origin) × M × T(-origin) の逆変換
    // 入力座標系の原点を使用（整数のまま、シフト演算を最小限に）
    int32_t originXInt = static_cast<int32_t>(std::round(originX));
    int32_t originYInt = static_cast<int32_t>(std::round(originY));
    // P_in = M^-1 × P_out + (origin - M^-1 × origin) - M^-1 × t
    // 原点オフセット: origin - M^-1 × origin = origin - (invA × originX + invB × originY, ...)
    fixedInvTx += (originXInt << FIXED_POINT_BITS) - originXInt * fixedInvA - fixedInvB * originYInt;
    fixedInvTy += (originYInt << FIXED_POINT_BITS) - originYInt * fixedInvD - fixedInvC * originXInt;

    // DDA増分の半分を加算: 切り捨て時の誤差を平均化
    // X方向ループの増分 fixedInvA/fixedInvC の半分を初期値に加える
    fixedInvTx += fixedInvA >> 1;
    fixedInvTy += fixedInvC >> 1;

    // 入力データへのポインタ取得
    const uint16_t* inputData = static_cast<const uint16_t*>(input.data);

    // 出力画像の各ピクセルをスキャン
    for (int dy = 0; dy < canvasHeight; dy++) {
        // 行の開始座標を固定小数点で計算（ループ内でdoubleを使わない）
        int32_t srcX_fixed = fixedInvB * dy + fixedInvTx;
        int32_t srcY_fixed = fixedInvD * dy + fixedInvTy;

        uint16_t* dstRow = output.getPixelPtr<uint16_t>(0, dy);

        for (int dx = 0; dx < canvasWidth; dx++) {
            // 固定小数点から整数座標を抽出
            int sx = srcX_fixed >> FIXED_POINT_BITS;
            int sy = srcY_fixed >> FIXED_POINT_BITS;

            // 範囲チェック
            if (sx >= 0 && sx < input.width && sy >= 0 && sy < input.height) {
                const uint16_t* srcPixel = input.getPixelPtr<uint16_t>(sx, sy);
                uint16_t* dstPixel = &dstRow[dx * 4];

                // ピクセルをそのままコピー（アルファ調整なし）
                dstPixel[0] = srcPixel[0];
                dstPixel[1] = srcPixel[1];
                dstPixel[2] = srcPixel[2];
                dstPixel[3] = srcPixel[3];
            }

            // DDAアルゴリズム: 座標増分を加算（除算なし）
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
