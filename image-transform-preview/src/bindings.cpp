#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <vector>
#include "image_transform.h"

using namespace emscripten;
using namespace ImageTransform;

// JavaScriptからアクセスしやすいラッパークラス
class ImageProcessorWrapper {
public:
    ImageProcessorWrapper(int width, int height)
        : processor(width, height) {}

    int addLayer(const val& imageData, int width, int height) {
        // JavaScript Uint8ClampedArray からデータを取得
        unsigned int length = imageData["length"].as<unsigned int>();
        std::vector<uint8_t> data(length);

        // JavaScriptの配列をC++のvectorにコピー
        for (unsigned int i = 0; i < length; i++) {
            data[i] = imageData[i].as<uint8_t>();
        }

        return processor.addLayer(data.data(), width, height);
    }

    void removeLayer(int layerId) {
        processor.removeLayer(layerId);
    }

    void setLayerTransform(int layerId, double tx, double ty, double rotation,
                          double scaleX, double scaleY, double alpha) {
        AffineParams params;
        params.translateX = tx;
        params.translateY = ty;
        params.rotation = rotation;
        params.scaleX = scaleX;
        params.scaleY = scaleY;
        params.alpha = alpha;
        processor.setLayerParams(layerId, params);
    }

    void setLayerVisibility(int layerId, bool visible) {
        processor.setLayerVisibility(layerId, visible);
    }

    void moveLayer(int fromIndex, int toIndex) {
        processor.moveLayer(fromIndex, toIndex);
    }

    void setCanvasSize(int width, int height) {
        processor.setCanvasSize(width, height);
    }

    int getLayerCount() const {
        return processor.getLayerCount();
    }

    // フィルタ管理
    void addFilter(int layerId, const std::string& filterType, float param) {
        processor.addFilter(layerId, filterType, param);
    }

    void removeFilter(int layerId, int filterIndex) {
        processor.removeFilter(layerId, filterIndex);
    }

    void clearFilters(int layerId) {
        processor.clearFilters(layerId);
    }

    int getFilterCount(int layerId) const {
        return processor.getFilterCount(layerId);
    }

    // ノード管理
    void setFilterNodePosition(int layerId, int filterIndex, double x, double y) {
        processor.setFilterNodePosition(layerId, filterIndex, x, y);
    }

    int getFilterNodeId(int layerId, int filterIndex) const {
        return processor.getFilterNodeId(layerId, filterIndex);
    }

    double getFilterNodePosX(int layerId, int filterIndex) const {
        return processor.getFilterNodePosX(layerId, filterIndex);
    }

    double getFilterNodePosY(int layerId, int filterIndex) const {
        return processor.getFilterNodePosY(layerId, filterIndex);
    }

