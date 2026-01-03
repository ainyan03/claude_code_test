#ifndef IMAGE_TYPES_H
#define IMAGE_TYPES_H

#include <vector>
#include <cstdint>
#include <cmath>
#include "pixel_format.h"

namespace ImageTransform {

// 旧PixelFormat定義（後方互換性のため維持、将来削除予定）
enum class PixelFormat {
    Straight,
    Premultiplied
};

// 画像データ構造（8bit RGBA、ストレートアルファ）
struct Image {
    std::vector<uint8_t> data;  // RGBA format
    int width;
    int height;

    Image() : width(0), height(0) {}
    Image(int w, int h) : width(w), height(h), data(w * h * 4, 0) {}
};

// 16bit プリマルチプライドアルファ画像（内部処理用）
struct Image16 {
    std::vector<uint16_t> data;  // RGBA, 16bit per channel
    int width;
    int height;

    // ★新規: 新しいフォーマットシステム（Phase 2）
    PixelFormatID formatID;

    // ★旧フィールド（deprecated、後方互換性のため維持、将来削除予定）
    PixelFormat format;

    // デフォルトコンストラクタ
    Image16()
        : width(0), height(0),
          formatID(PixelFormatIDs::RGBA16_Premultiplied),
          format(PixelFormat::Premultiplied) {}

    // ★既存のコンストラクタ（後方互換性維持）
    // デフォルト引数でPremultipliedを使用
    Image16(int w, int h, PixelFormat fmt = PixelFormat::Premultiplied)
        : width(w), height(h), data(w * h * 4, 0),
          formatID(fmt == PixelFormat::Straight ?
                   PixelFormatIDs::RGBA16_Straight :
                   PixelFormatIDs::RGBA16_Premultiplied),
          format(fmt) {}

    // ★新規: フォーマットIDを指定するコンストラクタ（Phase 2以降で使用）
    Image16(int w, int h, PixelFormatID fmtID)
        : width(w), height(h), data(w * h * 4, 0),
          formatID(fmtID),
          format(fmtID == PixelFormatIDs::RGBA16_Straight ?
                 PixelFormat::Straight : PixelFormat::Premultiplied) {}
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

} // namespace ImageTransform

#endif // IMAGE_TYPES_H
