#ifndef IMAGE_TRANSFORM_H
#define IMAGE_TRANSFORM_H

#include <vector>
#include <cstdint>
#include <cmath>
#include <memory>
#include <string>
#include <map>
#include <set>

namespace ImageTransform {

// 画像データ構造（8bit RGBA、ストレートアルファ）
struct Image {
    std::vector<uint8_t> data;  // RGBA format
    int width;
    int height;

    Image() : width(0), height(0) {}
    Image(int w, int h) : width(w), height(h), data(w * h * 4, 0) {}
};

// 16bit プリマルチプライドアルファ画像（ノードグラフ内部処理用）
struct Image16 {
    std::vector<uint16_t> data;  // Premultiplied RGBA, 16bit per channel
    int width;
    int height;

    Image16() : width(0), height(0) {}
    Image16(int w, int h) : width(w), height(h), data(w * h * 4, 0) {}
};

// ========================================================================
// 16bit フィルタクラス群（新アーキテクチャ）
// ========================================================================

// フィルタパラメータ構造体
struct BrightnessFilterParams {
    float brightness;  // -1.0 ~ 1.0

    BrightnessFilterParams(float b = 0.0f) : brightness(b) {}
};

struct GrayscaleFilterParams {
    // パラメータなし（将来の拡張用）
    GrayscaleFilterParams() {}
};

struct BoxBlurFilterParams {
    int radius;  // ブラー半径（1以上）

    BoxBlurFilterParams(int r = 3) : radius(r > 0 ? r : 1) {}
};

// 16bitフィルタの基底クラス
class ImageFilter16 {
protected:
    ImageFilter16(const char* name) : name_(name) {}

public:
    virtual ~ImageFilter16() = default;
    virtual Image16 apply(const Image16& input) const = 0;
    const char* getName() const { return name_; }

private:
    const char* name_;
};

// 明るさ調整フィルタ（16bit版）
class BrightnessFilter16 : public ImageFilter16 {
public:
    explicit BrightnessFilter16(const BrightnessFilterParams& params)
        : ImageFilter16("Brightness"), params_(params) {}

    Image16 apply(const Image16& input) const override;

private:
    BrightnessFilterParams params_;
};

// グレースケールフィルタ（16bit版）
class GrayscaleFilter16 : public ImageFilter16 {
public:
    explicit GrayscaleFilter16(const GrayscaleFilterParams& params = {})
        : ImageFilter16("Grayscale"), params_(params) {}

    Image16 apply(const Image16& input) const override;

private:
    GrayscaleFilterParams params_;
};

// ボックスブラーフィルタ（16bit版）
class BoxBlurFilter16 : public ImageFilter16 {
public:
    explicit BoxBlurFilter16(const BoxBlurFilterParams& params)
        : ImageFilter16("BoxBlur"), params_(params) {}

    Image16 apply(const Image16& input) const override;

private:
    BoxBlurFilterParams params_;
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

// 2x3アフィン変換行列
// [a  b  tx]   [x]   [a*x + b*y + tx]
// [c  d  ty] * [y] = [c*x + d*y + ty]
//              [1]
struct AffineMatrix {
    double a, b, c, d, tx, ty;

    AffineMatrix() : a(1), b(0), c(0), d(1), tx(0), ty(0) {}

    // AffineParamsから行列を生成
    static AffineMatrix fromParams(const AffineParams& params, double centerX, double centerY);
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

    // ノードグラフ用の処理関数（16bit版）
    Image16 applyFilterToImage16(const Image16& input, const std::string& filterType, float param = 0.0f) const;
    Image16 applyTransformToImage16(const Image16& input, const AffineMatrix& matrix, double alpha = 1.0) const;
    Image16 mergeImages16(const std::vector<const Image16*>& images) const;

    // 8bit ↔ 16bit 変換（入出力用）
    Image16 toPremultiplied(const Image& input, double alpha = 1.0) const;
    Image fromPremultiplied(const Image16& input) const;

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

// ========================================================================
// ノードグラフ評価エンジン（C++側で完結）
// ========================================================================

// 合成ノードの入力定義
struct CompositeInput {
    std::string id;
    double alpha;

    CompositeInput() : id(""), alpha(1.0) {}
    CompositeInput(const std::string& inputId, double inputAlpha)
        : id(inputId), alpha(inputAlpha) {}
};

// ノードグラフのノード定義
struct GraphNode {
    std::string type;  // "image", "filter", "composite", "output"
    std::string id;

    // image用（新形式: imageId + alpha）
    int imageId;       // 画像ライブラリのID
    double imageAlpha; // 画像ノードのアルファ値

    // image用（旧形式: 後方互換性のため保持）
    int layerId;
    AffineParams transform;

    // filter用（独立フィルタノード）
    std::string filterType;
    float filterParam;
    bool independent;

    // filter用（レイヤー付帯フィルタノード）
    int filterLayerId;
    int filterIndex;

    // composite用
    double alpha1;  // 後方互換性のため保持（非推奨）
    double alpha2;  // 後方互換性のため保持（非推奨）
    std::vector<CompositeInput> compositeInputs;  // 動的な入力配列
    AffineParams compositeTransform;

    // affine用（アフィン変換ノード）
    bool matrixMode;           // false: パラメータモード, true: 行列モード
    AffineParams affineParams; // パラメータモード用
    AffineMatrix affineMatrix; // 行列モード用

    GraphNode() : imageId(-1), imageAlpha(1.0),
                  layerId(-1), filterParam(0.0f), independent(false),
                  filterLayerId(-1), filterIndex(-1), alpha1(1.0), alpha2(1.0),
                  matrixMode(false) {}
};

// ノードグラフの接続定義
struct GraphConnection {
    std::string fromNodeId;
    std::string fromPort;
    std::string toNodeId;
    std::string toPort;
};

// ノードグラフ評価エンジン
class NodeGraphEvaluator {
public:
    NodeGraphEvaluator(int canvasWidth, int canvasHeight);

    // レイヤー画像を登録（8bit RGBA）
    void setLayerImage(int layerId, const Image& img);

    // ノードグラフ構造を設定
    void setNodes(const std::vector<GraphNode>& nodes);
    void setConnections(const std::vector<GraphConnection>& connections);

    // ノードグラフを評価して最終画像を取得（1回のWASM呼び出しで完結）
    Image evaluateGraph();

    // キャンバスサイズ変更
    void setCanvasSize(int width, int height);

private:
    int canvasWidth;
    int canvasHeight;
    ImageProcessor processor;

    std::vector<GraphNode> nodes;
    std::vector<GraphConnection> connections;

    // レイヤー画像キャッシュ
    std::map<int, Image> layerImages;         // 元画像（8bit）
    std::map<int, Image16> layerPremulCache;  // premultiplied変換済み（16bit）

    // ノード評価結果キャッシュ（1回の評価で使い回す）
    std::map<std::string, Image16> nodeResultCache;

    // 内部評価関数（再帰的にノードを評価）
    Image16 evaluateNode(const std::string& nodeId, std::set<std::string>& visited);

    // レイヤー画像のpremultiplied変換（キャッシュ付き）
    Image16 getLayerPremultiplied(int layerId, const AffineParams& transform);
};

} // namespace ImageTransform

#endif // IMAGE_TRANSFORM_H
