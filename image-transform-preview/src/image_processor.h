#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include "viewport.h"
#include "filters.h"
#include "pixel_format_registry.h"
#include <string>
#include <vector>

namespace ImageTransform {

// 前方宣言（後方互換性）
struct Image;
struct AffineMatrix;

// 画像処理エンジン（ViewPortベース）
class ImageProcessor {
public:
    ImageProcessor(int canvasWidth, int canvasHeight);

    // コア画像処理関数（ViewPortベース）
    ViewPort applyFilter(const ViewPort& input, const std::string& filterType, float param = 0.0f) const;
    ViewPort applyTransform(const ViewPort& input, const AffineMatrix& matrix) const;
    ViewPort mergeImages(const std::vector<const ViewPort*>& images) const;

    // 8bit Image ↔ ViewPort 変換（入出力用、純粋な型変換）
    ViewPort fromImage(const Image& input) const;
    Image toImage(const ViewPort& input) const;

    // ピクセルフォーマット変換
    ViewPort convertPixelFormat(const ViewPort& input, PixelFormatID targetFormat) const;

    // キャンバスサイズ管理
    void setCanvasSize(int width, int height);
    int getCanvasWidth() const { return canvasWidth; }
    int getCanvasHeight() const { return canvasHeight; }

private:
    int canvasWidth;
    int canvasHeight;
};

} // namespace ImageTransform

#endif // IMAGE_PROCESSOR_H
