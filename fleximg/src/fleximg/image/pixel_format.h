#ifndef FLEXIMG_PIXEL_FORMAT_H
#define FLEXIMG_PIXEL_FORMAT_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "../core/common.h"
#include "../core/types.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// DDA転写パラメータ
// ========================================================================
//
// copyRowDDA 関数に渡すパラメータ構造体。
// アフィン変換等でのピクセルサンプリングに使用する。
//

struct DDAParam {
    int32_t srcStride;    // ソースのストライド（バイト数）
    int_fixed srcX;       // ソース開始X座標（Q16.16固定小数点）
    int_fixed srcY;       // ソース開始Y座標（Q16.16固定小数点）
    int_fixed incrX;      // 1ピクセルあたりのX増分（Q16.16固定小数点）
    int_fixed incrY;      // 1ピクセルあたりのY増分（Q16.16固定小数点）
};

// DDA行転写関数の型定義
// dst: 出力先バッファ
// srcData: ソースデータ先頭
// count: 転写ピクセル数
// param: DDAパラメータ（const、関数内でローカルコピーして使用）
using CopyRowDDA_Func = void(*)(
    uint8_t* dst,
    const uint8_t* srcData,
    int count,
    const DDAParam* param
);

// BPP別 DDA転写関数（前方宣言）
// 実装は viewport.h の FLEXIMG_IMPLEMENTATION 部で提供
void copyRowDDA_1bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param);
void copyRowDDA_2bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param);
void copyRowDDA_3bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param);
void copyRowDDA_4bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param);

// 前方宣言
struct PixelFormatDescriptor;

// ========================================================================
// ピクセルフォーマットID（Descriptor ポインタ）
// ========================================================================

using PixelFormatID = const PixelFormatDescriptor*;

// ========================================================================
// 変換パラメータ / 補助情報
// ========================================================================

struct PixelAuxInfo {
    // パレット情報（インデックスフォーマット用）
    const void* palette = nullptr;           // パレットデータポインタ（非所有）
    PixelFormatID paletteFormat = nullptr;   // パレットエントリのフォーマット
    uint16_t paletteColorCount = 0;          // パレットエントリ数

    uint8_t alphaMultiplier = 255;  // アルファ係数（1 byte）
    bool useColorKey = false;       // カラーキー有効フラグ（1 byte）
    uint32_t colorKey = 0;          // 透過カラー（4 bytes）

    // デフォルトコンストラクタ
    constexpr PixelAuxInfo() = default;

    // アルファ係数指定
    constexpr explicit PixelAuxInfo(uint8_t alpha)
        : alphaMultiplier(alpha) {}

    // カラーキー指定
    constexpr PixelAuxInfo(uint32_t key, bool use)
        : useColorKey(use), colorKey(key) {}
};

// 後方互換性のためエイリアスを残す
using ConvertParams = PixelAuxInfo;
using BlendParams = PixelAuxInfo;

// ========================================================================
// パレットデータ参照（軽量構造体）
// ========================================================================
//
// パレット情報の外部受け渡し用。ViewPortと同様の軽量参照型（非所有）。
// SourceNode::setSource() 等の外部APIで使用。
//

struct PaletteData {
    const void* data = nullptr;        // パレットデータ（非所有）
    PixelFormatID format = nullptr;    // 各エントリのフォーマット
    uint16_t colorCount = 0;           // エントリ数

    constexpr PaletteData() = default;
    constexpr PaletteData(const void* d, PixelFormatID f, uint16_t c)
        : data(d), format(f), colorCount(c) {}

