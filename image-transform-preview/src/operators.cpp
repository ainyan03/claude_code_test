#include "operators.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace ImageTransform {

// ========================================================================
// BrightnessOperator 実装（8bit Straight形式で処理）
// ========================================================================

ViewPort BrightnessOperator::applyToSingle(const ViewPort& input,
                                           const OperatorContext& ctx) const {
    (void)ctx;  // 未使用

    // 入力を要求形式に変換（既に同じ形式ならコピー）
    ViewPort working = input.convertTo(PixelFormatIDs::RGBA8_Straight);

    // 8bit Straight形式での処理
    ViewPort output(working.width, working.height, PixelFormatIDs::RGBA8_Straight);
    int adjustment = static_cast<int>(brightness_ * 255.0f);

    for (int y = 0; y < working.height; y++) {
        const uint8_t* srcRow = working.getPixelPtr<uint8_t>(0, y);
        uint8_t* dstRow = output.getPixelPtr<uint8_t>(0, y);

        for (int x = 0; x < working.width; x++) {
            int pixelOffset = x * 4;
            // RGB各チャンネルに明るさ調整を適用（ストレート形式なので直接加算）
            for (int c = 0; c < 3; c++) {
                int value = static_cast<int>(srcRow[pixelOffset + c]) + adjustment;
                dstRow[pixelOffset + c] = static_cast<uint8_t>(std::max(0, std::min(255, value)));
            }
            // Alphaはそのままコピー
            dstRow[pixelOffset + 3] = srcRow[pixelOffset + 3];
        }
    }

    return output;
}

// ========================================================================
// GrayscaleOperator 実装（8bit Straight形式で処理）
// ========================================================================

ViewPort GrayscaleOperator::applyToSingle(const ViewPort& input,
                                          const OperatorContext& ctx) const {
    (void)ctx;  // 未使用

    // 入力を要求形式に変換（既に同じ形式ならコピー）
    ViewPort working = input.convertTo(PixelFormatIDs::RGBA8_Straight);

    // 8bit Straight形式での処理
    ViewPort output(working.width, working.height, PixelFormatIDs::RGBA8_Straight);

    for (int y = 0; y < working.height; y++) {
        const uint8_t* srcRow = working.getPixelPtr<uint8_t>(0, y);
        uint8_t* dstRow = output.getPixelPtr<uint8_t>(0, y);

        for (int x = 0; x < working.width; x++) {
            int pixelOffset = x * 4;
            // グレースケール変換（平均法、ストレート形式で正しく処理）
            uint8_t gray = static_cast<uint8_t>(
                (static_cast<uint16_t>(srcRow[pixelOffset]) +
                 static_cast<uint16_t>(srcRow[pixelOffset + 1]) +
                 static_cast<uint16_t>(srcRow[pixelOffset + 2])) / 3
            );
            dstRow[pixelOffset] = gray;       // R
            dstRow[pixelOffset + 1] = gray;   // G
            dstRow[pixelOffset + 2] = gray;   // B
            dstRow[pixelOffset + 3] = srcRow[pixelOffset + 3];  // Alphaはそのままコピー
        }
    }

    return output;
}

// ========================================================================
// BoxBlurOperator 実装（8bit Straight形式で処理）
// ========================================================================

