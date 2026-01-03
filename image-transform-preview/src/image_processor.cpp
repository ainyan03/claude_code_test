#include "image_processor.h"
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

// 8bit RGBA → 16bit Premultiplied RGBA変換
Image16 ImageProcessor::toPremultiplied(const Image& input, double alpha) const {
    Image16 output(input.width, input.height);
    int pixelCount = input.width * input.height;

    for (int i = 0; i < pixelCount; i++) {
        int idx8 = i * 4;
        int idx16 = i * 4;

        uint16_t r8 = input.data[idx8];
        uint16_t g8 = input.data[idx8 + 1];
        uint16_t b8 = input.data[idx8 + 2];
        uint16_t a8 = input.data[idx8 + 3];

        // 8bit → 16bit変換（0-255 → 0-65535）
        // ノードのアルファ値も適用
        uint16_t a16_raw = (a8 << 8) | a8;
        uint16_t a16 = (a16_raw * static_cast<uint16_t>(alpha * 65535)) >> 16;

        // プリマルチプライド: RGB * alpha
        // (r8 * a16) / 65535 を高速計算
        output.data[idx16]     = (r8 * a16) >> 8;  // (r8 * a16) / 256（近似）
        output.data[idx16 + 1] = (g8 * a16) >> 8;
        output.data[idx16 + 2] = (b8 * a16) >> 8;
        output.data[idx16 + 3] = a16;
    }

    return output;
}

// 16bit Premultiplied RGBA → 8bit RGBA変換
Image ImageProcessor::fromPremultiplied(const Image16& input) const {
    Image output(input.width, input.height);
    int pixelCount = input.width * input.height;

    for (int i = 0; i < pixelCount; i++) {
        int idx = i * 4;

        uint16_t r16 = input.data[idx];
        uint16_t g16 = input.data[idx + 1];
        uint16_t b16 = input.data[idx + 2];
        uint16_t a16 = input.data[idx + 3];

        // アンプリマルチプライド: RGB / alpha
        if (a16 > 0) {
            // (r16 * 65535) / a16 を計算して8bitに変換
            uint32_t r_unpre = ((uint32_t)r16 * 65535) / a16;
            uint32_t g_unpre = ((uint32_t)g16 * 65535) / a16;
            uint32_t b_unpre = ((uint32_t)b16 * 65535) / a16;

            output.data[idx]     = std::min(r_unpre >> 8, 255u);
            output.data[idx + 1] = std::min(g_unpre >> 8, 255u);
            output.data[idx + 2] = std::min(b_unpre >> 8, 255u);
        } else {
            output.data[idx]     = 0;
            output.data[idx + 1] = 0;
            output.data[idx + 2] = 0;
        }

        output.data[idx + 3] = a16 >> 8;  // 16bit → 8bit
    }

    return output;
}

// 16bit Premultiplied画像の合成（超高速版：除算なし）
Image16 ImageProcessor::mergeImages16(const std::vector<const Image16*>& images) const {
    Image16 result(canvasWidth, canvasHeight);

    // キャンバスを透明で初期化
    std::fill(result.data.begin(), result.data.end(), 0);

    // 各画像を順番に合成（プリマルチプライド前提なので単純加算）
    for (size_t i = 0; i < images.size(); i++) {
        const Image16* img = images[i];
        if (!img) continue;

        // 画像サイズがキャンバスサイズと異なる場合は中央配置
        int offsetX = (canvasWidth - img->width) / 2;
        int offsetY = (canvasHeight - img->height) / 2;

        for (int y = 0; y < img->height && (y + offsetY) < canvasHeight; y++) {
            for (int x = 0; x < img->width && (x + offsetX) < canvasWidth; x++) {
                if (offsetX + x < 0 || offsetY + y < 0) continue;

                int srcIdx = (y * img->width + x) * 4;
                int dstIdx = ((y + offsetY) * canvasWidth + (x + offsetX)) * 4;

                uint16_t srcR = img->data[srcIdx];
                uint16_t srcG = img->data[srcIdx + 1];
                uint16_t srcB = img->data[srcIdx + 2];
                uint16_t srcA = img->data[srcIdx + 3];

                uint16_t dstR = result.data[dstIdx];
                uint16_t dstG = result.data[dstIdx + 1];
                uint16_t dstB = result.data[dstIdx + 2];
                uint16_t dstA = result.data[dstIdx + 3];

                // プリマルチプライド合成: src over dst
                // out_rgb = src_rgb + dst_rgb * (1 - src_a)
                // out_a = src_a + dst_a * (1 - src_a)

                uint16_t invSrcA = 65535 - srcA;

                // 乗算と16bitシフトで高速計算（除算なし）
                result.data[dstIdx]     = srcR + ((dstR * invSrcA) >> 16);
                result.data[dstIdx + 1] = srcG + ((dstG * invSrcA) >> 16);
                result.data[dstIdx + 2] = srcB + ((dstB * invSrcA) >> 16);
                result.data[dstIdx + 3] = srcA + ((dstA * invSrcA) >> 16);
            }
        }
    }

    return result;
}

