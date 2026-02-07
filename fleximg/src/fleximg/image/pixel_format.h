#ifndef FLEXIMG_PIXEL_FORMAT_H
#define FLEXIMG_PIXEL_FORMAT_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <climits>
#include "../core/common.h"
#include "../core/types.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// エッジフェードアウトフラグ（方向ベース）
// ========================================================================
//
// バイリニア補間時に、どの辺でフェードアウト処理を行うかを指定。
// フェードアウト有効な辺では:
//   - 出力範囲が0.5ピクセル拡張される
//   - 境界外ピクセルのアルファが0になり、なめらかに透明化
// フェードアウト無効な辺では:
//   - 出力範囲はNearestと同じ
//   - 境界ピクセルはクランプ（端のピクセルを複製）
//
enum EdgeFadeFlags : uint8_t {
    EdgeFade_None   = 0,
    EdgeFade_Left   = 0x01,
    EdgeFade_Right  = 0x02,
    EdgeFade_Top    = 0x04,
    EdgeFade_Bottom = 0x08,
    EdgeFade_All    = 0x0F
};


// ========================================================================
// バイリニア補間の重み情報
// ========================================================================

// バイリニア補間の重み（チャンク用、fx/fyのみ）
struct BilinearWeightXY {
    uint8_t fx;         // X方向小数部（0-255）
    uint8_t fy;         // Y方向小数部（0-255）
};

// edgeFlags の説明（チャンク用の別配列として管理、copyQuadDDA 内で生成）
// EdgeFadeFlags と同じビット配置で境界方向を格納する。
// 消費側で edgeFadeMask と AND してから、各ピクセルの無効判定に使用する。
//   p00（左上）: flags & (EdgeFade_Left  | EdgeFade_Top)
//   p10（右上）: flags & (EdgeFade_Right | EdgeFade_Top)
//   p01（左下）: flags & (EdgeFade_Left  | EdgeFade_Bottom)
//   p11（右下）: flags & (EdgeFade_Right | EdgeFade_Bottom)

// ========================================================================
// DDA転写パラメータ
// ========================================================================
//
// copyRowDDA / copyQuadDDA 関数に渡すパラメータ構造体。
// アフィン変換等でのピクセルサンプリングに使用する。
//

struct DDAParam {
    int32_t srcStride;    // ソースのストライド（バイト数）
    int32_t srcWidth;     // ソース幅（copyQuadDDA用、境界クランプ）
    int32_t srcHeight;    // ソース高さ（copyQuadDDA用、境界クランプ）
    int_fixed srcX;       // ソース開始X座標（Q16.16固定小数点）
    int_fixed srcY;       // ソース開始Y座標（Q16.16固定小数点）
    int_fixed incrX;      // 1ピクセルあたりのX増分（Q16.16固定小数点）
    int_fixed incrY;      // 1ピクセルあたりのY増分（Q16.16固定小数点）

    // バイリニア補間用
    BilinearWeightXY* weightsXY;  // 重み出力先（チャンク用）
    uint8_t* edgeFlags;           // 境界フラグ出力先（チャンク用、copyQuadDDA内で生成、必須）
};

// DDA行転写関数の型定義
// dst: 出力先バッファ
// srcData: ソースデータ先頭
// count: 転写ピクセル数
// param: DDAパラメータ（const、関数内でローカルコピーして使用）
using CopyRowDDA_Func = void(*)(
    uint8_t* dst,
    const uint8_t* srcData,
    int_fast16_t count,
    const DDAParam* param
);

// DDA 4ピクセル抽出関数の型定義（バイリニア補間用）
// dst: 出力先バッファ（[p00,p10,p01,p11] × count）
// srcData: ソースデータ先頭
// count: 抽出ピクセル数
// param: DDAパラメータ（srcWidth/srcHeight/weightsを使用）
using CopyQuadDDA_Func = void(*)(
    uint8_t* dst,
    const uint8_t* srcData,
    int_fast16_t count,
    const DDAParam* param
);

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

    // カラーキー情報（toStraight後にin-placeで適用）
    uint32_t colorKeyRGBA8 = 0;    // カラーキー比較値（RGBA8、alpha込み）
    uint32_t colorKeyReplace = 0;  // カラーキー差し替え値（通常は透明黒0）
    // colorKeyRGBA8 == colorKeyReplace の場合は無効

    uint16_t paletteColorCount = 0;          // パレットエントリ数
    uint8_t alphaMultiplier = 255;  // アルファ係数（1 byte）AlphaNodeで使用
    uint8_t pixelOffsetInByte = 0;  // bit-packed用: 1バイト内でのピクセル位置 (0 - PixelsPerByte-1)
                                     // Index1: 0-7, Index2: 0-3, Index4: 0-1

    // デフォルトコンストラクタ
    constexpr PixelAuxInfo() = default;

    // アルファ係数指定
    constexpr explicit PixelAuxInfo(uint8_t alpha)
        : alphaMultiplier(alpha) {}

    // カラーキー指定
    constexpr PixelAuxInfo(uint32_t keyRGBA8, uint32_t replaceRGBA8)
        : colorKeyRGBA8(keyRGBA8), colorKeyReplace(replaceRGBA8) {}
};

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
// ピクセルフォーマット記述子
// ========================================================================