ViewPort BoxBlurOperator::applyToSingle(const ViewPort& input,
                                        const OperatorContext& ctx) const {
    (void)ctx;  // 未使用

    // 入力を要求形式に変換（既に同じ形式ならコピー）
    ViewPort working = input.convertTo(PixelFormatIDs::RGBA8_Straight);

    int width = working.width;
    int height = working.height;

    // 中間バッファ（水平ブラー結果）
    ViewPort temp(width, height, PixelFormatIDs::RGBA8_Straight);

    // パス1: 水平方向のブラー
    for (int y = 0; y < height; y++) {
        const uint8_t* srcRow = working.getPixelPtr<uint8_t>(0, y);
        uint8_t* dstRow = temp.getPixelPtr<uint8_t>(0, y);

        for (int x = 0; x < width; x++) {
            uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            int count = 0;

            int xStart = std::max(0, x - radius_);
            int xEnd = std::min(width - 1, x + radius_);

            for (int nx = xStart; nx <= xEnd; nx++) {
                int pixelOffset = nx * 4;
                sumR += srcRow[pixelOffset];
                sumG += srcRow[pixelOffset + 1];
                sumB += srcRow[pixelOffset + 2];
                sumA += srcRow[pixelOffset + 3];
                count++;
            }

            int outOffset = x * 4;
            dstRow[outOffset] = sumR / count;
            dstRow[outOffset + 1] = sumG / count;
            dstRow[outOffset + 2] = sumB / count;
            dstRow[outOffset + 3] = sumA / count;
        }
    }

    // パス2: 垂直方向のブラー
    ViewPort output(width, height, PixelFormatIDs::RGBA8_Straight);
    for (int y = 0; y < height; y++) {
        uint8_t* dstRow = output.getPixelPtr<uint8_t>(0, y);

        for (int x = 0; x < width; x++) {
            uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            int count = 0;

            int yStart = std::max(0, y - radius_);
            int yEnd = std::min(height - 1, y + radius_);

            for (int ny = yStart; ny <= yEnd; ny++) {
                const uint8_t* tmpRow = temp.getPixelPtr<uint8_t>(0, ny);
                int pixelOffset = x * 4;
                sumR += tmpRow[pixelOffset];
                sumG += tmpRow[pixelOffset + 1];
                sumB += tmpRow[pixelOffset + 2];
                sumA += tmpRow[pixelOffset + 3];
                count++;
            }

            int outOffset = x * 4;
            dstRow[outOffset] = sumR / count;
            dstRow[outOffset + 1] = sumG / count;
            dstRow[outOffset + 2] = sumB / count;
            dstRow[outOffset + 3] = sumA / count;
        }
    }

    return output;
}

// ========================================================================
// AlphaOperator 実装（入力形式に応じて処理）
// ========================================================================

ViewPort AlphaOperator::applyToSingle(const ViewPort& input,
                                      const OperatorContext& ctx) const {
    (void)ctx;  // 未使用

    // 入力が RGBA16_Premultiplied の場合は16bit処理
    if (input.formatID == PixelFormatIDs::RGBA16_Premultiplied) {
        ViewPort output(input.width, input.height, PixelFormatIDs::RGBA16_Premultiplied);
        uint32_t alphaScale = static_cast<uint32_t>(alpha_ * 65536.0f);  // 16.16固定小数点

        for (int y = 0; y < input.height; y++) {
            const uint16_t* srcRow = input.getPixelPtr<uint16_t>(0, y);
            uint16_t* dstRow = output.getPixelPtr<uint16_t>(0, y);

            for (int x = 0; x < input.width; x++) {
                int pixelOffset = x * 4;
                // RGBA全チャンネルにアルファ乗算を適用（Premultiplied形式）
                for (int c = 0; c < 4; c++) {
                    uint32_t value = srcRow[pixelOffset + c];
                    dstRow[pixelOffset + c] = static_cast<uint16_t>((value * alphaScale) >> 16);
                }
            }
        }
        return output;
    }

    // それ以外の場合は RGBA8_Straight で処理（変換が必要なら変換）
    ViewPort working = input.convertTo(PixelFormatIDs::RGBA8_Straight);

    ViewPort output(working.width, working.height, PixelFormatIDs::RGBA8_Straight);
    uint32_t alphaScale = static_cast<uint32_t>(alpha_ * 256.0f);  // 8.8固定小数点

    for (int y = 0; y < working.height; y++) {
        const uint8_t* srcRow = working.getPixelPtr<uint8_t>(0, y);
        uint8_t* dstRow = output.getPixelPtr<uint8_t>(0, y);

        for (int x = 0; x < working.width; x++) {
            int pixelOffset = x * 4;
            // Straight形式: RGBはそのまま、Alphaのみ乗算
            dstRow[pixelOffset]     = srcRow[pixelOffset];      // R
            dstRow[pixelOffset + 1] = srcRow[pixelOffset + 1];  // G
            dstRow[pixelOffset + 2] = srcRow[pixelOffset + 2];  // B
            uint32_t alpha = srcRow[pixelOffset + 3];
            dstRow[pixelOffset + 3] = static_cast<uint8_t>((alpha * alphaScale) >> 8);  // A
        }
    }

    return output;
}

// ========================================================================
// OperatorFactory 実装
// ========================================================================

std::unique_ptr<NodeOperator> OperatorFactory::createFilterOperator(
    const std::string& filterType,
    const std::vector<float>& params
) {
    // ヘルパー: パラメータ取得（範囲外の場合はデフォルト値を返す）
    auto getParam = [&params](size_t index, float defaultVal) -> float {
        return index < params.size() ? params[index] : defaultVal;
    };

    if (filterType == "brightness") {
        return std::make_unique<BrightnessOperator>(getParam(0, 0.0f));
    }
    else if (filterType == "grayscale") {
        return std::make_unique<GrayscaleOperator>();
    }
    else if (filterType == "blur" || filterType == "boxblur") {
        return std::make_unique<BoxBlurOperator>(static_cast<int>(getParam(0, 3.0f)));
    }
    else if (filterType == "alpha") {
        return std::make_unique<AlphaOperator>(getParam(0, 1.0f));
    }

    // 未知のフィルタタイプ
    return nullptr;
}