// 行列ベース + 固定小数点アフィン変換（16bit版）
Image16 ImageProcessor::applyTransformToImage16(const Image16& input, const AffineMatrix& matrix, double alpha) const {
    Image16 output(canvasWidth, canvasHeight);
    std::fill(output.data.begin(), output.data.end(), 0);

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

    uint16_t alphaU16 = static_cast<uint16_t>(alpha * 65535);

    // 出力画像の各ピクセルをスキャン
    for (int dy = 0; dy < canvasHeight; dy++) {
        // 行の開始座標を固定小数点で計算
        int32_t srcX_fixed = static_cast<int32_t>((invA * 0 + invB * dy + invTx) * 65536);
        int32_t srcY_fixed = static_cast<int32_t>((invC * 0 + invD * dy + invTy) * 65536);

        for (int dx = 0; dx < canvasWidth; dx++) {
            // 固定小数点から整数座標を抽出（上位16bit）
            int sx = srcX_fixed >> 16;
            int sy = srcY_fixed >> 16;

            // 範囲チェック
            if (sx >= 0 && sx < input.width && sy >= 0 && sy < input.height) {
                int srcIdx = (sy * input.width + sx) * 4;
                int dstIdx = (dy * canvasWidth + dx) * 4;

                // プリマルチプライド済みなので、alphaを乗算するだけ
                output.data[dstIdx]     = (input.data[srcIdx]     * alphaU16) >> 16;
                output.data[dstIdx + 1] = (input.data[srcIdx + 1] * alphaU16) >> 16;
                output.data[dstIdx + 2] = (input.data[srcIdx + 2] * alphaU16) >> 16;
                output.data[dstIdx + 3] = (input.data[srcIdx + 3] * alphaU16) >> 16;
            }

            // DDAアルゴリズム: 座標増分を加算（除算なし）
            srcX_fixed += fixedInvA;
            srcY_fixed += fixedInvC;
        }
    }

    return output;
}

// 16bit版フィルタ処理（ファクトリパターン）
Image16 ImageProcessor::applyFilterToImage16(const Image16& input, const std::string& filterType, float param) const {
    std::unique_ptr<ImageFilter16> filter;

    // フィルタクラスの生成（文字列→クラスのマッピング）
    if (filterType == "brightness") {
        BrightnessFilterParams params(param);
        filter = std::make_unique<BrightnessFilter16>(params);
    }
    else if (filterType == "grayscale") {
        GrayscaleFilterParams params;
        filter = std::make_unique<GrayscaleFilter16>(params);
    }
    else if (filterType == "blur") {
        BoxBlurFilterParams params(static_cast<int>(param));
        filter = std::make_unique<BoxBlurFilter16>(params);
    }

    // フィルタを適用
    if (filter) {
        return filter->apply(input);
    }

    // 未知のフィルタタイプの場合は入力をそのまま返す
    return input;
}

// ピクセルフォーマット変換（Phase 3）
Image16 ImageProcessor::convertPixelFormat(const Image16& input, PixelFormatID targetFormat) const {
    // 同じフォーマットの場合は変換不要
    if (input.formatID == targetFormat) {
        return input;
    }

    // レジストリから変換を実行
    Image16 output(input.width, input.height, targetFormat);

    PixelFormatRegistry::getInstance().convert(
        input.data.data(), input.formatID,
        output.data.data(), targetFormat,
        input.width * input.height
    );

    return output;
}

} // namespace ImageTransform