struct PixelFormatDescriptor {
    // ========================================================================
    // 変換関数の型定義
    // ========================================================================
    // 統一シグネチャ: void(*)(void* dst, const void* src, size_t pixelCount, const PixelAuxInfo* aux)

    // Straight形式（RGBA8_Straight）との相互変換
    using ConvertFunc = void(*)(void* dst, const void* src, size_t pixelCount, const PixelAuxInfo* aux);
    using ToStraightFunc = ConvertFunc;
    using FromStraightFunc = ConvertFunc;

    // インデックス展開関数（インデックス値 → パレットフォーマットのピクセルデータ）
    // aux->palette, aux->paletteFormat を参照してインデックスをパレットエントリに展開
    // 出力はパレットフォーマット（RGBA8とは限らない）
    using ExpandIndexFunc = ConvertFunc;

    // BlendUnderStraightFunc: srcフォーマットからStraight形式(RGBA8)のdstへunder合成
    //   - dst が不透明なら何もしない（スキップ）
    //   - dst が透明なら単純コピー
    //   - dst が半透明ならunder合成（unpremultiply含む）
    using BlendUnderStraightFunc = ConvertFunc;

    // SwapEndianFunc: エンディアン違いの兄弟フォーマットとの変換
    using SwapEndianFunc = ConvertFunc;

    // ========================================================================
    // メンバ（アライメント効率順: ポインタ → 4byte → 2byte → 1byte）
    // ========================================================================

    // フォーマット名
    const char* name;

    // 変換関数ポインタ（ダイレクトカラー用）
    ToStraightFunc toStraight;
    FromStraightFunc fromStraight;
    ExpandIndexFunc expandIndex;   // 非インデックスフォーマットでは nullptr
    BlendUnderStraightFunc blendUnderStraight;  // 未実装の場合は nullptr

    // エンディアン変換（兄弟フォーマットがある場合）
    const PixelFormatDescriptor* siblingEndian;  // エンディアン違いの兄弟（なければnullptr）
    SwapEndianFunc swapEndian;                   // バイトスワップ関数

    // DDA転写関数
    CopyRowDDA_Func copyRowDDA;                  // DDA方式の行転写（nullptrなら未対応）
    CopyQuadDDA_Func copyQuadDDA;                // DDA方式の4ピクセル抽出（バイリニア用、nullptrなら未対応）

    // エンディアン情報
    BitOrder bitOrder;
    ByteOrder byteOrder;

    // パレット情報（インデックスカラーの場合）
    uint16_t maxPaletteSize;

    // 基本情報
    uint8_t bitsPerPixel;       // ピクセルあたりのビット数
    uint8_t bytesPerPixel;      // ピクセルあたりのバイト数（切り上げ）
    uint8_t pixelsPerUnit;      // 1ユニットあたりのピクセル数
    uint8_t bytesPerUnit;       // 1ユニットあたりのバイト数
    uint8_t channelCount;       // チャンネル総数

    // フラグ
    bool hasAlpha;
    bool isIndexed;
};

// ========================================================================
// 内部ヘルパー関数（ピクセルフォーマット実装用）
// ========================================================================

