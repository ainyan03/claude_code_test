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
    int count,
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
    int count,
    const DDAParam* param
);

// BPP別 DDA転写関数（前方宣言）
// 実装は FLEXIMG_IMPLEMENTATION 部で提供
void copyRowDDA_1bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param);
void copyRowDDA_2bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param);
void copyRowDDA_3bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param);
void copyRowDDA_4bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param);

// BPP別 DDA 4ピクセル抽出関数（前方宣言）
void copyQuadDDA_1bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param);
void copyQuadDDA_2bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param);
void copyQuadDDA_3bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param);
void copyQuadDDA_4bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param);

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

    uint8_t alphaMultiplier = 255;  // アルファ係数（1 byte）AlphaNodeで使用
    uint32_t colorKeyRGBA8 = 0;    // カラーキー比較値（RGBA8、alpha込み）
    uint32_t colorKeyReplace = 0;  // カラーキー差し替え値（通常は透明黒0）
    // colorKeyRGBA8 == colorKeyReplace の場合は無効

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

    // コンストラクタ（チャンネル種別を指定）
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
    CopyQuadDDA_Func copyQuadDDA;                // DDA方式の4ピクセル抽出（バイリニア用、nullptrなら未対応）

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
template void lut8toN<uint16_t>(uint16_t*, const uint8_t*, int, const uint16_t*);
template void lut8toN<uint32_t>(uint32_t*, const uint8_t*, int, const uint32_t*);

} // namespace detail
} // namespace pixel_format

// ============================================================================
// DDA転写関数 - 実装
// ============================================================================

