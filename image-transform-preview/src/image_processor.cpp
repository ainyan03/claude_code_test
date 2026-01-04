#include "image_processor.h"
#include "image_types.h"
#include <algorithm>
#include <cstring>
#include <memory>

namespace ImageTransform {

// ========================================================================
// AffineMatrix実装
// ========================================================================

// AffineParamsから2x3行列を生成
AffineMatrix AffineMatrix::fromParams(const AffineParams& params, double centerX, double centerY) {
    // 変換順序: 中心に移動 → スケール → 回転 → 平行移動 → 元の位置に戻す
    // 行列計算: T(tx,ty) * R(rot) * S(sx,sy) * T(-cx,-cy)

    double cosR = std::cos(params.rotation);
    double sinR = std::sin(params.rotation);
    double sx = params.scaleX;
    double sy = params.scaleY;

    AffineMatrix m;
    // 合成行列の要素を直接計算
    m.a = sx * cosR;
    m.b = -sy * sinR;
    m.c = sx * sinR;
    m.d = sy * cosR;

    // 平行移動成分（中心基準の回転・スケール + ユーザー指定の平行移動）
    m.tx = -centerX * m.a - centerY * m.b + centerX + params.translateX;
    m.ty = -centerX * m.c - centerY * m.d + centerY + params.translateY;

    return m;
}

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

// 8bit RGBA → ViewPort（16bit Premultiplied RGBA）変換
// 純粋な型変換のみ（アルファ調整はAlphaFilterを使用）
ViewPort ImageProcessor::fromImage(const Image& input) const {
    ViewPort output(input.width, input.height, PixelFormatIDs::RGBA16_Premultiplied);

    const uint8_t* srcData = input.data.data();

    // 行ごとにアクセス（Image:flat, ViewPort:stride付き）
    for (int y = 0; y < input.height; y++) {
        const uint8_t* srcRow = srcData + y * input.width * 4;
        uint16_t* dstRow = output.getPixelPtr<uint16_t>(0, y);

        for (int x = 0; x < input.width; x++) {
            int idx = x * 4;

            uint16_t r8 = srcRow[idx];
            uint16_t g8 = srcRow[idx + 1];
            uint16_t b8 = srcRow[idx + 2];
            uint16_t a8 = srcRow[idx + 3];

            // 8bit → 16bit変換（0-255 → 0-65535）
            uint16_t a16 = (a8 << 8) | a8;

            // プリマルチプライド: RGB * alpha
            // (r8 * a16) / 65535 を高速計算
            dstRow[idx]     = (r8 * a16) >> 8;  // (r8 * a16) / 256（近似）
            dstRow[idx + 1] = (g8 * a16) >> 8;
            dstRow[idx + 2] = (b8 * a16) >> 8;
            dstRow[idx + 3] = a16;
        }
    }

    return output;
}

// ViewPort（16bit Premultiplied RGBA）→ 8bit RGBA変換
Image ImageProcessor::toImage(const ViewPort& input) const {
    Image output(input.width, input.height);

    uint8_t* dstData = output.data.data();

    // 行ごとにアクセス（ViewPort:stride付き, Image:flat）
    for (int y = 0; y < input.height; y++) {
        const uint16_t* srcRow = input.getPixelPtr<uint16_t>(0, y);
        uint8_t* dstRow = dstData + y * input.width * 4;

        for (int x = 0; x < input.width; x++) {
            int idx = x * 4;

            uint16_t r16 = srcRow[idx];
            uint16_t g16 = srcRow[idx + 1];
            uint16_t b16 = srcRow[idx + 2];
            uint16_t a16 = srcRow[idx + 3];

            // アンプリマルチプライド: RGB / alpha
            if (a16 > 0) {
                // (r16 * 65535) / a16 を計算して8bitに変換
                uint32_t r_unpre = ((uint32_t)r16 * 65535) / a16;
                uint32_t g_unpre = ((uint32_t)g16 * 65535) / a16;
                uint32_t b_unpre = ((uint32_t)b16 * 65535) / a16;

                dstRow[idx]     = std::min(r_unpre >> 8, 255u);
                dstRow[idx + 1] = std::min(g_unpre >> 8, 255u);
                dstRow[idx + 2] = std::min(b_unpre >> 8, 255u);
            } else {
                dstRow[idx]     = 0;
                dstRow[idx + 1] = 0;
                dstRow[idx + 2] = 0;
            }

            dstRow[idx + 3] = a16 >> 8;  // 16bit → 8bit
        }
    }

    return output;
}

// ViewPort（Premultiplied）画像の合成（超高速版：除算なし）
ViewPort ImageProcessor::mergeImages(const std::vector<const ViewPort*>& images) const {
    ViewPort result(canvasWidth, canvasHeight, PixelFormatIDs::RGBA16_Premultiplied);

    // キャンバスを透明で初期化
    std::memset(result.data, 0, result.getTotalBytes());

    // 各画像を順番に合成（プリマルチプライド前提なので単純加算）
    for (size_t i = 0; i < images.size(); i++) {
        const ViewPort* img = images[i];
        if (!img || !img->isValid()) continue;

        // 画像サイズがキャンバスサイズと異なる場合は中央配置
        int offsetX = (canvasWidth - img->width) / 2;
        int offsetY = (canvasHeight - img->height) / 2;

        for (int y = 0; y < img->height && (y + offsetY) < canvasHeight; y++) {
            for (int x = 0; x < img->width && (x + offsetX) < canvasWidth; x++) {
                if (offsetX + x < 0 || offsetY + y < 0) continue;

                const uint16_t* srcPixel = img->getPixelPtr<uint16_t>(x, y);
                uint16_t* dstPixel = result.getPixelPtr<uint16_t>(x + offsetX, y + offsetY);

                uint16_t srcR = srcPixel[0];
                uint16_t srcG = srcPixel[1];
                uint16_t srcB = srcPixel[2];
                uint16_t srcA = srcPixel[3];

                uint16_t dstR = dstPixel[0];
                uint16_t dstG = dstPixel[1];
                uint16_t dstB = dstPixel[2];
                uint16_t dstA = dstPixel[3];

                // プリマルチプライド合成: src over dst
                // out_rgb = src_rgb + dst_rgb * (1 - src_a)
                // out_a = src_a + dst_a * (1 - src_a)

                uint16_t invSrcA = 65535 - srcA;

                // 乗算と16bitシフトで高速計算（除算なし）
                dstPixel[0] = srcR + ((dstR * invSrcA) >> 16);
                dstPixel[1] = srcG + ((dstG * invSrcA) >> 16);
                dstPixel[2] = srcB + ((dstB * invSrcA) >> 16);
                dstPixel[3] = srcA + ((dstA * invSrcA) >> 16);
            }
        }
    }

    return result;
}

// 行列ベース + 固定小数点アフィン変換（ViewPortベース）
// 純粋な幾何変換のみ（アルファ調整はAlphaFilterを使用）
ViewPort ImageProcessor::applyTransform(const ViewPort& input, const AffineMatrix& matrix) const {
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

    // 固定小数点16.16形式に変換（上位16bit: 整数、下位16bit: 小数）
    int32_t fixedInvA  = static_cast<int32_t>(invA * 65536);
    int32_t fixedInvB  = static_cast<int32_t>(invB * 65536);
    int32_t fixedInvC  = static_cast<int32_t>(invC * 65536);
    int32_t fixedInvD  = static_cast<int32_t>(invD * 65536);

    // 入力データへのポインタ取得
    const uint16_t* inputData = static_cast<const uint16_t*>(input.data);

    // 出力画像の各ピクセルをスキャン
    for (int dy = 0; dy < canvasHeight; dy++) {
        // 行の開始座標を固定小数点で計算
        int32_t srcX_fixed = static_cast<int32_t>((invA * 0 + invB * dy + invTx) * 65536);
        int32_t srcY_fixed = static_cast<int32_t>((invC * 0 + invD * dy + invTy) * 65536);

        uint16_t* dstRow = output.getPixelPtr<uint16_t>(0, dy);

        for (int dx = 0; dx < canvasWidth; dx++) {
            // 固定小数点から整数座標を抽出（上位16bit）
            int sx = srcX_fixed >> 16;
            int sy = srcY_fixed >> 16;

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

// ViewPortベースフィルタ処理（ファクトリパターン）
ViewPort ImageProcessor::applyFilter(const ViewPort& input, const std::string& filterType, float param) const {
    std::unique_ptr<ImageFilter> filter;

    // フィルタクラスの生成（文字列→クラスのマッピング）
    if (filterType == "brightness") {
        BrightnessFilterParams params(param);
        filter = std::make_unique<BrightnessFilter>(params);
    }
    else if (filterType == "grayscale") {
        GrayscaleFilterParams params;
        filter = std::make_unique<GrayscaleFilter>(params);
    }
    else if (filterType == "blur") {
        BoxBlurFilterParams params(static_cast<int>(param));
        filter = std::make_unique<BoxBlurFilter>(params);
    }
    else if (filterType == "alpha") {
        AlphaFilterParams params(param);
        filter = std::make_unique<AlphaFilter>(params);
    }

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
