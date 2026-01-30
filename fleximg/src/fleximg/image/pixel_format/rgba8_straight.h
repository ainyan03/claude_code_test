#ifndef FLEXIMG_PIXEL_FORMAT_RGBA8_STRAIGHT_H
#define FLEXIMG_PIXEL_FORMAT_RGBA8_STRAIGHT_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
    extern const PixelFormatDescriptor RGBA8_Straight;
}

namespace PixelFormatIDs {
    inline const PixelFormatID RGBA8_Straight = &BuiltinFormats::RGBA8_Straight;
}

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

#include "../../core/format_metrics.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// RGBA8_Straight 変換関数
// 標準フォーマット: RGBA8_Straight（8bit RGBA、ストレートアルファ）
// ========================================================================

// RGBA8_Straight: Straight形式なのでコピー
static void rgba8Straight_toStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA8_Straight, ToStraight, pixelCount);
    std::memcpy(dst, src, static_cast<size_t>(pixelCount) * 4);
}

static void rgba8Straight_fromStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA8_Straight, FromStraight, pixelCount);
    std::memcpy(dst, src, static_cast<size_t>(pixelCount) * 4);
}

// blendUnderStraight: RGBA8_Straight形式のunder合成（背面への合成）
//
// under合成の数式:
//   resultA = dstA + srcA * (1 - dstA/255)
//   resultColor = (dstColor * dstA + srcColor * srcA * (1 - dstA/255)) / resultA
//
// 処理パターン:
// - dstA == 255（不透明）: スキップ（背面は見えない）
// - dstA == 0（透明）: srcをコピー
// - srcA == 0（透明）: スキップ（合成対象なし）
// - それ以外: ブレンド計算
//
// 最適化手法:
// 1. gotoラベル方式のディスパッチ（分岐予測しやすい）
// 2. 4ピクセル単位の連続領域高速スキップ/コピー
// 3. 正規化重み方式によるブレンド計算の効率化:
//    - 重みを合計256に正規化し、シフトで除算を代替
//    - dstW = (dstA * 255 * 256) / total, srcW = 256 - dstW
//    - 色計算: (d * dstW + s * srcW) >> 8
//    - R,Bチャンネルを32ビット演算でまとめて処理
static void rgba8Straight_blendUnderStraight(void* __restrict__ dst, const void* __restrict__ src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGBA8_Straight, BlendUnder, pixelCount);
    if (pixelCount <= 0) return;

    const uint8_t* __restrict__ s = static_cast<const uint8_t*>(src);
    uint_fast8_t srcA = s[3];
    uint8_t* __restrict__ d = static_cast<uint8_t*>(dst);
    uint_fast8_t dstA = d[3];

    // メインディスパッチ: srcAを優先的にチェック
    if (srcA == 0) goto handle_srcA_0;
    if (dstA == 255) goto handle_dstA_255;
    if (dstA == 0) goto handle_dstA_0;
blend:
    // ========================================================================
    // ブレンド処理ループ（srcA != 0, dstA != 0, dstA != 255）
    //
    // 正規化重み方式:
    //   total = dstA * 255 + srcA * (255 - dstA)  // 合成後のアルファ×255
    //   dstW = (dstA * 255 * 256) / total         // dst側の重み（0〜256）
    //   srcW = 256 - dstW                          // src側の重み（0〜256）
    //   color = (d * dstW + s * srcW) >> 8        // シフトで除算を代替
    //
    // 精度: 最大誤差±1（90%以上が完全一致）
    //
    // do-whileで末尾デクリメント: breakした時点でpixelCountはまだ減っていない
    // ========================================================================
    do {
        dstA = d[3];
        srcA = s[3];
        if (dstA == 255) break;
        if (dstA == 0) break;
        if (srcA == 0) break;

        // 合成後アルファの計算（255スケール）
        uint_fast32_t dstA_255 = dstA * 255;
        uint_fast32_t invDstA = 255 - dstA;
        uint_fast32_t srcA_invDstA = srcA * invDstA;
        uint_fast32_t total = dstA_255 + srcA_invDstA;

        // 正規化重み（合計256、除算1回）
        uint_fast32_t dstW = (dstA_255 * 256 + (total >> 1)) / total;
        uint_fast32_t srcW = 256 - dstW;

        // R,Bを32ビットでまとめて処理（リトルエンディアン: [R,G,B,A]）
        uint32_t d32 = *reinterpret_cast<uint32_t*>(d);
        uint32_t s32 = *reinterpret_cast<const uint32_t*>(s);
        uint32_t d32_odd = d[1];              // G（単独処理）
        uint32_t s32_odd = s[1];
        uint32_t d32_even = d32 & 0xFF00FF;   // R, B（まとめて処理）
        uint32_t s32_even = s32 & 0xFF00FF;

        // 重み付き加算 + シフト（255 * 256 = 65280 < 65536 でオーバーフローなし）
        d32_even = d32_even * dstW + s32_even * srcW;
        d32_odd = d32_odd * dstW + s32_odd * srcW;

        // 結果を書き込み
        *reinterpret_cast<uint32_t*>(d) = d32_even >> 8;  // R, B
        d[1] = static_cast<uint8_t>(d32_odd >> 8);        // G
        d[3] = static_cast<uint8_t>((total + 127) / 255); // A（正確な計算）

        d += 4;
        s += 4;
    } while (--pixelCount > 0);
    if (pixelCount <= 0) return;
    if (srcA == 0) goto handle_srcA_0;
    if (dstA == 0) goto handle_dstA_0;
    // ここには通常到達しない

    // ========================================================================
    // dst不透明(255) → 連続スキップ
    // ========================================================================