namespace pixel_format {
namespace detail {

// BPP → ネイティブ型マッピング（ロード・ストア分離用）
// BPP 1, 2, 4 はネイティブ型で直接ロード・ストア可能
// BPP 3 はネイティブ型が存在しないため byte 単位で処理
template<size_t BPP> struct PixelType {};
template<> struct PixelType<1> { using type = uint8_t; };
template<> struct PixelType<2> { using type = uint16_t; };
template<> struct PixelType<4> { using type = uint32_t; };

// DDA行転写: Y座標一定パス（ソース行が同一の場合）
// srcRowBase = srcData + sy * srcStride（呼び出し前に計算済み）
// 4ピクセル単位展開でループオーバーヘッドを削減
template<size_t BytesPerPixel>
void copyRowDDA_ConstY(
    uint8_t* __restrict__ dstRow,
    const uint8_t* __restrict__ srcData,
    int count,
    const DDAParam* param
) {
    int_fixed srcX = param->srcX;
    const int_fixed incrX = param->incrX;
    const int32_t srcStride = param->srcStride;
    const uint8_t* srcRowBase = srcData + static_cast<size_t>((param->srcY >> INT_FIXED_SHIFT) * srcStride);

    // 端数を先に処理し、4ピクセルループを最後に連続実行する
    if constexpr (BytesPerPixel == 3) {
        if (count & 1) {
            // BPP==3: byte単位でロード・ストア分離（3bytes × 4pixels）
            size_t s0 = static_cast<size_t>(srcX >> INT_FIXED_SHIFT) * 3;
            uint8_t p00 = srcRowBase[s0], p01 = srcRowBase[s0+1], p02 = srcRowBase[s0+2];
            srcX += incrX;
            dstRow[0]  = p00; dstRow[1]  = p01; dstRow[2]  = p02;
            dstRow += BytesPerPixel;
        }
        count >>= 1;
        while (count--) {
            // BPP==3: byte単位でロード・ストア分離（3bytes × 4pixels）
            size_t s0 = static_cast<size_t>(srcX >> INT_FIXED_SHIFT) * 3;
            uint8_t p00 = srcRowBase[s0], p01 = srcRowBase[s0+1], p02 = srcRowBase[s0+2];
            srcX += incrX;
            dstRow[0]  = p00; dstRow[1]  = p01; dstRow[2]  = p02;

            size_t s1 = static_cast<size_t>(srcX >> INT_FIXED_SHIFT) * 3;
            uint8_t p10 = srcRowBase[s1], p11 = srcRowBase[s1+1], p12 = srcRowBase[s1+2];
            srcX += incrX;
            dstRow[3]  = p10; dstRow[4]  = p11; dstRow[5]  = p12;

            dstRow += BytesPerPixel * 2;
        }
    } else {
        using T = typename PixelType<BytesPerPixel>::type;
        auto src = reinterpret_cast<const T*>(srcRowBase);
        auto dst = reinterpret_cast<T*>(dstRow);
        int remainder = count & 3;
        for (int i = 0; i < remainder; i++) {
            // BPP 1, 2, 4: ネイティブ型でロード・ストア分離
            auto p0 = src[srcX >> INT_FIXED_SHIFT]; srcX += incrX;
            dst[0] = p0;
            dst += 1;
        }
        int count4 = count >> 2;
        for (int i = 0; i < count4; i++) {
            // BPP 1, 2, 4: ネイティブ型でロード・ストア分離
            auto p0 = src[srcX >> INT_FIXED_SHIFT]; srcX += incrX;
            auto p1 = src[srcX >> INT_FIXED_SHIFT]; srcX += incrX;
            auto p2 = src[srcX >> INT_FIXED_SHIFT]; srcX += incrX;
            auto p3 = src[srcX >> INT_FIXED_SHIFT]; srcX += incrX;
            dst[0] = p0;
            dst[1] = p1;
            dst[2] = p2;
            dst[3] = p3;
            dst += 4;
        }
    }
}

// DDA行転写: X座標一定パス（ソース列が同一の場合）
// srcColBase = srcData + sx * BPP（呼び出し前に加算済み）
// 4ピクセル単位展開でループオーバーヘッドを削減
template<size_t BytesPerPixel>
void copyRowDDA_ConstX(
    uint8_t* __restrict__ dstRow,
    const uint8_t* __restrict__ srcData,
    int count,
    const DDAParam* param
) {
    int_fixed srcY = param->srcY;
    const int_fixed incrY = param->incrY;
    const int32_t srcStride = param->srcStride;
    const uint8_t* srcColBase = srcData + static_cast<size_t>((param->srcX >> INT_FIXED_SHIFT) * static_cast<int32_t>(BytesPerPixel));

    int32_t sy;
    // 端数を先に処理し、4ピクセルループを最後に連続実行する
    if constexpr (BytesPerPixel == 3) {
        while (count--) {
            // BPP==3: byte単位でピクセルごとにロード・ストア
            sy = srcY >> INT_FIXED_SHIFT;
            const uint8_t* r = srcColBase + static_cast<size_t>(sy * srcStride);
            auto p0 = r[0], p1 = r[1], p2 = r[2];
            srcY += incrY;
            dstRow[0] = p0; dstRow[1] = p1; dstRow[2] = p2;
            dstRow += BytesPerPixel;
        }
    } else {
        using T = typename PixelType<BytesPerPixel>::type;
        auto dst = reinterpret_cast<T*>(dstRow);
        int remain = count & 3;
        while (remain--) {
            sy = srcY >> INT_FIXED_SHIFT;
            auto p = *reinterpret_cast<const T*>(srcColBase + static_cast<size_t>(sy * srcStride));
            srcY += incrY;
            dst[0] = p;
            dst += 1;
        }

        count >>= 2;
        while (count--) {
            // BPP 1, 2, 4: ネイティブ型でロード・ストア分離
            sy = srcY >> INT_FIXED_SHIFT;
            auto p0 = *reinterpret_cast<const T*>(srcColBase + static_cast<size_t>(sy * srcStride));
            srcY += incrY;
            sy = srcY >> INT_FIXED_SHIFT;
            auto p1 = *reinterpret_cast<const T*>(srcColBase + static_cast<size_t>(sy * srcStride));
            srcY += incrY;
            dst[0] = p0;
            dst[1] = p1;
            sy = srcY >> INT_FIXED_SHIFT;
            auto p2 = *reinterpret_cast<const T*>(srcColBase + static_cast<size_t>(sy * srcStride));
            srcY += incrY;
            sy = srcY >> INT_FIXED_SHIFT;
            auto p3 = *reinterpret_cast<const T*>(srcColBase + static_cast<size_t>(sy * srcStride));
            srcY += incrY;
            dst[2] = p2;
            dst[3] = p3;
            dst += 4;
        }
    }

}

// DDA行転写の汎用実装（両方非ゼロ、回転を含む変換）
// 4ピクセル単位展開でループオーバーヘッドを削減
template<size_t BytesPerPixel>
void copyRowDDA_Impl(
    uint8_t* __restrict__ dstRow,
    const uint8_t* __restrict__ srcData,
    int count,
    const DDAParam* param
) {
    int_fixed srcY = param->srcY;
    int_fixed srcX = param->srcX;
    const int_fixed incrY = param->incrY;
    const int_fixed incrX = param->incrX;
    const int32_t srcStride = param->srcStride;

    int32_t sx, sy;
    // 端数を先に処理し、4ピクセルループを最後に連続実行する
    if constexpr (BytesPerPixel == 3) {
        while (count--) {
            // BPP==3: byte単位でピクセルごとにロード・ストア
            sx = srcX >> INT_FIXED_SHIFT;
            sy = srcY >> INT_FIXED_SHIFT;
            const uint8_t* r0 = srcData + static_cast<size_t>(sy * srcStride + sx * 3);
            uint8_t p00 = r0[0], p01 = r0[1], p02 = r0[2];
            srcX += incrX; srcY += incrY;
            dstRow[0] = p00; dstRow[1] = p01; dstRow[2] = p02;
            dstRow += BytesPerPixel;
        }
    } else {
        using T = typename PixelType<BytesPerPixel>::type;
        auto d = reinterpret_cast<T*>(dstRow);
        if (count & 1) {
            sx = srcX >> INT_FIXED_SHIFT;
            sy = srcY >> INT_FIXED_SHIFT;
            auto p = reinterpret_cast<const T*>(srcData + static_cast<size_t>(sy * srcStride))[sx];
            srcX += incrX; srcY += incrY;
            d[0] = p;
            d++;
        }

        count >>= 1;
        while (count--) {
            sx = srcX >> INT_FIXED_SHIFT;
            sy = srcY >> INT_FIXED_SHIFT;
            auto p0 = reinterpret_cast<const T*>(srcData + static_cast<size_t>(sy * srcStride))[sx];
            srcX += incrX; srcY += incrY;
            sx = srcX >> INT_FIXED_SHIFT;
            sy = srcY >> INT_FIXED_SHIFT;
            auto p1 = reinterpret_cast<const T*>(srcData + static_cast<size_t>(sy * srcStride))[sx];
            srcX += incrX; srcY += incrY;
            // BPP 1, 2, 4: ネイティブ型でロード・ストア分離
            d[0] = p0;
            d[1] = p1;
            d += 2;
        }
    }
}

// ============================================================================
// BPP別 DDA転写関数（CopyRowDDA_Func シグネチャ準拠）
// ============================================================================
//
// PixelFormatDescriptor::copyRowDDA に設定する関数群。
//

template<size_t BytesPerPixel>
void copyRowDDA_bpp(
    uint8_t* dst,
    const uint8_t* srcData,
    int count,
    const DDAParam* param
) {
    const int_fixed srcY = param->srcY;
    const int_fixed incrY = param->incrY;
    // ソース座標の整数部が全ピクセルで同一か判定（座標は呼び出し側で非負が保証済み）
    if (0 == (((srcY & ((1 << INT_FIXED_SHIFT) - 1)) + incrY * count) >> INT_FIXED_SHIFT)) {
        // Y座標一定パス（高頻度: 回転なし拡大縮小・平行移動、微小Y変動も含む）
        copyRowDDA_ConstY<BytesPerPixel>(dst, srcData, count, param);
        return;
    }

    const int_fixed srcX = param->srcX;
    const int_fixed incrX = param->incrX;
    if (0 == (((srcX & ((1 << INT_FIXED_SHIFT) - 1)) + incrX * count) >> INT_FIXED_SHIFT)) {
        // X座標一定パス（微小X変動も含む）
        copyRowDDA_ConstX<BytesPerPixel>(dst, srcData, count, param);
        return;
    }

    // 汎用パス（回転を含む変換）
    copyRowDDA_Impl<BytesPerPixel>(dst, srcData, count, param);
}

// 明示的インスタンス化（各フォーマットから参照される）
template void copyRowDDA_bpp<1>(uint8_t*, const uint8_t*, int, const DDAParam*);
template void copyRowDDA_bpp<2>(uint8_t*, const uint8_t*, int, const DDAParam*);
template void copyRowDDA_bpp<3>(uint8_t*, const uint8_t*, int, const DDAParam*);
template void copyRowDDA_bpp<4>(uint8_t*, const uint8_t*, int, const DDAParam*);

// BPP別の関数ポインタ取得用ラッパー（非テンプレート）
inline void copyRowDDA_1bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyRowDDA_bpp<1>(dst, srcData, count, param);
}
inline void copyRowDDA_2bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyRowDDA_bpp<2>(dst, srcData, count, param);
}
inline void copyRowDDA_3bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyRowDDA_bpp<3>(dst, srcData, count, param);
}
inline void copyRowDDA_4bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyRowDDA_bpp<4>(dst, srcData, count, param);
}

