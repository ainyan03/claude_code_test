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
        Layer temp = layers[fromIndex];
        layers.erase(layers.begin() + fromIndex);
        layers.insert(layers.begin() + toIndex, temp);
    }
}

void ImageProcessor::setCanvasSize(int width, int height) {
    canvasWidth = width;
    canvasHeight = height;
}

Image ImageProcessor::compose() {
    Image result(canvasWidth, canvasHeight);

    // キャンバスを透明で初期化
    std::fill(result.data.begin(), result.data.end(), 0);

    // 各レイヤーを合成
    for (const auto& layer : layers) {
        if (!layer.visible) continue;

        Image transformed(canvasWidth, canvasHeight);
        applyAffineTransform(layer.image, transformed, layer.params);

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

} // namespace ImageTransform