// ========================================================================
// AffineOperator 実装
// ========================================================================

AffineOperator::AffineOperator(const AffineMatrix& matrix,
                               double originX, double originY,
                               double outputOffsetX, double outputOffsetY,
                               int outputWidth, int outputHeight)
    : matrix_(matrix)
    , originX_(originX), originY_(originY)
    , outputOffsetX_(outputOffsetX), outputOffsetY_(outputOffsetY)
    , outputWidth_(outputWidth), outputHeight_(outputHeight) {}

ViewPort AffineOperator::applyToSingle(const ViewPort& input,
                                       const OperatorContext& ctx) const {
    // 出力サイズの決定（0以下の場合はコンテキストのcanvasSizeを使用）
    int outW = (outputWidth_ > 0) ? outputWidth_ : ctx.canvasWidth;
    int outH = (outputHeight_ > 0) ? outputHeight_ : ctx.canvasHeight;

    ViewPort output(outW, outH, PixelFormatIDs::RGBA16_Premultiplied);
    std::memset(output.data, 0, output.getTotalBytes());

    // 逆行列を計算（出力→入力の座標変換）
    double det = matrix_.a * matrix_.d - matrix_.b * matrix_.c;
    if (std::abs(det) < 1e-10) {
        return output;  // 特異行列の場合は空画像を返す
    }

    double invDet = 1.0 / det;
    double invA = matrix_.d * invDet;
    double invB = -matrix_.b * invDet;
    double invC = -matrix_.c * invDet;
    double invD = matrix_.a * invDet;
    double invTx = (-matrix_.d * matrix_.tx + matrix_.b * matrix_.ty) * invDet;
    double invTy = (matrix_.c * matrix_.tx - matrix_.a * matrix_.ty) * invDet;

    // 固定小数点の小数部ビット数
    constexpr int FIXED_POINT_BITS = 16;
    constexpr int32_t FIXED_POINT_SCALE = 1 << FIXED_POINT_BITS;

    // 固定小数点形式に変換
    int32_t fixedInvA  = std::lround(invA * FIXED_POINT_SCALE);
    int32_t fixedInvB  = std::lround(invB * FIXED_POINT_SCALE);
    int32_t fixedInvC  = std::lround(invC * FIXED_POINT_SCALE);
    int32_t fixedInvD  = std::lround(invD * FIXED_POINT_SCALE);
    int32_t fixedInvTx = std::lround(invTx * FIXED_POINT_SCALE);
    int32_t fixedInvTy = std::lround(invTy * FIXED_POINT_SCALE);

    // 原点中心の変換を固定小数点で適用
    int32_t originXInt = std::lround(originX_);
    int32_t originYInt = std::lround(originY_);
    fixedInvTx += (originXInt << FIXED_POINT_BITS) - originXInt * fixedInvA - fixedInvB * originYInt;
    fixedInvTy += (originYInt << FIXED_POINT_BITS) - originYInt * fixedInvD - fixedInvC * originXInt;

    // 出力座標系オフセットの適用
    if (outputOffsetX_ != 0 || outputOffsetY_ != 0) {
        int32_t offsetXInt = std::lround(outputOffsetX_);
        int32_t offsetYInt = std::lround(outputOffsetY_);
        fixedInvTx -= offsetXInt * fixedInvA + offsetYInt * fixedInvB;
        fixedInvTy -= offsetXInt * fixedInvC + offsetYInt * fixedInvD;
    }

    // 有効描画範囲の事前計算
    auto calcValidRange = [FIXED_POINT_BITS, FIXED_POINT_SCALE](
        int32_t coeff, int32_t base, int minVal, int maxVal, int canvasSize
    ) -> std::pair<int, int> {
        int32_t coeffHalf = coeff >> 1;

        if (coeff == 0) {
            int val = base >> FIXED_POINT_BITS;
            if (base < 0 && (base & (FIXED_POINT_SCALE - 1)) != 0) val--;
            return (val >= minVal && val <= maxVal)
                ? std::make_pair(0, canvasSize - 1)
                : std::make_pair(1, 0);
        }

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

    // ピクセルスキャン（DDAアルゴリズム）
    const int inputStride = input.stride / sizeof(uint16_t);
    const int32_t rowOffsetX = fixedInvB >> 1;
    const int32_t rowOffsetY = fixedInvD >> 1;
    const int32_t dxOffsetX = fixedInvA >> 1;
    const int32_t dxOffsetY = fixedInvC >> 1;

    for (int dy = 0; dy < outH; dy++) {
        int32_t rowBaseX = fixedInvB * dy + fixedInvTx + rowOffsetX;
        int32_t rowBaseY = fixedInvD * dy + fixedInvTy + rowOffsetY;

        auto [xStart, xEnd] = calcValidRange(fixedInvA, rowBaseX, 0, input.width - 1, outW);
        auto [yStart, yEnd] = calcValidRange(fixedInvC, rowBaseY, 0, input.height - 1, outW);
        int dxStart = std::max({0, xStart, yStart});
        int dxEnd = std::min({outW - 1, xEnd, yEnd});

        if (dxStart > dxEnd) continue;

        int32_t srcX_fixed = fixedInvA * dxStart + rowBaseX + dxOffsetX;
        int32_t srcY_fixed = fixedInvC * dxStart + rowBaseY + dxOffsetY;

        uint16_t* dstRow = output.getPixelPtr<uint16_t>(dxStart, dy);
        const uint16_t* inputData = static_cast<const uint16_t*>(input.data);

        for (int dx = dxStart; dx <= dxEnd; dx++) {
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

// ========================================================================
// CompositeOperator 実装
// ========================================================================

ViewPort CompositeOperator::apply(const std::vector<ViewPort>& inputs,
                                  const OperatorContext& ctx) const {
    ViewPort result(ctx.canvasWidth, ctx.canvasHeight, PixelFormatIDs::RGBA16_Premultiplied);

    // キャンバスを透明で初期化
    std::memset(result.data, 0, result.getTotalBytes());

    // 合成の基準点
    double refX = ctx.dstOriginX;
    double refY = ctx.dstOriginY;

    // 各画像を順番に合成
    for (const auto& img : inputs) {
        if (!img.isValid()) continue;

        // srcOrigin ベースの配置
        int offsetX = static_cast<int>(refX - img.srcOriginX);
        int offsetY = static_cast<int>(refY - img.srcOriginY);

        // ループ範囲を事前計算
        int yStart = std::max(0, -offsetY);
        int yEnd = std::min(img.height, ctx.canvasHeight - offsetY);
        int xStart = std::max(0, -offsetX);
        int xEnd = std::min(img.width, ctx.canvasWidth - offsetX);

        if (yStart >= yEnd || xStart >= xEnd) continue;

        for (int y = yStart; y < yEnd; y++) {
            const uint16_t* srcRow = img.getPixelPtr<uint16_t>(0, y);
            uint16_t* dstRow = result.getPixelPtr<uint16_t>(0, y + offsetY);

            for (int x = xStart; x < xEnd; x++) {
                const uint16_t* srcPixel = srcRow + x * 4;
                uint16_t* dstPixel = dstRow + (x + offsetX) * 4;

                uint16_t srcA = srcPixel[3];
                if (srcA == 0) continue;

                uint16_t srcR = srcPixel[0];
                uint16_t srcG = srcPixel[1];
                uint16_t srcB = srcPixel[2];
                uint16_t dstA = dstPixel[3];

                if (srcA != 65535 && dstA != 0) {
                    // プリマルチプライド合成: src over dst
                    uint16_t invSrcA = 65535 - srcA;
                    srcR += (dstPixel[0] * invSrcA) >> 16;
                    srcG += (dstPixel[1] * invSrcA) >> 16;
                    srcB += (dstPixel[2] * invSrcA) >> 16;
                    srcA += (dstA * invSrcA) >> 16;
                }

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

// ========================================================================
// OperatorFactory 追加メソッド
// ========================================================================

std::unique_ptr<NodeOperator> OperatorFactory::createAffineOperator(
    const AffineMatrix& matrix,
    double originX, double originY,
    double outputOffsetX, double outputOffsetY,
    int outputWidth, int outputHeight
) {
    return std::make_unique<AffineOperator>(
        matrix, originX, originY,
        outputOffsetX, outputOffsetY,
        outputWidth, outputHeight
    );
}

std::unique_ptr<NodeOperator> OperatorFactory::createCompositeOperator() {
    return std::make_unique<CompositeOperator>();
}

} // namespace ImageTransform