namespace pixel_format {
namespace detail {

// BytesPerPixel別 DDA転写関数（前方宣言）
// 実装は dda.h で提供（FLEXIMG_IMPLEMENTATION部）
void copyRowDDA_1Byte(uint8_t* dst, const uint8_t* srcData, int_fast16_t count, const DDAParam* param);
void copyRowDDA_2Byte(uint8_t* dst, const uint8_t* srcData, int_fast16_t count, const DDAParam* param);
void copyRowDDA_3Byte(uint8_t* dst, const uint8_t* srcData, int_fast16_t count, const DDAParam* param);
void copyRowDDA_4Byte(uint8_t* dst, const uint8_t* srcData, int_fast16_t count, const DDAParam* param);

// BytesPerPixel別 DDA 4ピクセル抽出関数（前方宣言）
void copyQuadDDA_1Byte(uint8_t* dst, const uint8_t* srcData, int_fast16_t count, const DDAParam* param);
void copyQuadDDA_2Byte(uint8_t* dst, const uint8_t* srcData, int_fast16_t count, const DDAParam* param);
void copyQuadDDA_3Byte(uint8_t* dst, const uint8_t* srcData, int_fast16_t count, const DDAParam* param);
void copyQuadDDA_4Byte(uint8_t* dst, const uint8_t* srcData, int_fast16_t count, const DDAParam* param);

// BitsPerPixel別 bit-packed DDA転写関数（前方宣言）
// 実装は dda.h で提供（bit_packed_index.h インクルード後）
template<int BitsPerPixel, BitOrder Order>
void copyRowDDA_Bit(uint8_t* dst, const uint8_t* srcData, int_fast16_t count, const DDAParam* param);

template<int BitsPerPixel, BitOrder Order>
void copyQuadDDA_Bit(uint8_t* dst, const uint8_t* srcData, int_fast16_t count, const DDAParam* param);

// 8bit LUT → Nbit 変換（4ピクセル単位展開）
// T = uint32_t: rgb332_toStraight, index8_expandIndex (bpc==4) 等で共用
// T = uint16_t: index8_expandIndex (bpc==2) 等で共用
template<typename T>
void lut8toN(T* d, const uint8_t* s, size_t pixelCount, const T* lut);

// 便利エイリアス
inline void lut8to32(uint32_t* d, const uint8_t* s, size_t pixelCount, const uint32_t* lut) {
    lut8toN(d, s, pixelCount, lut);
}
inline void lut8to16(uint16_t* d, const uint8_t* s, size_t pixelCount, const uint16_t* lut) {
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
void lut8toN(T* d, const uint8_t* s, size_t pixelCount, const T* lut) {
    while (pixelCount & 3) {
        auto v0 = s[0];
        ++s;
        auto l0 = lut[v0];
        --pixelCount;
        d[0] = l0;
        ++d;
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
template void lut8toN<uint16_t>(uint16_t*, const uint8_t*, size_t, const uint16_t*);
template void lut8toN<uint32_t>(uint32_t*, const uint8_t*, size_t, const uint32_t*);

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
#include "pixel_format/index.h"

// DDA関数（bit_packed_detail が定義された後にインクルード）
#ifdef FLEXIMG_IMPLEMENTATION
#include "pixel_format/dda.h"
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// ユーティリティ関数
// ========================================================================

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
    PixelFormatIDs::Index1_MSB,
    PixelFormatIDs::Index1_LSB,
    PixelFormatIDs::Index2_MSB,
    PixelFormatIDs::Index2_LSB,
    PixelFormatIDs::Index4_MSB,
    PixelFormatIDs::Index4_LSB,
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
//   auto converter = resolveConverter(srcFormat, dstFormat, &srcAux);
//   if (converter) {
//       converter(dstRow, srcRow, width);  // 分岐なし
//   }
//

struct FormatConverter {
    // 解決済み変換関数（分岐なし）
    using ConvertFunc = void(*)(void* dst, const void* src,
                                size_t pixelCount, const void* ctx);
    ConvertFunc func = nullptr;

    // 解決済みコンテキスト（Prepare 時に確定）
    struct Context {
        // 解決済み関数ポインタ
        PixelFormatDescriptor::ExpandIndexFunc expandIndex = nullptr;
        PixelFormatDescriptor::ToStraightFunc toStraight = nullptr;
        PixelFormatDescriptor::FromStraightFunc fromStraight = nullptr;

        // パレット情報（Index 展開用）
        const void* palette = nullptr;
        PixelFormatID paletteFormat = nullptr;
        uint16_t paletteColorCount = 0;

        // フォーマット情報（memcpy パス用）
        uint8_t pixelsPerUnit = 1;
        uint8_t bytesPerUnit = 4;

        // カラーキー情報（toStraight後にin-placeで適用）
        uint32_t colorKeyRGBA8 = 0;
        uint32_t colorKeyReplace = 0;

        // BytesPerPixel情報（チャンク処理のポインタ進行用）
        uint8_t srcBytesPerPixel = 0;
        uint8_t dstBytesPerPixel = 0;

        // パレット展開時のBytesPerPixel（中間バッファ用）
        uint8_t paletteBytesPerPixel = 0;

        // bit-packed用: 1バイト内でのピクセル位置（0 - PixelsPerByte-1）
        uint8_t pixelOffsetInByte = 0;
    } ctx;

    // 行変換実行（分岐なし）
    void operator()(void* dst, const void* src, size_t pixelCount) const {
        func(dst, src, pixelCount, &ctx);
    }

    explicit operator bool() const { return func != nullptr; }
};

// 変換パス解決関数
// srcFormat/dstFormat 間の最適な変換関数を事前解決し、FormatConverter を返す。
// チャンク処理により中間バッファはスタック上に確保されるため、アロケータ不要。
FormatConverter resolveConverter(
    PixelFormatID srcFormat,
    PixelFormatID dstFormat,
    const PixelAuxInfo* srcAux = nullptr);

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