// ============================================================================
// DDA 4ピクセル抽出関数（バイリニア補間用）
// ============================================================================
//
// バイリニア補間に必要な4ピクセル（2x2グリッド）を抽出する。
// 出力形式: [p00,p10,p01,p11][p00,p10,p01,p11]... × count
// 重み情報はparam->weightsに出力される。
//
// 最適化:
// - 案1: CheckBoundaryテンプレートパラメータで境界チェック有無を制御
// - 案2: コピー部分のみテンプレート化（copyQuadPixels）
//

// 4ピクセルのコピー（BPP依存部分のみ）
template<size_t BPP>
inline void copyQuadPixels(
    uint8_t* __restrict__ dst,
    const uint8_t* p00,
    const uint8_t* p10,
    const uint8_t* p01,
    const uint8_t* p11
) {
    if constexpr (BPP == 3) {
        dst[0] = p00[0]; dst[1] = p00[1]; dst[2] = p00[2];
        dst[3] = p10[0]; dst[4] = p10[1]; dst[5] = p10[2];
        dst[6] = p01[0]; dst[7] = p01[1]; dst[8] = p01[2];
        dst[9] = p11[0]; dst[10] = p11[1]; dst[11] = p11[2];
    } else {
        using T = typename PixelType<BPP>::type;
        auto d = reinterpret_cast<T*>(dst);
        auto d0 = *reinterpret_cast<const T*>(p00);
        auto d1 = *reinterpret_cast<const T*>(p10);
        auto d2 = *reinterpret_cast<const T*>(p01);
        auto d3 = *reinterpret_cast<const T*>(p11);
        d[0] = d0;
        d[1] = d1;
        d[2] = d2;
        d[3] = d3;
    }
}