    val compose() {
        Image result = processor.compose();

        // C++ vector を JavaScript Uint8ClampedArray に変換
        val uint8Array = val::global("Uint8ClampedArray").new_(
            typed_memory_view(result.data.size(), result.data.data())
        );

        val resultObj = val::object();
        resultObj.set("data", uint8Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    // ノードグラフ用の単体処理関数
    val applyFilterToImage(const val& inputImageObj, const std::string& filterType, float param) {
        // JavaScript画像オブジェクトからC++のImageに変換
        val inputData = inputImageObj["data"];
        int width = inputImageObj["width"].as<int>();
        int height = inputImageObj["height"].as<int>();

        Image input(width, height);
        unsigned int length = inputData["length"].as<unsigned int>();
        for (unsigned int i = 0; i < length; i++) {
            input.data[i] = inputData[i].as<uint8_t>();
        }

        // フィルタ適用
        Image result = processor.applyFilterToImage(input, filterType, param);

        // 結果をJavaScriptオブジェクトに変換
        val uint8Array = val::global("Uint8ClampedArray").new_(
            typed_memory_view(result.data.size(), result.data.data())
        );

        val resultObj = val::object();
        resultObj.set("data", uint8Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    val applyTransformToImage(const val& inputImageObj, double tx, double ty,
                             double rotation, double scaleX, double scaleY, double alpha) {
        // JavaScript画像オブジェクトからC++のImageに変換
        val inputData = inputImageObj["data"];
        int width = inputImageObj["width"].as<int>();
        int height = inputImageObj["height"].as<int>();

        Image input(width, height);
        unsigned int length = inputData["length"].as<unsigned int>();
        for (unsigned int i = 0; i < length; i++) {
            input.data[i] = inputData[i].as<uint8_t>();
        }

        // 変換パラメータ設定
        AffineParams params;
        params.translateX = tx;
        params.translateY = ty;
        params.rotation = rotation;
        params.scaleX = scaleX;
        params.scaleY = scaleY;
        params.alpha = alpha;

        // 変換適用
        Image result = processor.applyTransformToImage(input, params);

        // 結果をJavaScriptオブジェクトに変換
        val uint8Array = val::global("Uint8ClampedArray").new_(
            typed_memory_view(result.data.size(), result.data.data())
        );

        val resultObj = val::object();
        resultObj.set("data", uint8Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    val mergeImages(const val& imagesArray, const val& alphasArray) {
        // JavaScript配列から画像とアルファ値を取得
        unsigned int imageCount = imagesArray["length"].as<unsigned int>();
        std::vector<Image> images;
        std::vector<const Image*> imagePtrs;
        std::vector<double> alphas;

        for (unsigned int i = 0; i < imageCount; i++) {
            val imageObj = imagesArray[i];
            val imageData = imageObj["data"];
            int width = imageObj["width"].as<int>();
            int height = imageObj["height"].as<int>();

            Image img(width, height);
            unsigned int length = imageData["length"].as<unsigned int>();
            for (unsigned int j = 0; j < length; j++) {
                img.data[j] = imageData[j].as<uint8_t>();
            }
            images.push_back(std::move(img));
        }

        // ポインタ配列を作成
        for (auto& img : images) {
            imagePtrs.push_back(&img);
        }

        // アルファ値を取得
        unsigned int alphaCount = alphasArray["length"].as<unsigned int>();
        for (unsigned int i = 0; i < alphaCount; i++) {
            alphas.push_back(alphasArray[i].as<double>());
        }

        // マージ処理
        Image result = processor.mergeImages(imagePtrs, alphas);

        // 結果をJavaScriptオブジェクトに変換
        val uint8Array = val::global("Uint8ClampedArray").new_(
            typed_memory_view(result.data.size(), result.data.data())
        );

        val resultObj = val::object();
        resultObj.set("data", uint8Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    // ========================================================================
    // 16bit Premultiplied Alpha版 高速処理関数群
    // ========================================================================

    // 8bit RGBA → 16bit Premultiplied変換
    val toPremultiplied(const val& inputImageObj) {
        val inputData = inputImageObj["data"];
        int width = inputImageObj["width"].as<int>();
        int height = inputImageObj["height"].as<int>();

        Image input(width, height);
        unsigned int length = inputData["length"].as<unsigned int>();
        for (unsigned int i = 0; i < length; i++) {
            input.data[i] = inputData[i].as<uint8_t>();
        }

        Image16 result = processor.toPremultiplied(input);

        // Uint16Arrayとして返す
        val uint16Array = val::global("Uint16Array").new_(
            typed_memory_view(result.data.size(), result.data.data())
        );

        val resultObj = val::object();
        resultObj.set("data", uint16Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    // 16bit Premultiplied → 8bit RGBA変換
    val fromPremultiplied(const val& inputImageObj) {
        val inputData = inputImageObj["data"];
        int width = inputImageObj["width"].as<int>();
        int height = inputImageObj["height"].as<int>();

        Image16 input(width, height);
        unsigned int length = inputData["length"].as<unsigned int>();
        for (unsigned int i = 0; i < length; i++) {
            input.data[i] = inputData[i].as<uint16_t>();
        }

        Image result = processor.fromPremultiplied(input);

        val uint8Array = val::global("Uint8ClampedArray").new_(
            typed_memory_view(result.data.size(), result.data.data())
        );

        val resultObj = val::object();
        resultObj.set("data", uint8Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    // 16bit版フィルタ処理
    val applyFilterToImage16(const val& inputImageObj, const std::string& filterType, float param) {
        val inputData = inputImageObj["data"];
        int width = inputImageObj["width"].as<int>();
        int height = inputImageObj["height"].as<int>();

        Image16 input(width, height);
        unsigned int length = inputData["length"].as<unsigned int>();
        for (unsigned int i = 0; i < length; i++) {
            input.data[i] = inputData[i].as<uint16_t>();
        }

        Image16 result = processor.applyFilterToImage16(input, filterType, param);

        val uint16Array = val::global("Uint16Array").new_(
            typed_memory_view(result.data.size(), result.data.data())
        );

        val resultObj = val::object();
        resultObj.set("data", uint16Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    // 16bit版行列ベースアフィン変換
    val applyTransformToImage16(const val& inputImageObj, double a, double b, double c,
                                double d, double tx, double ty, double alpha) {
        val inputData = inputImageObj["data"];
        int width = inputImageObj["width"].as<int>();
        int height = inputImageObj["height"].as<int>();

        Image16 input(width, height);
        unsigned int length = inputData["length"].as<unsigned int>();
        for (unsigned int i = 0; i < length; i++) {
            input.data[i] = inputData[i].as<uint16_t>();
        }

        AffineMatrix matrix;
        matrix.a = a;
        matrix.b = b;
        matrix.c = c;
        matrix.d = d;
        matrix.tx = tx;
        matrix.ty = ty;

        Image16 result = processor.applyTransformToImage16(input, matrix, alpha);

        val uint16Array = val::global("Uint16Array").new_(
            typed_memory_view(result.data.size(), result.data.data())
        );

        val resultObj = val::object();
        resultObj.set("data", uint16Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    // 16bit版マージ（超高速版）
    val mergeImages16(const val& imagesArray) {
        unsigned int imageCount = imagesArray["length"].as<unsigned int>();
        std::vector<Image16> images;
        std::vector<const Image16*> imagePtrs;

        for (unsigned int i = 0; i < imageCount; i++) {
            val imageObj = imagesArray[i];
            val imageData = imageObj["data"];
            int width = imageObj["width"].as<int>();
            int height = imageObj["height"].as<int>();

            Image16 img(width, height);
            unsigned int length = imageData["length"].as<unsigned int>();
            for (unsigned int j = 0; j < length; j++) {
                img.data[j] = imageData[j].as<uint16_t>();
            }
            images.push_back(std::move(img));
        }

        for (auto& img : images) {
            imagePtrs.push_back(&img);
        }

        Image16 result = processor.mergeImages16(imagePtrs);

        val uint16Array = val::global("Uint16Array").new_(
            typed_memory_view(result.data.size(), result.data.data())
        );

        val resultObj = val::object();
        resultObj.set("data", uint16Array);
        resultObj.set("width", result.width);
        resultObj.set("height", result.height);

        return resultObj;
    }

    // AffineMatrixをパラメータから生成するヘルパー
    val createAffineMatrix(double translateX, double translateY, double rotation,
                          double scaleX, double scaleY, double centerX, double centerY) {
        AffineParams params;
        params.translateX = translateX;
        params.translateY = translateY;
        params.rotation = rotation;
        params.scaleX = scaleX;
        params.scaleY = scaleY;

        AffineMatrix matrix = AffineMatrix::fromParams(params, centerX, centerY);

        val resultObj = val::object();
        resultObj.set("a", matrix.a);
        resultObj.set("b", matrix.b);
        resultObj.set("c", matrix.c);
        resultObj.set("d", matrix.d);
        resultObj.set("tx", matrix.tx);
        resultObj.set("ty", matrix.ty);

        return resultObj;
    }

private:
    ImageProcessor processor;
};

EMSCRIPTEN_BINDINGS(image_transform) {
    class_<ImageProcessorWrapper>("ImageProcessor")
        .constructor<int, int>()
        .function("addLayer", &ImageProcessorWrapper::addLayer)
        .function("removeLayer", &ImageProcessorWrapper::removeLayer)
        .function("setLayerTransform", &ImageProcessorWrapper::setLayerTransform)
        .function("setLayerVisibility", &ImageProcessorWrapper::setLayerVisibility)
        .function("moveLayer", &ImageProcessorWrapper::moveLayer)
        .function("setCanvasSize", &ImageProcessorWrapper::setCanvasSize)
        .function("getLayerCount", &ImageProcessorWrapper::getLayerCount)
        .function("addFilter", &ImageProcessorWrapper::addFilter)
        .function("removeFilter", &ImageProcessorWrapper::removeFilter)
        .function("clearFilters", &ImageProcessorWrapper::clearFilters)
        .function("getFilterCount", &ImageProcessorWrapper::getFilterCount)
        .function("setFilterNodePosition", &ImageProcessorWrapper::setFilterNodePosition)
        .function("getFilterNodeId", &ImageProcessorWrapper::getFilterNodeId)
        .function("getFilterNodePosX", &ImageProcessorWrapper::getFilterNodePosX)
        .function("getFilterNodePosY", &ImageProcessorWrapper::getFilterNodePosY)
        .function("compose", &ImageProcessorWrapper::compose)
        .function("applyFilterToImage", &ImageProcessorWrapper::applyFilterToImage)
        .function("applyTransformToImage", &ImageProcessorWrapper::applyTransformToImage)
        .function("mergeImages", &ImageProcessorWrapper::mergeImages)
        // 16bit Premultiplied Alpha版 高速処理関数
        .function("toPremultiplied", &ImageProcessorWrapper::toPremultiplied)
        .function("fromPremultiplied", &ImageProcessorWrapper::fromPremultiplied)
        .function("applyFilterToImage16", &ImageProcessorWrapper::applyFilterToImage16)
        .function("applyTransformToImage16", &ImageProcessorWrapper::applyTransformToImage16)
        .function("mergeImages16", &ImageProcessorWrapper::mergeImages16)
        .function("createAffineMatrix", &ImageProcessorWrapper::createAffineMatrix);
}
