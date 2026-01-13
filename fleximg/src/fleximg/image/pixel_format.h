#ifndef FLEXIMG_PIXEL_FORMAT_H
#define FLEXIMG_PIXEL_FORMAT_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include "../core/common.h"

namespace FLEXIMG_NAMESPACE {

// 前方宣言
struct PixelFormatDescriptor;

// ========================================================================
// ピクセルフォーマットID（Descriptor ポインタ）
// ========================================================================

using PixelFormatID = const PixelFormatDescriptor*;

// RGBA16_Premultiplied用アルファ閾値
namespace RGBA16Premul {
    constexpr uint16_t ALPHA_TRANSPARENT_MAX = 255;
    constexpr uint16_t ALPHA_OPAQUE_MIN = 65280;

    inline constexpr bool isTransparent(uint16_t a) { return a <= ALPHA_TRANSPARENT_MAX; }
    inline constexpr bool isOpaque(uint16_t a) { return a >= ALPHA_OPAQUE_MIN; }
}

// ========================================================================
// エンディアン情報
// ========================================================================

// ビット順序（bit-packed形式用）
enum class BitOrder {
    MSBFirst,  // 最上位ビットが先（例: 1bit bitmap）
    LSBFirst   // 最下位ビットが先
};

// バイト順序（multi-byte形式用）
enum class ByteOrder {
    BigEndian,     // ビッグエンディアン（ネットワークバイトオーダー）
    LittleEndian,  // リトルエンディアン（x86等）
    Native         // ネイティブ（プラットフォーム依存）
};

// ========================================================================
// チャンネル記述子
// ========================================================================

struct ChannelDescriptor {
    uint8_t bits;       // ビット数（0なら存在しない）
    uint8_t shift;      // ビットシフト量
    uint16_t mask;      // ビットマスク

    ChannelDescriptor(uint8_t b = 0, uint8_t s = 0)
        : bits(b), shift(s)
        , mask(b > 0 ? static_cast<uint16_t>(((1u << b) - 1) << s) : 0) {}
};

// ========================================================================
// ピクセルフォーマット記述子
// ========================================================================

struct PixelFormatDescriptor {
    const char* name;

    // 基本情報
    uint8_t bitsPerPixel;       // ピクセルあたりのビット数
    uint8_t pixelsPerUnit;      // 1ユニットあたりのピクセル数
    uint8_t bytesPerUnit;       // 1ユニットあたりのバイト数

    // チャンネル情報（ダイレクトカラーの場合）
    ChannelDescriptor channels[4];  // R, G, B, A の順

    // アルファ情報
    bool hasAlpha;
    bool isPremultiplied;

    // パレット情報（インデックスカラーの場合）
    bool isIndexed;
    uint16_t maxPaletteSize;

    // エンディアン情報
    BitOrder bitOrder;
    ByteOrder byteOrder;

    // 変換関数の型定義（標準フォーマット RGBA8_Straight との相互変換）
    using ToStandardFunc = void(*)(const void* src, uint8_t* dst, int pixelCount);
    using FromStandardFunc = void(*)(const uint8_t* src, void* dst, int pixelCount);
    using ToStandardIndexedFunc = void(*)(const void* src, uint8_t* dst, int pixelCount, const uint16_t* palette);
    using FromStandardIndexedFunc = void(*)(const uint8_t* src, void* dst, int pixelCount, const uint16_t* palette);

    // 変換関数ポインタ（ダイレクトカラー用）
    ToStandardFunc toStandard;
    FromStandardFunc fromStandard;

    // 変換関数ポインタ（インデックスカラー用）
    ToStandardIndexedFunc toStandardIndexed;
    FromStandardIndexedFunc fromStandardIndexed;
};

// ========================================================================
// 組み込みフォーマット（extern 宣言）
// 実体は pixel_format_registry.cpp で定義
// ========================================================================

namespace BuiltinFormats {
    extern const PixelFormatDescriptor RGBA16_Premultiplied;
    extern const PixelFormatDescriptor RGBA8_Straight;
    extern const PixelFormatDescriptor RGB565_LE;
    extern const PixelFormatDescriptor RGB565_BE;
    extern const PixelFormatDescriptor RGB332;
    extern const PixelFormatDescriptor RGB888;
    extern const PixelFormatDescriptor BGR888;
}

// ========================================================================
// ピクセルフォーマットID定数
// ========================================================================

namespace PixelFormatIDs {
    // 16bit RGBA系
    inline const PixelFormatID RGBA16_Premultiplied  = &BuiltinFormats::RGBA16_Premultiplied;

    // 8bit RGBA系
    inline const PixelFormatID RGBA8_Straight        = &BuiltinFormats::RGBA8_Straight;

    // パックドRGB系
    inline const PixelFormatID RGB565_LE             = &BuiltinFormats::RGB565_LE;
    inline const PixelFormatID RGB565_BE             = &BuiltinFormats::RGB565_BE;
    inline const PixelFormatID RGB332                = &BuiltinFormats::RGB332;