    explicit operator bool() const { return data != nullptr; }
};

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

    // パレット情報（インデックスカラーの場合）
    bool isIndexed;
    uint16_t maxPaletteSize;

    // エンディアン情報
    BitOrder bitOrder;
    ByteOrder byteOrder;

    // ========================================================================
    // 変換関数の型定義
    // ========================================================================
    // 統一シグネチャ: void(*)(void* dst, const void* src, int pixelCount, const PixelAuxInfo* aux)

    // Straight形式（RGBA8_Straight）との相互変換
    using ConvertFunc = void(*)(void* dst, const void* src, int pixelCount, const PixelAuxInfo* aux);
    using ToStraightFunc = ConvertFunc;
    using FromStraightFunc = ConvertFunc;

    // 変換関数ポインタ（ダイレクトカラー用）
    ToStraightFunc toStraight;
    FromStraightFunc fromStraight;

    // インデックス展開関数（インデックス値 → パレットフォーマットのピクセルデータ）
    // aux->palette, aux->paletteFormat を参照してインデックスをパレットエントリに展開
    // 出力はパレットフォーマット（RGBA8とは限らない）
    using ExpandIndexFunc = ConvertFunc;
    ExpandIndexFunc expandIndex;   // 非インデックスフォーマットでは nullptr

    // BlendUnderStraightFunc: srcフォーマットからStraight形式(RGBA8)のdstへunder合成
    //   - dst が不透明なら何もしない（スキップ）
    //   - dst が透明なら単純コピー
    //   - dst が半透明ならunder合成（unpremultiply含む）
    using BlendUnderStraightFunc = ConvertFunc;

    // SwapEndianFunc: エンディアン違いの兄弟フォーマットとの変換
    using SwapEndianFunc = ConvertFunc;

    // 関数ポインタ（Straight形式用、未実装の場合は nullptr）
    BlendUnderStraightFunc blendUnderStraight;

    // エンディアン変換（兄弟フォーマットがある場合）
    const PixelFormatDescriptor* siblingEndian;  // エンディアン違いの兄弟（なければnullptr）
    SwapEndianFunc swapEndian;                   // バイトスワップ関数

    // DDA転写関数
    CopyRowDDA_Func copyRowDDA;                  // DDA方式の行転写（nullptrなら未対応）

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

// ========================================================================
// 内部ヘルパー関数（ピクセルフォーマット実装用）
// ========================================================================

namespace pixel_format {
namespace detail {

// 8bit LUT → Nbit 変換（4ピクセル単位展開）
// T = uint32_t: rgb332_toStraight, index8_expandIndex (bpc==4) 等で共用
// T = uint16_t: index8_expandIndex (bpc==2) 等で共用
template<typename T>
void lut8toN(T* d, const uint8_t* s, int pixelCount, const T* lut);

// 便利エイリアス
inline void lut8to32(uint32_t* d, const uint8_t* s, int pixelCount, const uint32_t* lut) {
    lut8toN(d, s, pixelCount, lut);
}
inline void lut8to16(uint16_t* d, const uint8_t* s, int pixelCount, const uint16_t* lut) {
    lut8toN(d, s, pixelCount, lut);
}

} // namespace detail
} // namespace pixel_format

} // namespace FLEXIMG_NAMESPACE

// ------------------------------------------------------------------------
// 内部ヘルパー関数（実装部）
// ------------------------------------------------------------------------
#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {
namespace pixel_format {
namespace detail {

template<typename T>
void lut8toN(T* d, const uint8_t* s, int pixelCount, const T* lut) {
    if (pixelCount & 1) {
        auto v0 = s[0];
        ++s;
        d[0] = lut[v0];
        ++d;
    }
    if (pixelCount & 2) {
        auto v0 = s[0];
        auto v1 = s[1];
        s += 2;
        auto l0 = lut[v0];
        auto l1 = lut[v1];
        d[0] = l0;
        d[1] = l1;
        d += 2;
    }
    pixelCount >>= 2;
    if (pixelCount == 0) return;
    do {
        auto v0 = s[0];
        auto v1 = s[1];
        auto v2 = s[2];
        auto v3 = s[3];
        s += 4;
        auto l0 = lut[v0];
        auto l1 = lut[v1];
        auto l2 = lut[v2];
        auto l3 = lut[v3];
        d[0] = l0; d[1] = l1; d[2] = l2; d[3] = l3;
        d += 4;
    } while (--pixelCount);
}

// 明示的インスタンス化（非inlineを維持）
template void lut8toN<uint16_t>(uint16_t*, const uint8_t*, int, const uint16_t*);
template void lut8toN<uint32_t>(uint32_t*, const uint8_t*, int, const uint32_t*);

} // namespace detail
} // namespace pixel_format
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

// ========================================================================
// 各ピクセルフォーマット（個別ヘッダ）
// ========================================================================