// 4ピクセル抽出（DDAベース、バイリニア補間用）
// 境界領域と安全領域の2ブロック構成:
//   boundary [0, safeStart) → safe [safeStart, safeEnd) → boundary [safeEnd, count)
// fadeFlags は prepareCopyQuadDDA で事前生成済み、この関数では参照・更新しない
template<size_t BytesPerPixel>
void copyQuadDDA_bpp(
    uint8_t* __restrict__ dst,
    const uint8_t* __restrict__ srcData,
    int count,
    const DDAParam* param
) {
    constexpr size_t BPP = BytesPerPixel;
    constexpr size_t QUAD_SIZE = BPP * 4;

    int_fixed srcX = param->srcX;
    int_fixed srcY = param->srcY;
    const int_fixed incrX = param->incrX;
    const int_fixed incrY = param->incrY;
    const int32_t srcStride = param->srcStride;
    const int32_t srcLastX = param->srcWidth - 1;
    const int32_t srcLastY = param->srcHeight - 1;
    BilinearWeightXY* weightsXY = param->weightsXY;
    uint8_t* edgeFlags = param->edgeFlags;

    // 全ピクセル境界チェック版（事前範囲チェックなし）
    for (int i = 0; i < count; ++i) {
        int32_t sx = srcX >> INT_FIXED_SHIFT;
        int32_t sy = srcY >> INT_FIXED_SHIFT;
        weightsXY[i].fx = static_cast<uint8_t>(static_cast<uint32_t>(srcX) >> (INT_FIXED_SHIFT - 8));
        weightsXY[i].fy = static_cast<uint8_t>(static_cast<uint32_t>(srcY) >> (INT_FIXED_SHIFT - 8));
        srcX += incrX;
        srcY += incrY;
        bool x_sub = static_cast<uint32_t>(sx) < static_cast<uint32_t>(srcLastX);
        bool y_sub = static_cast<uint32_t>(sy) < static_cast<uint32_t>(srcLastY);

        if (x_sub && y_sub) {
            const uint8_t* p = srcData + static_cast<size_t>(sy) * static_cast<size_t>(srcStride) + static_cast<size_t>(sx) * BPP;
            edgeFlags[i] = 0;
    
            if constexpr (BPP == 3) {
                dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2];
                dst[3] = p[3]; dst[4] = p[4]; dst[5] = p[5];
                p += srcStride;
                dst[6] = p[0]; dst[7] = p[1]; dst[8] = p[2];
                dst[9] = p[3]; dst[10] = p[4]; dst[11] = p[5];
            } else {
                using T = typename PixelType<BPP>::type;
                auto d = reinterpret_cast<T*>(dst);
                auto val0 = reinterpret_cast<const T*>(p)[0];
                auto val1 = reinterpret_cast<const T*>(p)[1];
                d[0] = val0;
                d[1] = val1;
                p += srcStride;
                val0 = reinterpret_cast<const T*>(p)[0];
                val1 = reinterpret_cast<const T*>(p)[1];
                d[2] = val0;
                d[3] = val1;
            }
            dst += QUAD_SIZE;
        } else {
            // edgeFlags生成: 境界座標からフェードフラグを導出
            uint8_t flag_x = EdgeFade_Right;
            uint8_t flag_y = EdgeFade_Bottom;
            if (!x_sub) {
                if (sx < 0) {
                    sx = 0;
                    flag_x = EdgeFade_Left;
                }
            }
            if (!y_sub) {
                if (sy < 0) {
                    sy = 0;
                    flag_y = EdgeFade_Top;
                }
            }

            const uint8_t* p = srcData + static_cast<size_t>(sy) * static_cast<size_t>(srcStride) + static_cast<size_t>(sx) * BPP;
    
            if constexpr (BPP == 3) {
                auto val0 = p[0]; auto val1 = p[1]; auto val2 = p[2];
                dst[0] = val0; dst[1] = val1; dst[2] = val2;
                dst[3] = val0; dst[4] = val1; dst[5] = val2;
                dst[6] = val0; dst[7] = val1; dst[8] = val2;
                if (x_sub) {
                    val0 = p[3]; val1 = p[4]; val2 = p[5];
                    flag_x = 0;
                    dst[3] = val0; dst[4] = val1; dst[5] = val2;
                } else if (y_sub) {
                    p += srcStride;
                    val0 = p[0]; val1 = p[1]; val2 = p[2];
                    flag_y = 0;
                    dst[6] = val0; dst[7] = val1; dst[8] = val2;
                }
                dst[9] = val0; dst[10] = val1; dst[11] = val2;
            } else {
                using T = typename PixelType<BPP>::type;
                auto d = reinterpret_cast<T*>(dst);
                auto val = reinterpret_cast<const T*>(p)[0];
                d[0] = val; d[1] = val; d[2] = val;
                if (x_sub) {
                    val = reinterpret_cast<const T*>(p)[1];
                    flag_x = 0;
                    d[1] = val;
                } else if (y_sub) {
                    p += srcStride;
                    val = reinterpret_cast<const T*>(p)[0];
                    flag_y = 0;
                    d[2] = val;
                }
                d[3] = val;
            }
            edgeFlags[i] = flag_x + flag_y;
            dst += QUAD_SIZE;
        }
    }
}
// 明示的インスタンス化
template void copyQuadDDA_bpp<1>(uint8_t*, const uint8_t*, int, const DDAParam*);
template void copyQuadDDA_bpp<2>(uint8_t*, const uint8_t*, int, const DDAParam*);
template void copyQuadDDA_bpp<3>(uint8_t*, const uint8_t*, int, const DDAParam*);
template void copyQuadDDA_bpp<4>(uint8_t*, const uint8_t*, int, const DDAParam*);

// BPP別の関数ポインタ取得用ラッパー（非テンプレート）
inline void copyQuadDDA_1bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyQuadDDA_bpp<1>(dst, srcData, count, param);
}
inline void copyQuadDDA_2bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyQuadDDA_bpp<2>(dst, srcData, count, param);
}
inline void copyQuadDDA_3bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyQuadDDA_bpp<3>(dst, srcData, count, param);
}
inline void copyQuadDDA_4bpp(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyQuadDDA_bpp<4>(dst, srcData, count, param);
}

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
#include "pixel_format/bit_packed_index.h"

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

        // BPP情報（チャンク処理のポインタ進行用）
        int_fast8_t srcBpp = 0;
        int_fast8_t dstBpp = 0;

        // パレット展開時のBPP（中間バッファ用）
        int_fast8_t paletteBpp = 0;

        // カラーキー情報（toStraight後にin-placeで適用）
        uint32_t colorKeyRGBA8 = 0;
        uint32_t colorKeyReplace = 0;
    } ctx;

    // 行変換実行（分岐なし）
    void operator()(void* dst, const void* src, int pixelCount) const {
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
