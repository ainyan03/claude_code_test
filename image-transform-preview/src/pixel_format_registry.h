#ifndef PIXEL_FORMAT_REGISTRY_H
#define PIXEL_FORMAT_REGISTRY_H

#include "pixel_format.h"
#include <map>
#include <vector>

namespace ImageTransform {

// ========================================================================
// ピクセルフォーマットレジストリ
// ========================================================================

class PixelFormatRegistry {
public:
    // シングルトンパターン
    static PixelFormatRegistry& getInstance();

    // フォーマットを登録（ユーザー定義フォーマット用）
    // 戻り値: 割り当てられたPixelFormatID
    PixelFormatID registerFormat(const PixelFormatDescriptor& descriptor);

    // フォーマット記述子を取得
    const PixelFormatDescriptor* getFormat(PixelFormatID id) const;

    // 2つのフォーマット間で変換
    // パレットが必要な場合は srcPalette, dstPalette を指定
    void convert(const void* src, PixelFormatID srcFormat,
                 void* dst, PixelFormatID dstFormat,
                 int pixelCount,
                 const uint16_t* srcPalette = nullptr,
                 const uint16_t* dstPalette = nullptr);

private:
    PixelFormatRegistry();
    ~PixelFormatRegistry() = default;

    // コピー禁止
    PixelFormatRegistry(const PixelFormatRegistry&) = delete;
    PixelFormatRegistry& operator=(const PixelFormatRegistry&) = delete;

    // 登録されたフォーマット
    std::map<PixelFormatID, PixelFormatDescriptor> formats_;

    // ユーザー定義フォーマット用の次のID
    PixelFormatID nextUserFormatID_;

    // 変換用の一時バッファ（標準フォーマット経由の変換に使用）
    mutable std::vector<uint16_t> conversionBuffer_;
};

} // namespace ImageTransform

#endif // PIXEL_FORMAT_REGISTRY_H