#include "pixel_format/rgba8_straight.h"
#include "pixel_format/alpha8.h"
#include "pixel_format/rgb565.h"
#include "pixel_format/rgb332.h"
#include "pixel_format/rgb888.h"
#include "pixel_format/grayscale8.h"
#include "pixel_format/index8.h"

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
    PixelFormatIDs::RGBA8_Straight,
    PixelFormatIDs::RGB565_LE,
    PixelFormatIDs::RGB565_BE,
    PixelFormatIDs::RGB332,
    PixelFormatIDs::RGB888,
    PixelFormatIDs::BGR888,
    PixelFormatIDs::Alpha8,
    PixelFormatIDs::Grayscale8,
    PixelFormatIDs::Index8,
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
// FormatConverter: 変換パスの事前解決
// ========================================================================
//
// convertFormat の行単位呼び出しで毎回発生する条件分岐を排除するため、
// Prepare 時に最適な変換関数を解決する仕組み。
//
// 使用例:
//   auto converter = resolveConverter(srcFormat, dstFormat, &srcAux, allocator);
//   if (converter) {
//       converter(dstRow, srcRow, width);  // 分岐なし
//   }
//

// IAllocator の前方宣言（ポインタのみ使用）
namespace core { namespace memory { class IAllocator; } }

struct FormatConverter {
    // 解決済み変換関数（分岐なし）
    using ConvertFunc = void(*)(void* dst, const void* src,
                                int pixelCount, const void* ctx);
    ConvertFunc func = nullptr;

    // 解決済みコンテキスト（Prepare 時に確定）
    struct Context {
        // フォーマット情報（memcpy パス用）
        uint8_t pixelsPerUnit = 1;
        uint8_t bytesPerUnit = 4;

        // パレット情報（Index 展開用）
        const void* palette = nullptr;
        PixelFormatID paletteFormat = nullptr;
        uint16_t paletteColorCount = 0;

        // 解決済み関数ポインタ
        PixelFormatDescriptor::ExpandIndexFunc expandIndex = nullptr;
        PixelFormatDescriptor::ToStraightFunc toStraight = nullptr;
        PixelFormatDescriptor::FromStraightFunc fromStraight = nullptr;

        // 中間バッファ情報（合計バイト数/ピクセル、0 なら中間バッファ不要）
        int_fast8_t intermediateBpp = 0;

        // アロケータ（中間バッファ確保用）
        core::memory::IAllocator* allocator = nullptr;
    } ctx;

    // 行変換実行（分岐なし）
    void operator()(void* dst, const void* src, int pixelCount) const {
        func(dst, src, pixelCount, &ctx);
    }

    explicit operator bool() const { return func != nullptr; }
};

// 変換パス解決関数
// srcFormat/dstFormat 間の最適な変換関数を事前解決し、FormatConverter を返す。
// allocator が nullptr の場合は DefaultAllocator を使用。
FormatConverter resolveConverter(
    PixelFormatID srcFormat,
    PixelFormatID dstFormat,
    const PixelAuxInfo* srcAux = nullptr,
    core::memory::IAllocator* allocator = nullptr);

// ========================================================================
// フォーマット変換
// ========================================================================

// 2つのフォーマット間で変換
// - 同一フォーマット: 単純コピー
// - エンディアン兄弟: swapEndian
// - インデックスフォーマット: expandIndex → パレットフォーマット経由
// - それ以外はStraight形式（RGBA8_Straight）経由で変換
//
// 内部で resolveConverter を使用して最適な変換パスを解決する。
// 中間バッファが必要な場合は DefaultAllocator 経由で一時確保される。
inline void convertFormat(const void* src, PixelFormatID srcFormat,
                          void* dst, PixelFormatID dstFormat,
                          int pixelCount,
                          const PixelAuxInfo* srcAux = nullptr,
                          const PixelAuxInfo* dstAux = nullptr) {
    (void)dstAux;  // 現在の全呼び出し箇所で未使用
    auto converter = resolveConverter(srcFormat, dstFormat, srcAux);
    if (converter) {
        converter(dst, src, pixelCount);
    }
}

} // namespace FLEXIMG_NAMESPACE

// FormatConverter 実装（FLEXIMG_IMPLEMENTATION ガード内）
#include "pixel_format/format_converter.h"

#endif // FLEXIMG_PIXEL_FORMAT_H
