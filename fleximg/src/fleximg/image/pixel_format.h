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

#ifdef FLEXIMG_ENABLE_PREMUL
// RGBA16_Premultiplied用アルファ閾値
namespace RGBA16Premul {
    constexpr uint16_t ALPHA_TRANSPARENT_MAX = 255;
    constexpr uint16_t ALPHA_OPAQUE_MIN = 65280;

    inline constexpr bool isTransparent(uint16_t a) { return a <= ALPHA_TRANSPARENT_MAX; }
    inline constexpr bool isOpaque(uint16_t a) { return a >= ALPHA_OPAQUE_MIN; }
}
#endif

// ========================================================================
// 変換パラメータ
// ========================================================================

struct ConvertParams {
    uint32_t colorKey = 0;          // 透過カラー（4 bytes）
    uint8_t alphaMultiplier = 255;  // アルファ係数（1 byte）
    bool useColorKey = false;       // カラーキー有効フラグ（1 byte）

    // デフォルトコンストラクタ
    constexpr ConvertParams() = default;

    // アルファ係数指定
    constexpr explicit ConvertParams(uint8_t alpha)
        : alphaMultiplier(alpha) {}

    // カラーキー指定
    constexpr ConvertParams(uint32_t key, bool use)
        : colorKey(key), useColorKey(use) {}
};

// 後方互換性のためBlendParamsをエイリアスとして残す
using BlendParams = ConvertParams;

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
// チャンネル種別
// ========================================================================

enum class ChannelType : uint8_t {
    Unused = 0,      // チャンネルなし
    Red,             // 赤チャンネル
    Green,           // 緑チャンネル
    Blue,            // 青チャンネル
    Alpha,           // アルファチャンネル
    Luminance,       // 輝度（グレースケール）
    Index            // パレットインデックス
};

// ========================================================================
// チャンネル記述子
// ========================================================================

struct ChannelDescriptor {
    ChannelType type;   // チャンネル種別
    uint8_t bits;       // ビット数（0なら存在しない）
    uint8_t shift;      // ビットシフト量
    uint16_t mask;      // ビットマスク

    // デフォルトコンストラクタ（Unusedチャンネル）
    constexpr ChannelDescriptor()
        : type(ChannelType::Unused), bits(0), shift(0), mask(0) {}

    // 旧コンストラクタ（後方互換性維持、typeはUnusedに設定）
    constexpr ChannelDescriptor(uint8_t b, uint8_t s)
        : type(ChannelType::Unused), bits(b), shift(s)
        , mask(b > 0 ? static_cast<uint16_t>(((1u << b) - 1) << s) : 0) {}

    // 新コンストラクタ（チャンネル種別を指定）
    constexpr ChannelDescriptor(ChannelType t, uint8_t b, uint8_t s = 0)
        : type(t), bits(b), shift(s)
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
    uint8_t channelCount;           // チャンネル総数（Phase 2で追加、Phase 4で使用開始）
    ChannelDescriptor channels[4];  // R, G, B, A の順（Phase 5で削除予定）

    // アルファ情報
    bool hasAlpha;
    bool isPremultiplied;

    // パレット情報（インデックスカラーの場合）
    bool isIndexed;
    uint16_t maxPaletteSize;

    // エンディアン情報
    BitOrder bitOrder;
    ByteOrder byteOrder;

    // ========================================================================
    // 変換関数の型定義
    // ========================================================================
    // 統一シグネチャ: void(*)(void* dst, const void* src, int pixelCount, const ConvertParams* params)

    // Straight形式（RGBA8_Straight）との相互変換
    using ConvertFunc = void(*)(void* dst, const void* src, int pixelCount, const ConvertParams* params);
    using ToStraightFunc = ConvertFunc;
    using FromStraightFunc = ConvertFunc;

    // インデックスカラー用（パレット引数が必要なため別シグネチャ）
    using ToStraightIndexedFunc = void(*)(void* dst, const void* src, int pixelCount, const uint16_t* palette);
    using FromStraightIndexedFunc = void(*)(void* dst, const void* src, int pixelCount, const uint16_t* palette);

    // 変換関数ポインタ（ダイレクトカラー用）
    ToStraightFunc toStraight;
    FromStraightFunc fromStraight;

    // 変換関数ポインタ（インデックスカラー用）
    ToStraightIndexedFunc toStraightIndexed;
    FromStraightIndexedFunc fromStraightIndexed;

    // ========================================================================
    // Premul形式（RGBA16_Premultiplied）との変換・ブレンド関数
    // ========================================================================

    // 関数型定義（統一シグネチャ）
    // ToPremulFunc: このフォーマットのsrcからPremul形式のdstへ変換コピー
    using ToPremulFunc = ConvertFunc;

    // FromPremulFunc: Premul形式のsrcからこのフォーマットのdstへ変換コピー
    using FromPremulFunc = ConvertFunc;

    // BlendUnderPremulFunc: srcフォーマットからPremul形式のdstへunder合成
    //   - dst が不透明なら何もしない（スキップ）
    //   - dst が透明なら単純変換コピー（toPremul相当）
    //   - dst が半透明ならunder合成
    using BlendUnderPremulFunc = ConvertFunc;

    // BlendUnderStraightFunc: srcフォーマットからStraight形式(RGBA8)のdstへunder合成
    //   - dst が不透明なら何もしない（スキップ）
    //   - dst が透明なら単純コピー
    //   - dst が半透明ならunder合成（unpremultiply含む）
    using BlendUnderStraightFunc = ConvertFunc;

    // SwapEndianFunc: エンディアン違いの兄弟フォーマットとの変換
    using SwapEndianFunc = ConvertFunc;

