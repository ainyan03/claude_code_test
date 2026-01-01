#ifndef IMAGE_TRANSFORM_H
#define IMAGE_TRANSFORM_H

#include <vector>
#include <cstdint>
#include <cmath>

namespace ImageTransform {

// 画像データ構造
struct Image {
    std::vector<uint8_t> data;  // RGBA format
    int width;
    int height;

    Image() : width(0), height(0) {}
    Image(int w, int h) : width(w), height(h), data(w * h * 4, 0) {}
};

// アフィン変換パラメータ
struct AffineParams {
    double translateX;  // 平行移動 X
    double translateY;  // 平行移動 Y
    double rotation;    // 回転角度（ラジアン）
    double scaleX;      // スケール X
    double scaleY;      // スケール Y
    double alpha;       // 透過度 (0.0 - 1.0)

    AffineParams()
        : translateX(0), translateY(0), rotation(0),
          scaleX(1.0), scaleY(1.0), alpha(1.0) {}
};

// レイヤー情報
struct Layer {
    Image image;
    AffineParams params;
    bool visible;

    Layer() : visible(true) {}
};

// 画像変換クラス
class ImageProcessor {
public:
    ImageProcessor(int canvasWidth, int canvasHeight);

    // レイヤー管理
    int addLayer(const uint8_t* imageData, int width, int height);
    void removeLayer(int layerId);
    void setLayerParams(int layerId, const AffineParams& params);
    void setLayerVisibility(int layerId, bool visible);
    void moveLayer(int fromIndex, int toIndex);

    // 画像合成処理
    Image compose();

    // ユーティリティ
    void setCanvasSize(int width, int height);
    int getLayerCount() const { return layers.size(); }

private:
    int canvasWidth;
    int canvasHeight;
    std::vector<Layer> layers;

    // 内部処理関数
    void applyAffineTransform(const Image& src, Image& dst, const AffineParams& params);
    void blendPixel(uint8_t* dst, const uint8_t* src, double alpha);
    bool getTransformedPixel(const Image& src, double x, double y, uint8_t* pixel);
};

} // namespace ImageTransform

#endif // IMAGE_TRANSFORM_H
