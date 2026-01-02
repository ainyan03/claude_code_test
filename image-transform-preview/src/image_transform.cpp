#include "image_transform.h"
#include <algorithm>
#include <cstring>

namespace ImageTransform {

ImageProcessor::ImageProcessor(int canvasWidth, int canvasHeight)
    : canvasWidth(canvasWidth), canvasHeight(canvasHeight), nextNodeId(1) {
}

int ImageProcessor::addLayer(const uint8_t* imageData, int width, int height) {
    Layer layer;
    layer.image.width = width;
    layer.image.height = height;
    layer.image.data.resize(width * height * 4);
    std::memcpy(layer.image.data.data(), imageData, width * height * 4);
    layer.visible = true;

    layers.push_back(std::move(layer));
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
        // フィルタを追加（純粋な処理のみ）
        layers[layerId].filters.push_back(std::move(filter));

        // UI情報を別途追加
        int currentNodeId = nextNodeId++;
        double defaultX = 100.0;
        double defaultY = 100.0 + layers[layerId].nodeInfos.size() * 80.0;
        layers[layerId].nodeInfos.emplace_back(currentNodeId, defaultX, defaultY);
    }
}

void ImageProcessor::removeFilter(int layerId, int filterIndex) {
    if (layerId >= 0 && layerId < static_cast<int>(layers.size()) &&
        filterIndex >= 0 && filterIndex < static_cast<int>(layers[layerId].filters.size())) {
        layers[layerId].filters.erase(layers[layerId].filters.begin() + filterIndex);
        // UI情報も同期して削除
        if (filterIndex < static_cast<int>(layers[layerId].nodeInfos.size())) {
            layers[layerId].nodeInfos.erase(layers[layerId].nodeInfos.begin() + filterIndex);
        }
    }
}

