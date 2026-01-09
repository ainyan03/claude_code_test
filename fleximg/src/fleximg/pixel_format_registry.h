#ifndef FLEXIMG_PIXEL_FORMAT_REGISTRY_H
#define FLEXIMG_PIXEL_FORMAT_REGISTRY_H

#include "common.h"
#include "pixel_format.h"
#include <map>
#include <vector>
#include <utility>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// ピクセルフォーマットレジストリ
// ========================================================================
//
// フォーマット間の変換を管理するシングルトンクラス。
// - 組み込みフォーマット（RGBA8_Straight, RGBA16_Premultiplied等）を登録
// - ユーザー定義フォーマットの動的登録が可能
// - 標準フォーマット（RGBA8_Straight）経由の汎用変換
// - 頻出パターン用の直接変換登録が可能
//

class PixelFormatRegistry {
public:
    // シングルトンインスタンスを取得
    static PixelFormatRegistry& getInstance();

    // ========================================
    // フォーマット登録・取得
    // ========================================

    // フォーマットを登録（ユーザー定義フォーマット用）
    // 戻り値: 割り当てられたPixelFormatID
    PixelFormatID registerFormat(const PixelFormatDescriptor& descriptor);

    // フォーマット記述子を取得
    const PixelFormatDescriptor* getFormat(PixelFormatID id) const;

    // ========================================
    // フォーマット変換
    // ========================================

    // 2つのフォーマット間で変換
    // - 同一フォーマット: 単純コピー
    // - 直接変換が登録されていれば使用（最適化パス）
    // - なければ標準フォーマット（RGBA8_Straight）経由で変換
    void convert(const void* src, PixelFormatID srcFormat,
                 void* dst, PixelFormatID dstFormat,
                 int pixelCount,
                 const uint16_t* srcPalette = nullptr,
                 const uint16_t* dstPalette = nullptr);

    // ========================================
    // 直接変換の登録（最適化用）
    // ========================================

    // 直接変換関数の型
    using DirectConvertFunc = void(*)(const void* src, void* dst, int pixelCount);

    // 直接変換関数を登録
    // 登録すると、convert()で標準フォーマット経由ではなく直接変換が使用される
    void registerDirectConversion(PixelFormatID srcFormat, PixelFormatID dstFormat,
                                   DirectConvertFunc func);

    // 直接変換関数を取得（なければnullptr）
    DirectConvertFunc getDirectConversion(PixelFormatID srcFormat, PixelFormatID dstFormat) const;

private:
    PixelFormatRegistry();
    ~PixelFormatRegistry() = default;

    // コピー禁止
    PixelFormatRegistry(const PixelFormatRegistry&) = delete;
    PixelFormatRegistry& operator=(const PixelFormatRegistry&) = delete;

    // 登録されたフォーマット
    std::map<PixelFormatID, PixelFormatDescriptor> formats_;

    // 直接変換テーブル（src, dst）-> 変換関数
    std::map<std::pair<PixelFormatID, PixelFormatID>, DirectConvertFunc> directConversions_;

    // ユーザー定義フォーマット用の次のID
    PixelFormatID nextUserFormatID_;

    // 変換用の一時バッファ（標準フォーマット経由の変換に使用）
    mutable std::vector<uint8_t> conversionBuffer_;
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_PIXEL_FORMAT_REGISTRY_H
