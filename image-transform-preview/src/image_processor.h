#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include "image_types.h"
#include "filters.h"
#include "pixel_format_registry.h"
#include <string>
#include <vector>

namespace ImageTransform {

// 画像処理エンジン（コア16bit処理機能）
class ImageProcessor {
public:
    ImageProcessor(int canvasWidth, int canvasHeight);

    // コア画像処理関数（16bit premultiplied alpha処理）
    Image16 applyFilterToImage16(const Image16& input, const std::string& filterType, float param = 0.0f) const;
    Image16 applyTransformToImage16(const Image16& input, const AffineMatrix& matrix, double alpha = 1.0) const;
    Image16 mergeImages16(const std::vector<const Image16*>& images) const;

    // 8bit ↔ 16bit 変換（入出力用）
    Image16 toPremultiplied(const Image& input, double alpha = 1.0) const;
    Image fromPremultiplied(const Image16& input) const;

    // ★新規（Phase 3）: ピクセルフォーマット変換
    Image16 convertPixelFormat(const Image16& input, PixelFormatID targetFormat) const;

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
