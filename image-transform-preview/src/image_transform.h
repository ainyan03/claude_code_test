#ifndef IMAGE_TRANSFORM_H
#define IMAGE_TRANSFORM_H

#include <vector>
#include <cstdint>
#include <cmath>
#include <memory>
#include <string>

namespace ImageTransform {

// 画像データ構造
struct Image {
    std::vector<uint8_t> data;  // RGBA format
    int width;
    int height;

    Image() : width(0), height(0) {}
    Image(int w, int h) : width(w), height(h), data(w * h * 4, 0) {}
};

// フィルタ基底クラス（純粋な処理のみ、UI情報は含まない）
class ImageFilter {
public:
    virtual ~ImageFilter() = default;
    virtual Image apply(const Image& input) const = 0;
    virtual std::string getName() const = 0;
};

// グレースケールフィルタ
class GrayscaleFilter : public ImageFilter {
public:
    Image apply(const Image& input) const override;
    std::string getName() const override { return "Grayscale"; }
};

// 明るさ調整フィルタ
class BrightnessFilter : public ImageFilter {
public:
    explicit BrightnessFilter(float brightness = 0.0f) : brightness(brightness) {}
    Image apply(const Image& input) const override;
    std::string getName() const override { return "Brightness"; }

    void setBrightness(float value) { brightness = value; }
    float getBrightness() const { return brightness; }

private:
    float brightness;  // -1.0 ~ 1.0
};

// ボックスブラーフィルタ
class BoxBlurFilter : public ImageFilter {
public:
    explicit BoxBlurFilter(int radius = 1) : radius(radius) {}
    Image apply(const Image& input) const override;
    std::string getName() const override { return "BoxBlur"; }

    void setRadius(int value) { radius = value > 0 ? value : 1; }
    int getRadius() const { return radius; }

private:
    int radius;
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

// フィルタノードのUI情報（フィルタ処理とは独立）
struct FilterNodeInfo {
    int nodeId;     // ノードの一意なID
    double posX;    // ノードエディタ上のX座標
    double posY;    // ノードエディタ上のY座標

    FilterNodeInfo() : nodeId(0), posX(0.0), posY(0.0) {}
    FilterNodeInfo(int id, double x, double y) : nodeId(id), posX(x), posY(y) {}
};

// レイヤー情報
struct Layer {
    Image image;
    AffineParams params;
    bool visible;
    std::vector<std::unique_ptr<ImageFilter>> filters;  // フィルタパイプライン（純粋な処理）
    std::vector<FilterNodeInfo> nodeInfos;              // UI情報（フィルタと1対1対応）

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

    // フィルタ管理
    void addFilter(int layerId, const std::string& filterType, float param = 0.0f);
    void removeFilter(int layerId, int filterIndex);
    void clearFilters(int layerId);
    int getFilterCount(int layerId) const;

    // ノード管理
    void setFilterNodePosition(int layerId, int filterIndex, double x, double y);
    int getFilterNodeId(int layerId, int filterIndex) const;
    double getFilterNodePosX(int layerId, int filterIndex) const;
    double getFilterNodePosY(int layerId, int filterIndex) const;

    // 画像合成処理
    Image compose();

    // ユーティリティ
    void setCanvasSize(int width, int height);
    int getLayerCount() const { return layers.size(); }

private:
    int canvasWidth;
    int canvasHeight;
    std::vector<Layer> layers;
    int nextNodeId;  // ノードIDのカウンター

    // 内部処理関数
    void applyAffineTransform(const Image& src, Image& dst, const AffineParams& params);
    void blendPixel(uint8_t* dst, const uint8_t* src, double alpha);
    bool getTransformedPixel(const Image& src, double x, double y, uint8_t* pixel);
    Image applyFilters(const Image& input, const std::vector<std::unique_ptr<ImageFilter>>& filters);
};

} // namespace ImageTransform

#endif // IMAGE_TRANSFORM_H
