#include "image_transform.h"
#include <algorithm>
#include <cstring>

namespace ImageTransform {

ImageProcessor::ImageProcessor(int canvasWidth, int canvasHeight)
    : canvasWidth(canvasWidth), canvasHeight(canvasHeight) {
}

int ImageProcessor::addLayer(const uint8_t* imageData, int width, int height) {
    Layer layer;
    layer.image.width = width;
    layer.image.height = height;
    layer.image.data.resize(width * height * 4);
    std::memcpy(layer.image.data.data(), imageData, width * height * 4);
    layer.visible = true;

    layers.push_back(layer);
    return layers.size() - 1;
}

void ImageProcessor::removeLayer(int layerId) {
    if (layerId >= 0 && layerId < static_cast<int>(layers.size())) {
        layers.erase(layers.begin() + layerId);
    }
}

void ImageProcessor::setLayerParams(int layerId, const AffineParams& params) {
    if (layerId >= 0 && layerId < static_cast<int>(layers.size())) {
        layers[layerId].params = params;
    }
}

void ImageProcessor::setLayerVisibility(int layerId, bool visible) {
    if (layerId >= 0 && layerId < static_cast<int>(layers.size())) {
        layers[layerId].visible = visible;
    }
}

void ImageProcessor::moveLayer(int fromIndex, int toIndex) {
    if (fromIndex >= 0 && fromIndex < static_cast<int>(layers.size()) &&
        toIndex >= 0 && toIndex < static_cast<int>(layers.size())) {
        Layer temp = std::move(layers[fromIndex]);
        layers.erase(layers.begin() + fromIndex);
        layers.insert(layers.begin() + toIndex, std::move(temp));
    }
}

void ImageProcessor::setCanvasSize(int width, int height) {
    canvasWidth = width;
    canvasHeight = height;
}

// フィルタ管理
void ImageProcessor::addFilter(int layerId, const std::string& filterType, float param) {
    if (layerId < 0 || layerId >= static_cast<int>(layers.size())) {
        return;
    }

    std::unique_ptr<ImageFilter> filter;

    if (filterType == "grayscale") {
        filter = std::make_unique<GrayscaleFilter>();
    } else if (filterType == "brightness") {
        filter = std::make_unique<BrightnessFilter>(param);
    } else if (filterType == "blur") {
        filter = std::make_unique<BoxBlurFilter>(static_cast<int>(param));
    }

    if (filter) {
        layers[layerId].filters.push_back(std::move(filter));
    }
}

void ImageProcessor::removeFilter(int layerId, int filterIndex) {
    if (layerId >= 0 && layerId < static_cast<int>(layers.size()) &&
        filterIndex >= 0 && filterIndex < static_cast<int>(layers[layerId].filters.size())) {
        layers[layerId].filters.erase(layers[layerId].filters.begin() + filterIndex);
    }
}

void ImageProcessor::clearFilters(int layerId) {
    if (layerId >= 0 && layerId < static_cast<int>(layers.size())) {
        layers[layerId].filters.clear();
    }
}

int ImageProcessor::getFilterCount(int layerId) const {
    if (layerId >= 0 && layerId < static_cast<int>(layers.size())) {
        return layers[layerId].filters.size();
    }
    return 0;
}

Image ImageProcessor::applyFilters(const Image& input, const std::vector<std::unique_ptr<ImageFilter>>& filters) {
    Image result = input;
    for (const auto& filter : filters) {
        result = filter->apply(result);
    }
    return result;
}

Image ImageProcessor::compose() {
    Image result(canvasWidth, canvasHeight);

    // キャンバスを透明で初期化
    std::fill(result.data.begin(), result.data.end(), 0);

    // 各レイヤーを合成
    for (const auto& layer : layers) {
        if (!layer.visible) continue;

        // フィルタを適用
        Image filtered = applyFilters(layer.image, layer.filters);

        // アフィン変換を適用
        Image transformed(canvasWidth, canvasHeight);
        applyAffineTransform(filtered, transformed, layer.params);

        // アルファブレンディング
        for (int y = 0; y < canvasHeight; y++) {
            for (int x = 0; x < canvasWidth; x++) {
                int idx = (y * canvasWidth + x) * 4;
                blendPixel(&result.data[idx], &transformed.data[idx], layer.params.alpha);
            }
        }
    }

    return result;
}

void ImageProcessor::applyAffineTransform(const Image& src, Image& dst, const AffineParams& params) {
    // キャンバス中心を基準点とする
    double centerX = canvasWidth / 2.0;
    double centerY = canvasHeight / 2.0;

    double cosTheta = std::cos(-params.rotation);  // 逆変換のため符号反転
    double sinTheta = std::sin(-params.rotation);

    for (int dstY = 0; dstY < canvasHeight; dstY++) {
        for (int dstX = 0; dstX < canvasWidth; dstX++) {
            // キャンバス座標系からの相対座標
            double dx = dstX - centerX;
            double dy = dstY - centerY;

            // 平行移動を適用（逆変換）
            dx -= params.translateX;
            dy -= params.translateY;

            // 回転を適用（逆変換）
            double rotX = dx * cosTheta - dy * sinTheta;
            double rotY = dx * sinTheta + dy * cosTheta;

            // スケールを適用（逆変換）
            if (params.scaleX != 0.0 && params.scaleY != 0.0) {
                rotX /= params.scaleX;
                rotY /= params.scaleY;
            }

            // ソース画像の中心を基準に座標変換
            double srcX = rotX + src.width / 2.0;
            double srcY = rotY + src.height / 2.0;

            // バイリニア補間でピクセル取得
            int dstIdx = (dstY * canvasWidth + dstX) * 4;
            if (!getTransformedPixel(src, srcX, srcY, &dst.data[dstIdx])) {
                // 範囲外は透明
                dst.data[dstIdx] = 0;
                dst.data[dstIdx + 1] = 0;
                dst.data[dstIdx + 2] = 0;
                dst.data[dstIdx + 3] = 0;
            }
        }
    }
}

bool ImageProcessor::getTransformedPixel(const Image& src, double x, double y, uint8_t* pixel) {
    if (x < 0 || y < 0 || x >= src.width - 1 || y >= src.height - 1) {
        return false;
    }

    int x0 = static_cast<int>(x);
    int y0 = static_cast<int>(y);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    double fx = x - x0;
    double fy = y - y0;

    // バイリニア補間
    for (int c = 0; c < 4; c++) {
        double p00 = src.data[(y0 * src.width + x0) * 4 + c];
        double p10 = src.data[(y0 * src.width + x1) * 4 + c];
        double p01 = src.data[(y1 * src.width + x0) * 4 + c];
        double p11 = src.data[(y1 * src.width + x1) * 4 + c];

        double p0 = p00 * (1 - fx) + p10 * fx;
        double p1 = p01 * (1 - fx) + p11 * fx;
        double p = p0 * (1 - fy) + p1 * fy;

        pixel[c] = static_cast<uint8_t>(std::max(0.0, std::min(255.0, p)));
    }

    return true;
}

void ImageProcessor::blendPixel(uint8_t* dst, const uint8_t* src, double alpha) {
    double srcAlpha = (src[3] / 255.0) * alpha;
    double dstAlpha = dst[3] / 255.0;

    // アルファ合成
    double outAlpha = srcAlpha + dstAlpha * (1.0 - srcAlpha);

    if (outAlpha > 0.0) {
        for (int c = 0; c < 3; c++) {
            double srcC = src[c] / 255.0;
            double dstC = dst[c] / 255.0;
            double outC = (srcC * srcAlpha + dstC * dstAlpha * (1.0 - srcAlpha)) / outAlpha;
            dst[c] = static_cast<uint8_t>(outC * 255.0);
        }
        dst[3] = static_cast<uint8_t>(outAlpha * 255.0);
    }
}

// ========================================
// フィルタ実装
// ========================================

// グレースケールフィルタ
Image GrayscaleFilter::apply(const Image& input) const {
    Image output = input;

    for (size_t i = 0; i < input.data.size(); i += 4) {
        // グレースケール変換（平均法）
        uint8_t gray = static_cast<uint8_t>(
            (input.data[i] + input.data[i + 1] + input.data[i + 2]) / 3
        );
        output.data[i] = gray;       // R
        output.data[i + 1] = gray;   // G
        output.data[i + 2] = gray;   // B
        // Alpha は変更しない
    }

    return output;
}

// 明るさ調整フィルタ
Image BrightnessFilter::apply(const Image& input) const {
    Image output = input;

    // 明るさ調整値を -255 ~ 255 の範囲に変換
    int adjustment = static_cast<int>(brightness * 255.0f);

    for (size_t i = 0; i < input.data.size(); i += 4) {
        // RGB各チャンネルに明るさ調整を適用
        for (int c = 0; c < 3; c++) {
            int value = input.data[i + c] + adjustment;
            output.data[i + c] = static_cast<uint8_t>(std::max(0, std::min(255, value)));
        }
        // Alpha は変更しない
    }

    return output;
}

// ボックスブラーフィルタ
Image BoxBlurFilter::apply(const Image& input) const {
    Image output = input;

    int width = input.width;
    int height = input.height;

    // 各ピクセルに対してボックスブラーを適用
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            int count = 0;

            // 周囲のピクセルを走査
            for (int dy = -radius; dy <= radius; dy++) {
                for (int dx = -radius; dx <= radius; dx++) {
                    int nx = x + dx;
                    int ny = y + dy;

                    // 範囲チェック
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                        int idx = (ny * width + nx) * 4;
                        sumR += input.data[idx];
                        sumG += input.data[idx + 1];
                        sumB += input.data[idx + 2];
                        sumA += input.data[idx + 3];
                        count++;
                    }
                }
            }

            // 平均値を計算
            int outIdx = (y * width + x) * 4;
            if (count > 0) {
                output.data[outIdx] = static_cast<uint8_t>(sumR / count);
                output.data[outIdx + 1] = static_cast<uint8_t>(sumG / count);
                output.data[outIdx + 2] = static_cast<uint8_t>(sumB / count);
                output.data[outIdx + 3] = static_cast<uint8_t>(sumA / count);
            }
        }
    }

    return output;
}

} // namespace ImageTransform