handle_dstA_255:
    {
        // 現在のピクセルは255確認済み、スキップ
        if (--pixelCount <= 0) return;
        d += 4;
        dstA = d[3];
        s += 4;

        // 4ピクセル単位でスキップ
        auto plimit = pixelCount >> 2;
        if (plimit && dstA == 255) {
            auto d_start = d;
            do {
                uint_fast8_t a1 = d[7];
                uint_fast8_t a2 = d[11];
                uint_fast8_t a3 = d[15];
                if ((dstA & a1 & a2 & a3) != 255) break;
                d += 16;
                dstA = d[3];
            } while (--plimit);
            auto pindex = (d - d_start);
            if (d != d_start) {
                pixelCount -= pindex >> 2;
                if (pixelCount <= 0) return;
                s += pindex;
            }
        }
        srcA = s[3];
        if (dstA == 255) goto handle_dstA_255;
        if (dstA == 0) goto handle_dstA_0;
        if (srcA == 0) goto handle_srcA_0;
        goto blend;
    }

    // ========================================================================
    // dst透明(0) → 連続コピー
    // ========================================================================
handle_dstA_0:
    {
        // 現在のピクセルは0確認済み、コピー
        *reinterpret_cast<uint32_t*>(d) = *reinterpret_cast<const uint32_t*>(s);
        if (--pixelCount <= 0) return;
        d += 4;
        dstA = d[3];
        s += 4;

        // 4ピクセル単位でコピー
        auto plimit = pixelCount >> 2;
        if (plimit && dstA == 0) {
            auto s_start = s;
            do {
                uint_fast8_t a1 = d[7];
                uint_fast8_t a2 = d[11];
                uint_fast8_t a3 = d[15];
                if ((dstA | a1 | a2 | a3) != 0) break;
                reinterpret_cast<uint32_t*>(d)[0] = reinterpret_cast<const uint32_t*>(s)[0];
                reinterpret_cast<uint32_t*>(d)[1] = reinterpret_cast<const uint32_t*>(s)[1];
                reinterpret_cast<uint32_t*>(d)[2] = reinterpret_cast<const uint32_t*>(s)[2];
                reinterpret_cast<uint32_t*>(d)[3] = reinterpret_cast<const uint32_t*>(s)[3];
                d += 16;
                dstA = d[3];
                s += 16;
            } while (--plimit);
            auto pindex = (s - s_start);
            if (pindex) {
                pixelCount -= pindex >> 2;
                if (pixelCount <= 0) return;
            }
        }

        srcA = s[3];
        if (dstA == 255) goto handle_dstA_255;
        if (dstA == 0) goto handle_dstA_0;
        if (srcA == 0) goto handle_srcA_0;
        goto blend;
    }

    // ========================================================================
    // src透明(0) → 連続スキップ
    // ========================================================================
handle_srcA_0:
    {
        // 現在のピクセルは0確認済み、スキップ
        if (--pixelCount <= 0) return;
        s += 4;
        srcA = s[3];
        d += 4;

        // 4ピクセル単位でスキップ
        auto plimit = pixelCount >> 2;
        if (plimit && srcA == 0) {
            auto s_start = s;
            do {
                uint_fast8_t a1 = s[7];
                uint_fast8_t a2 = s[11];
                uint_fast8_t a3 = s[15];
                if ((srcA | a1 | a2 | a3) != 0) break;
                s += 16;
                srcA = s[3];
            } while (--plimit);
            auto pindex = (s - s_start);
            if (pindex) {
                pixelCount -= pindex >> 2;
                if (pixelCount <= 0) return;
                d += pindex;
            }
        }
        dstA = d[3];
        if (srcA == 0) goto handle_srcA_0;
        if (dstA == 255) goto handle_dstA_255;
        if (dstA == 0) goto handle_dstA_0;
        goto blend;
    }
}

// ------------------------------------------------------------------------
// フォーマット定義
// ------------------------------------------------------------------------

namespace BuiltinFormats {

const PixelFormatDescriptor RGBA8_Straight = {
    "RGBA8_Straight",
    32,  // bitsPerPixel
    1,   // pixelsPerUnit
    4,   // bytesPerUnit
    4,   // channelCount
    { ChannelDescriptor(ChannelType::Red, 8, 0),
      ChannelDescriptor(ChannelType::Green, 8, 0),
      ChannelDescriptor(ChannelType::Blue, 8, 0),
      ChannelDescriptor(ChannelType::Alpha, 8, 0) },  // R, G, B, A
    true,   // hasAlpha
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    rgba8Straight_toStraight,
    rgba8Straight_fromStraight,
    nullptr,  // expandIndex
    rgba8Straight_blendUnderStraight,  // blendUnderStraight
    nullptr,  // siblingEndian
    nullptr   // swapEndian
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_RGBA8_STRAIGHT_H
