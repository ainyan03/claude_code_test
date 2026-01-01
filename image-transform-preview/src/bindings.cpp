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
        .function("compose", &ImageProcessorWrapper::compose);
}
