#ifndef FLEXIMG_PIXEL_FORMAT_H
#define FLEXIMG_PIXEL_FORMAT_H

#include <cstdint>

#include "common.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// ピクセルフォーマットID
// ========================================================================

using PixelFormatID = uint32_t;

namespace PixelFormatIDs {
    // 16bit RGBA系（0x0000～0x00FF）
    constexpr PixelFormatID RGBA16_Straight       = 0x0001;
    constexpr PixelFormatID RGBA16_Premultiplied  = 0x0002;

    // パックドRGB系（0x0100～0x01FF）
    constexpr PixelFormatID RGB565_LE             = 0x0100;
    constexpr PixelFormatID RGB565_BE             = 0x0101;
    constexpr PixelFormatID RGB332                = 0x0102;
    constexpr PixelFormatID RGBA5551              = 0x0103;
    constexpr PixelFormatID RGBA4444              = 0x0104;

    // 8bit RGBA系（0x0200～0x02FF）
    constexpr PixelFormatID RGBA8_Straight        = 0x0200;
    constexpr PixelFormatID RGBA8_Premultiplied   = 0x0201;

    // グレースケール系（0x0300～0x03FF）
    constexpr PixelFormatID Grayscale8            = 0x0300;
    constexpr PixelFormatID Grayscale16           = 0x0301;
    constexpr PixelFormatID Gray3bit              = 0x0302;

    // モノクロ系（0x0400～0x04FF）
    constexpr PixelFormatID Mono1bit_MSB          = 0x0400;
    constexpr PixelFormatID Mono1bit_LSB          = 0x0401;
    constexpr PixelFormatID Mono2bit              = 0x0402;
    constexpr PixelFormatID Mono4bit              = 0x0403;

    // インデックスカラー系（0x0500～0x05FF）
    constexpr PixelFormatID Indexed4bit           = 0x0500;
    constexpr PixelFormatID Indexed8bit           = 0x0501;

    // ユーザー定義フォーマット用の範囲（0x10000000～）
    constexpr PixelFormatID USER_DEFINED_BASE     = 0x10000000;
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
        : bits(b), shift(s), mask(b > 0 ? ((1u << b) - 1) << s : 0) {}
};

// ========================================================================
// ピクセルフォーマット記述子
// ========================================================================

struct PixelFormatDescriptor {
    PixelFormatID id;
    const char* name;

    // 基本情報
    uint8_t bitsPerPixel;       // ピクセルあたりのビット数（1,2,3,4,8,16,24,32等）
    uint8_t pixelsPerUnit;      // 1ユニットあたりのピクセル数
    uint8_t bytesPerUnit;       // 1ユニットあたりのバイト数
    // 制約: bitsPerPixel * pixelsPerUnit == bytesPerUnit * 8

    // チャンネル情報（ダイレクトカラーの場合）
    ChannelDescriptor channels[4];  // R, G, B, A の順

    // アルファ情報
    bool hasAlpha;
    bool isPremultiplied;

    // パレット情報（インデックスカラーの場合）
    bool isIndexed;
    uint16_t maxPaletteSize;   // 最大パレットサイズ（2^bitsPerPixel）

    // エンディアン情報
    BitOrder bitOrder;      // ビット順序（bit-packed形式で使用）
    ByteOrder byteOrder;    // バイト順序（multi-byte pixel形式で使用）

    // 変換関数の型定義
    // 標準フォーマット（RGBA8_Straight）との相互変換
    using ToStandardFunc = void(*)(const void* src, uint8_t* dst, int pixelCount);
    using FromStandardFunc = void(*)(const uint8_t* src, void* dst, int pixelCount);
    using ToStandardIndexedFunc = void(*)(const void* src, uint8_t* dst, int pixelCount, const uint16_t* palette);
    using FromStandardIndexedFunc = void(*)(const uint8_t* src, void* dst, int pixelCount, const uint16_t* palette);

    // 変換関数ポインタ（インデックスカラーかどうかで使い分け）
    union {
        ToStandardFunc toStandard;              // ダイレクトカラー用
        ToStandardIndexedFunc toStandardIndexed; // インデックスカラー用
    };

    union {
        FromStandardFunc fromStandard;
        FromStandardIndexedFunc fromStandardIndexed;
    };

    // デフォルトコンストラクタ
    PixelFormatDescriptor()
        : id(0), name(""), bitsPerPixel(0), pixelsPerUnit(1), bytesPerUnit(0),
          channels{}, hasAlpha(false), isPremultiplied(false),
          isIndexed(false), maxPaletteSize(0),
          bitOrder(BitOrder::MSBFirst), byteOrder(ByteOrder::Native),
          toStandard(nullptr) {}
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_PIXEL_FORMAT_H
