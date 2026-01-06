#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include "viewport.h"
#include "filters.h"
#include "pixel_format_registry.h"
#include <string>
#include <vector>

namespace ImageTransform {

// 前方宣言
struct Image;
struct AffineMatrix;

// 画像処理エンジン（ViewPortベース）
class ImageProcessor {
public:
    ImageProcessor(int canvasWidth, int canvasHeight);

    // コア画像処理関数（ViewPortベース）
    ViewPort applyFilter(const ViewPort& input, const std::string& filterType, const std::vector<float>& params = {}) const;

    // アフィン変換
    // outputWidth/Height: 出力ViewPortサイズ（0以下の場合はcanvasサイズを使用）
    ViewPort applyTransform(const ViewPort& input, const AffineMatrix& matrix, double originX = 0, double originY = 0,
                            double outputOffsetX = 0, double outputOffsetY = 0,
                            int outputWidth = 0, int outputHeight = 0) const;

    // 画像合成
    // outputWidth/Height: 出力ViewPortサイズ（0以下の場合はcanvasサイズを使用）
    ViewPort mergeImages(const std::vector<const ViewPort*>& images, double dstOriginX, double dstOriginY,
                         int outputWidth = 0, int outputHeight = 0) const;

    // ピクセルフォーマット変換
    ViewPort convertPixelFormat(const ViewPort& input, PixelFormatID targetFormat) const;

    // キャンバスサイズ管理
    void setCanvasSize(int width, int height);

private:
    int canvasWidth;
    int canvasHeight;
};

} // namespace ImageTransform

#endif // IMAGE_PROCESSOR_H