    // 24bit RGB系
    inline const PixelFormatID RGB888                = &BuiltinFormats::RGB888;
    inline const PixelFormatID BGR888                = &BuiltinFormats::BGR888;
}

// ========================================================================
// 直接変換テーブル
// ========================================================================

// 直接変換関数の型
using DirectConvertFunc = void(*)(const void* src, void* dst, int pixelCount);

// 直接変換エントリ
struct DirectConversion {
    PixelFormatID from;
    PixelFormatID to;
    DirectConvertFunc convert;
};

// 直接変換関数（実体は pixel_format_registry.cpp で定義）
namespace DirectConvertFuncs {
    extern void rgba16PremulToRgba8Straight(const void* src, void* dst, int pixelCount);
    extern void rgba8StraightToRgba16Premul(const void* src, void* dst, int pixelCount);
}

// 直接変換テーブル（線形検索用）
inline const DirectConversion directConversions[] = {
    { PixelFormatIDs::RGBA16_Premultiplied, PixelFormatIDs::RGBA8_Straight,
      DirectConvertFuncs::rgba16PremulToRgba8Straight },
    { PixelFormatIDs::RGBA8_Straight, PixelFormatIDs::RGBA16_Premultiplied,
      DirectConvertFuncs::rgba8StraightToRgba16Premul },
};

inline constexpr size_t directConversionsCount = sizeof(directConversions) / sizeof(directConversions[0]);

// 直接変換関数を取得（なければ nullptr）
inline DirectConvertFunc getDirectConversion(PixelFormatID from, PixelFormatID to) {
    for (size_t i = 0; i < directConversionsCount; ++i) {
        if (directConversions[i].from == from && directConversions[i].to == to) {
            return directConversions[i].convert;
        }
    }
    return nullptr;
}

// ========================================================================
// ユーティリティ関数
// ========================================================================

// ピクセルあたりのバイト数を取得
inline int_fast8_t getBytesPerPixel(PixelFormatID formatID) {
    if (formatID) {
        return static_cast<int_fast8_t>((formatID->bitsPerPixel + 7) / 8);
    }
    return 4;  // フォールバック
}

// 組み込みフォーマット一覧（名前検索用）
inline const PixelFormatID builtinFormats[] = {
    PixelFormatIDs::RGBA16_Premultiplied,
    PixelFormatIDs::RGBA8_Straight,
    PixelFormatIDs::RGB565_LE,
    PixelFormatIDs::RGB565_BE,
    PixelFormatIDs::RGB332,
    PixelFormatIDs::RGB888,
    PixelFormatIDs::BGR888,
};

inline constexpr size_t builtinFormatsCount = sizeof(builtinFormats) / sizeof(builtinFormats[0]);

// 名前からフォーマットを取得（見つからなければ nullptr）
inline PixelFormatID getFormatByName(const char* name) {
    if (!name) return nullptr;
    for (size_t i = 0; i < builtinFormatsCount; ++i) {
        if (std::strcmp(builtinFormats[i]->name, name) == 0) {
            return builtinFormats[i];
        }
    }
    return nullptr;
}

// フォーマット名を取得
inline const char* getFormatName(PixelFormatID formatID) {
    return formatID ? formatID->name : "unknown";
}

// ========================================================================
// フォーマット変換
// ========================================================================

// 2つのフォーマット間で変換
// - 同一フォーマット: 単純コピー
// - 直接変換があれば使用（最適化パス）
// - なければ標準フォーマット（RGBA8_Straight）経由で変換
inline void convertFormat(const void* src, PixelFormatID srcFormat,
                          void* dst, PixelFormatID dstFormat,
                          int pixelCount,
                          const uint16_t* srcPalette = nullptr,
                          const uint16_t* dstPalette = nullptr) {
    // 同じフォーマットの場合はコピー
    if (srcFormat == dstFormat) {
        if (srcFormat) {
            int units = (pixelCount + srcFormat->pixelsPerUnit - 1) / srcFormat->pixelsPerUnit;
            std::memcpy(dst, src, units * srcFormat->bytesPerUnit);
        }
        return;
    }

    // 直接変換があれば使用（最適化パス）
    DirectConvertFunc directFunc = getDirectConversion(srcFormat, dstFormat);
    if (directFunc) {
        directFunc(src, dst, pixelCount);
        return;
    }

    // 標準フォーマット（RGBA8_Straight）経由で変換
    if (!srcFormat || !dstFormat) return;

    // 一時バッファを確保（スレッドローカル）
    thread_local std::vector<uint8_t> conversionBuffer;
    conversionBuffer.resize(pixelCount * 4);

    // src → RGBA8_Straight
    if (srcFormat->isIndexed && srcFormat->toStandardIndexed && srcPalette) {
        srcFormat->toStandardIndexed(src, conversionBuffer.data(), pixelCount, srcPalette);
    } else if (!srcFormat->isIndexed && srcFormat->toStandard) {
        srcFormat->toStandard(src, conversionBuffer.data(), pixelCount);
    }

    // RGBA8_Straight → dst
    if (dstFormat->isIndexed && dstFormat->fromStandardIndexed && dstPalette) {
        dstFormat->fromStandardIndexed(conversionBuffer.data(), dst, pixelCount, dstPalette);
    } else if (!dstFormat->isIndexed && dstFormat->fromStandard) {
        dstFormat->fromStandard(conversionBuffer.data(), dst, pixelCount);
    }
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_PIXEL_FORMAT_H