void ImageProcessor::clearFilters(int layerId) {
    if (layerId >= 0 && layerId < static_cast<int>(layers.size())) {
        layers[layerId].filters.clear();
        // UI情報もクリア
        layers[layerId].nodeInfos.clear();
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

// ノード管理（UI情報のみ、フィルタ処理には影響しない）
void ImageProcessor::setFilterNodePosition(int layerId, int filterIndex, double x, double y) {
    if (layerId >= 0 && layerId < static_cast<int>(layers.size()) &&
        filterIndex >= 0 && filterIndex < static_cast<int>(layers[layerId].nodeInfos.size())) {
        layers[layerId].nodeInfos[filterIndex].posX = x;
        layers[layerId].nodeInfos[filterIndex].posY = y;
    }
}

int ImageProcessor::getFilterNodeId(int layerId, int filterIndex) const {
    if (layerId >= 0 && layerId < static_cast<int>(layers.size()) &&
        filterIndex >= 0 && filterIndex < static_cast<int>(layers[layerId].nodeInfos.size())) {
        return layers[layerId].nodeInfos[filterIndex].nodeId;
    }
    return -1;
}

double ImageProcessor::getFilterNodePosX(int layerId, int filterIndex) const {
    if (layerId >= 0 && layerId < static_cast<int>(layers.size()) &&
        filterIndex >= 0 && filterIndex < static_cast<int>(layers[layerId].nodeInfos.size())) {
        return layers[layerId].nodeInfos[filterIndex].posX;
    }
    return 0.0;
}

double ImageProcessor::getFilterNodePosY(int layerId, int filterIndex) const {
    if (layerId >= 0 && layerId < static_cast<int>(layers.size()) &&
        filterIndex >= 0 && filterIndex < static_cast<int>(layers[layerId].nodeInfos.size())) {
        return layers[layerId].nodeInfos[filterIndex].posY;
    }
    return 0.0;
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

// ノードグラフ用: 単一画像にフィルタを適用
Image ImageProcessor::applyFilterToImage(const Image& input, const std::string& filterType, float param) const {
    std::unique_ptr<ImageFilter> filter;

    if (filterType == "grayscale") {
        filter = std::make_unique<GrayscaleFilter>();
    } else if (filterType == "brightness") {
        filter = std::make_unique<BrightnessFilter>(param);
    } else if (filterType == "blur") {
        filter = std::make_unique<BoxBlurFilter>(static_cast<int>(param));
    }

    if (filter) {
        return filter->apply(input);
    }

    return input;  // フィルタが見つからない場合は入力をそのまま返す
}

// ノードグラフ用: 単一画像にアフィン変換を適用
Image ImageProcessor::applyTransformToImage(const Image& input, const AffineParams& params) const {
    Image result(canvasWidth, canvasHeight);
    std::fill(result.data.begin(), result.data.end(), 0);

    // アフィン変換を適用
    Image transformed(canvasWidth, canvasHeight);
    const_cast<ImageProcessor*>(this)->applyAffineTransform(input, transformed, params);

    return transformed;
}

// ノードグラフ用: 複数画像をマージ（合成ノード）
Image ImageProcessor::mergeImages(const std::vector<const Image*>& images, const std::vector<double>& alphas) const {
    Image result(canvasWidth, canvasHeight);

    // キャンバスを透明で初期化
    std::fill(result.data.begin(), result.data.end(), 0);

    // 各画像を順番に合成
    for (size_t i = 0; i < images.size() && i < alphas.size(); i++) {
        const Image* img = images[i];
        double alpha = alphas[i];

        if (!img) continue;

        // 画像サイズがキャンバスサイズと異なる場合は中央配置
        int offsetX = (canvasWidth - img->width) / 2;
        int offsetY = (canvasHeight - img->height) / 2;

        for (int y = 0; y < img->height && (y + offsetY) < canvasHeight; y++) {
            for (int x = 0; x < img->width && (x + offsetX) < canvasWidth; x++) {
                if (offsetX + x < 0 || offsetY + y < 0) continue;

                int srcIdx = (y * img->width + x) * 4;
                int dstIdx = ((y + offsetY) * canvasWidth + (x + offsetX)) * 4;

                // インライン化されたアルファブレンディング（パフォーマンス最適化）
                uint8_t* dst = &result.data[dstIdx];
                const uint8_t* src = &img->data[srcIdx];

                double srcAlpha = (src[3] / 255.0) * alpha;
                double dstAlpha = dst[3] / 255.0;
                double outAlpha = srcAlpha + dstAlpha * (1.0 - srcAlpha);

                if (outAlpha > 0.0) {
                    double invOutAlpha = 1.0 / outAlpha;
                    double srcWeight = srcAlpha * invOutAlpha;
                    double dstWeight = dstAlpha * (1.0 - srcAlpha) * invOutAlpha;

                    dst[0] = static_cast<uint8_t>(src[0] * srcWeight + dst[0] * dstWeight);
                    dst[1] = static_cast<uint8_t>(src[1] * srcWeight + dst[1] * dstWeight);
                    dst[2] = static_cast<uint8_t>(src[2] * srcWeight + dst[2] * dstWeight);
                    dst[3] = static_cast<uint8_t>(outAlpha * 255.0);
                }
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
    int width = input.width;
    int height = input.height;

    // 分離可能ブラー: 水平方向 → 垂直方向の2パスで処理
    // O(width * height * radius^2) → O(width * height * radius)

    // 中間バッファ（水平ブラー結果）
    Image temp(width, height);

    // パス1: 水平方向のブラー
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            int count = 0;

            int xStart = std::max(0, x - radius);
            int xEnd = std::min(width - 1, x + radius);

            for (int nx = xStart; nx <= xEnd; nx++) {
                int idx = (y * width + nx) * 4;
                sumR += input.data[idx];
                sumG += input.data[idx + 1];
                sumB += input.data[idx + 2];
                sumA += input.data[idx + 3];
                count++;
            }

            int outIdx = (y * width + x) * 4;
            temp.data[outIdx] = sumR / count;
            temp.data[outIdx + 1] = sumG / count;
            temp.data[outIdx + 2] = sumB / count;
            temp.data[outIdx + 3] = sumA / count;
        }
    }

    // パス2: 垂直方向のブラー
    Image output(width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            int count = 0;

            int yStart = std::max(0, y - radius);
            int yEnd = std::min(height - 1, y + radius);

            for (int ny = yStart; ny <= yEnd; ny++) {
                int idx = (ny * width + x) * 4;
                sumR += temp.data[idx];
                sumG += temp.data[idx + 1];
                sumB += temp.data[idx + 2];
                sumA += temp.data[idx + 3];
                count++;
            }

            int outIdx = (y * width + x) * 4;
            output.data[outIdx] = sumR / count;
            output.data[outIdx + 1] = sumG / count;
            output.data[outIdx + 2] = sumB / count;
            output.data[outIdx + 3] = sumA / count;
        }
    }

    return output;
}

// ========================================================================
// 16bit Premultiplied Alpha 高速処理関数群
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

// 8bit RGBA → 16bit Premultiplied RGBA変換
Image16 ImageProcessor::toPremultiplied(const Image& input) const {
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
        uint16_t a16 = (a8 << 8) | a8;

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

// 16bit版フィルタ処理
Image16 ImageProcessor::applyFilterToImage16(const Image16& input, const std::string& filterType, float param) const {
    // 簡易実装: 一旦8bitに戻してフィルタ適用後に16bitに変換
    // TODO: 将来的には16bit直接処理に最適化
    Image img8 = fromPremultiplied(input);
    Image filtered8 = applyFilterToImage(img8, filterType, param);
    return toPremultiplied(filtered8);
}

// ========================================================================
// NodeGraphEvaluator実装（ノードグラフ評価エンジン）
// ========================================================================

NodeGraphEvaluator::NodeGraphEvaluator(int width, int height)
    : canvasWidth(width), canvasHeight(height), processor(width, height) {}

void NodeGraphEvaluator::setCanvasSize(int width, int height) {
    canvasWidth = width;
    canvasHeight = height;
    processor.setCanvasSize(width, height);
    // キャンバスサイズ変更時はpremultipliedキャッシュをクリア
    layerPremulCache.clear();
}

void NodeGraphEvaluator::setLayerImage(int layerId, const Image& img) {
    layerImages[layerId] = img;
    // 画像更新時は該当レイヤーのキャッシュをクリア
    layerPremulCache.erase(layerId);
}

void NodeGraphEvaluator::setNodes(const std::vector<GraphNode>& newNodes) {
    nodes = newNodes;
}

void NodeGraphEvaluator::setConnections(const std::vector<GraphConnection>& newConnections) {
    connections = newConnections;
}

// レイヤー画像のpremultiplied変換（変換パラメータ付き、キャッシュ使用）
Image16 NodeGraphEvaluator::getLayerPremultiplied(int layerId, const AffineParams& transform) {
    // レイヤー画像を取得
    auto it = layerImages.find(layerId);
    if (it == layerImages.end()) {
        return Image16(canvasWidth, canvasHeight);  // 空画像
    }

    const Image& img = it->second;

    // premultiplied変換（キャッシュを使用）
    Image16 premul;
    auto cacheIt = layerPremulCache.find(layerId);
    if (cacheIt != layerPremulCache.end()) {
        premul = cacheIt->second;
    } else {
        premul = processor.toPremultiplied(img);
        layerPremulCache[layerId] = premul;
    }

    // アフィン変換が必要かチェック
    const bool needsTransform = (
        transform.translateX != 0 || transform.translateY != 0 ||
        transform.rotation != 0 || transform.scaleX != 1.0 ||
        transform.scaleY != 1.0 || transform.alpha != 1.0
    );

    if (needsTransform) {
        // 元画像の中心を回転軸にする
        const Image& originalImg = layerImages.at(layerId);
        double centerX = originalImg.width / 2.0;
        double centerY = originalImg.height / 2.0;
        AffineMatrix matrix = AffineMatrix::fromParams(transform, centerX, centerY);
        return processor.applyTransformToImage16(premul, matrix, transform.alpha);
    }

    return premul;
}

// ノードを再帰的に評価
Image16 NodeGraphEvaluator::evaluateNode(const std::string& nodeId, std::set<std::string>& visited) {
    // 循環参照チェック
    if (visited.find(nodeId) != visited.end()) {
        return Image16(canvasWidth, canvasHeight);  // 空画像
    }
    visited.insert(nodeId);

    // キャッシュチェック
    auto cacheIt = nodeResultCache.find(nodeId);
    if (cacheIt != nodeResultCache.end()) {
        return cacheIt->second;
    }

    // ノードを検索
    const GraphNode* node = nullptr;
    for (const auto& n : nodes) {
        if (n.id == nodeId) {
            node = &n;
            break;
        }
    }

    if (!node) {
        return Image16(canvasWidth, canvasHeight);  // 空画像
    }

    Image16 result(canvasWidth, canvasHeight);

    if (node->type == "image") {
        // 画像ノード: レイヤー画像を取得してpremultiplied変換 + アフィン変換
        result = getLayerPremultiplied(node->layerId, node->transform);

    } else if (node->type == "filter") {
        // フィルタノード: 入力画像にフィルタを適用
        // 入力接続を検索
        const GraphConnection* inputConn = nullptr;
        for (const auto& conn : connections) {
            if (conn.toNodeId == nodeId && conn.toPort == "in") {
                inputConn = &conn;
                break;
            }
        }

        if (inputConn) {
            Image16 inputImage = evaluateNode(inputConn->fromNodeId, visited);

            std::string filterType;
            float filterParam;

            if (node->independent) {
                // 独立フィルタノード
                filterType = node->filterType;
                filterParam = node->filterParam;
            } else {
                // レイヤー付帯フィルタノード（未実装：簡略化のためスキップ）
                result = inputImage;
                nodeResultCache[nodeId] = result;
                return result;
            }

            result = processor.applyFilterToImage16(inputImage, filterType, filterParam);
        }

    } else if (node->type == "composite") {
        // 合成ノード: 可変長入力を合成
        std::vector<Image16> images;
        std::vector<const Image16*> imagePtrs;

        // 動的な入力配列を使用（compositeInputsが空の場合は旧形式の互換性処理）
        if (!node->compositeInputs.empty()) {
            // 新形式: 動的な入力配列
            for (const auto& input : node->compositeInputs) {
                // この入力ポートへの接続を検索
                for (const auto& conn : connections) {
                    if (conn.toNodeId == nodeId && conn.toPort == input.id) {
                        Image16 img = evaluateNode(conn.fromNodeId, visited);

                        // アルファ値を適用
                        if (input.alpha != 1.0) {
                            uint16_t alphaU16 = static_cast<uint16_t>(input.alpha * 65535);
                            for (size_t i = 0; i < img.data.size(); i++) {
                                img.data[i] = (img.data[i] * alphaU16) >> 16;
                            }
                        }

                        images.push_back(std::move(img));
                        break;
                    }
                }
            }
        } else {
            // 旧形式: alpha1, alpha2（後方互換性）
            // 入力1を取得
            for (const auto& conn : connections) {
                if (conn.toNodeId == nodeId && conn.toPort == "in1") {
                    Image16 img1 = evaluateNode(conn.fromNodeId, visited);

                    // alpha1を適用
                    if (node->alpha1 != 1.0) {
                        uint16_t alphaU16 = static_cast<uint16_t>(node->alpha1 * 65535);
                        for (size_t i = 0; i < img1.data.size(); i++) {
                            img1.data[i] = (img1.data[i] * alphaU16) >> 16;
                        }
                    }

                    images.push_back(std::move(img1));
                    break;
                }
            }

            // 入力2を取得
            for (const auto& conn : connections) {
                if (conn.toNodeId == nodeId && conn.toPort == "in2") {
                    Image16 img2 = evaluateNode(conn.fromNodeId, visited);

                    // alpha2を適用
                    if (node->alpha2 != 1.0) {
                        uint16_t alphaU16 = static_cast<uint16_t>(node->alpha2 * 65535);
                        for (size_t i = 0; i < img2.data.size(); i++) {
                            img2.data[i] = (img2.data[i] * alphaU16) >> 16;
                        }
                    }

                    images.push_back(std::move(img2));
                    break;
                }
            }
        }

        // ポインタ配列を作成
        for (auto& img : images) {
            imagePtrs.push_back(&img);
        }

        // 合成
        if (imagePtrs.size() == 1) {
            result = images[0];
        } else if (imagePtrs.size() > 1) {
            result = processor.mergeImages16(imagePtrs);
        }

        // 合成ノードのアフィン変換
        const AffineParams& p = node->compositeTransform;
        const bool needsTransform = (
            p.translateX != 0 || p.translateY != 0 || p.rotation != 0 ||
            p.scaleX != 1.0 || p.scaleY != 1.0 || p.alpha != 1.0
        );

        if (needsTransform) {
            double centerX = canvasWidth / 2.0;
            double centerY = canvasHeight / 2.0;
            AffineMatrix matrix = AffineMatrix::fromParams(p, centerX, centerY);
            result = processor.applyTransformToImage16(result, matrix, p.alpha);
        }
    }

    // キャッシュに保存
    nodeResultCache[nodeId] = result;
    return result;
}

// ノードグラフ全体を評価（1回のWASM呼び出しで完結）
Image NodeGraphEvaluator::evaluateGraph() {
    // ノード評価キャッシュをクリア
    nodeResultCache.clear();

    // 出力ノードを検索
    const GraphNode* outputNode = nullptr;
    for (const auto& node : nodes) {
        if (node.type == "output") {
            outputNode = &node;
            break;
        }
    }

    if (!outputNode) {
        return Image(canvasWidth, canvasHeight);  // 空画像
    }

    // 出力ノードへの入力接続を検索
    const GraphConnection* inputConn = nullptr;
    for (const auto& conn : connections) {
        if (conn.toNodeId == outputNode->id && conn.toPort == "in") {
            inputConn = &conn;
            break;
        }
    }

    if (!inputConn) {
        return Image(canvasWidth, canvasHeight);  // 空画像
    }

    // グラフを評価（16bit premultiplied）
    std::set<std::string> visited;
    Image16 result16 = evaluateNode(inputConn->fromNodeId, visited);

    // 16bit → 8bit変換
    return processor.fromPremultiplied(result16);
}

} // namespace ImageTransform