    // 関数ポインタ（Premul形式用、未実装の場合は nullptr）
    ToPremulFunc toPremul;
    FromPremulFunc fromPremul;
    BlendUnderPremulFunc blendUnderPremul;

    // 関数ポインタ（Straight形式用、未実装の場合は nullptr）
    BlendUnderStraightFunc blendUnderStraight;

    // エンディアン変換（兄弟フォーマットがある場合）
    const PixelFormatDescriptor* siblingEndian;  // エンディアン違いの兄弟（なければnullptr）
    SwapEndianFunc swapEndian;                   // バイトスワップ関数

    // ========================================================================
    // チャンネルアクセスメソッド（Phase 2で追加）
    // ========================================================================

    // 指定インデックスのチャンネル記述子を取得
    // index >= channelCount の場合はUnusedチャンネルを返す
    constexpr ChannelDescriptor getChannel(uint8_t index) const {
        return (index < channelCount) ? channels[index] : ChannelDescriptor();
    }

    // 指定タイプのチャンネルインデックスを取得
    // 見つからない場合は-1を返す
    constexpr int8_t getChannelIndex(ChannelType type) const {
        for (uint8_t i = 0; i < channelCount; ++i) {
            if (channels[i].type == type) {
                return static_cast<int8_t>(i);
            }
        }
        return -1;
    }

    // 指定タイプのチャンネルを持つか判定
    constexpr bool hasChannelType(ChannelType type) const {
        return getChannelIndex(type) >= 0;
    }

    // 指定タイプのチャンネル記述子を取得
    // 見つからない場合はUnusedチャンネルを返す
    constexpr ChannelDescriptor getChannelByType(ChannelType type) const {
        int8_t idx = getChannelIndex(type);
        return (idx >= 0) ? channels[idx] : ChannelDescriptor();
    }
};

} // namespace FLEXIMG_NAMESPACE

// ========================================================================
// 各ピクセルフォーマット（個別ヘッダ）
// ========================================================================

#include "pixel_format/rgba8_straight.h"
#include "pixel_format/alpha8.h"
#include "pixel_format/rgba16_premul.h"
#include "pixel_format/rgb565.h"
#include "pixel_format/rgb332.h"
#include "pixel_format/rgb888.h"

namespace FLEXIMG_NAMESPACE {

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
#ifdef FLEXIMG_ENABLE_PREMUL
    PixelFormatIDs::RGBA16_Premultiplied,
#endif
    PixelFormatIDs::RGBA8_Straight,
    PixelFormatIDs::RGB565_LE,
    PixelFormatIDs::RGB565_BE,
    PixelFormatIDs::RGB332,
    PixelFormatIDs::RGB888,
    PixelFormatIDs::BGR888,
    PixelFormatIDs::Alpha8,
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
// - エンディアン違いの兄弟: swapEndian
// - Premul形式との直接変換（toPremul/fromPremul）
// - それ以外はStraight形式（RGBA8_Straight）経由で変換
inline void convertFormat(const void* src, PixelFormatID srcFormat,
                          void* dst, PixelFormatID dstFormat,
                          int pixelCount,
                          const ConvertParams* params = nullptr,
                          const uint16_t* srcPalette = nullptr,
                          const uint16_t* dstPalette = nullptr) {
    // 同じフォーマットの場合はコピー
    if (srcFormat == dstFormat) {
        if (srcFormat) {
            size_t units = static_cast<size_t>((pixelCount + srcFormat->pixelsPerUnit - 1) / srcFormat->pixelsPerUnit);
            std::memcpy(dst, src, units * srcFormat->bytesPerUnit);
        }
        return;
    }

    if (!srcFormat || !dstFormat) return;

    // エンディアン違いの兄弟フォーマット → swapEndian
    if (srcFormat->siblingEndian == dstFormat && srcFormat->swapEndian) {
        srcFormat->swapEndian(dst, src, pixelCount, params);
        return;
    }

#ifdef FLEXIMG_ENABLE_PREMUL
    // Premul形式への直接変換（toPremul）
    if (dstFormat == PixelFormatIDs::RGBA16_Premultiplied && srcFormat->toPremul) {
        srcFormat->toPremul(dst, src, pixelCount, params);
        return;
    }

    // Premul形式からの直接変換（fromPremul）
    if (srcFormat == PixelFormatIDs::RGBA16_Premultiplied && dstFormat->fromPremul) {
        dstFormat->fromPremul(dst, src, pixelCount, params);
        return;
    }
#endif

    // Straight形式（RGBA8_Straight）経由で変換
    // 一時バッファを確保（スレッドローカル）
    thread_local std::vector<uint8_t> conversionBuffer;
    conversionBuffer.resize(static_cast<size_t>(pixelCount) * 4);

    // src → RGBA8_Straight
    if (srcFormat->isIndexed && srcFormat->toStraightIndexed && srcPalette) {
        srcFormat->toStraightIndexed(conversionBuffer.data(), src, pixelCount, srcPalette);
    } else if (!srcFormat->isIndexed && srcFormat->toStraight) {
        srcFormat->toStraight(conversionBuffer.data(), src, pixelCount, params);
    }

    // RGBA8_Straight → dst
    if (dstFormat->isIndexed && dstFormat->fromStraightIndexed && dstPalette) {
        dstFormat->fromStraightIndexed(dst, conversionBuffer.data(), pixelCount, dstPalette);
    } else if (!dstFormat->isIndexed && dstFormat->fromStraight) {
        dstFormat->fromStraight(dst, conversionBuffer.data(), pixelCount, params);
    }
}

} // namespace FLEXIMG_NAMESPACE


#endif // FLEXIMG_PIXEL_FORMAT_H
