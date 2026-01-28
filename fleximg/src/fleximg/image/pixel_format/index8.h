#ifndef FLEXIMG_PIXEL_FORMAT_INDEX8_H
#define FLEXIMG_PIXEL_FORMAT_INDEX8_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor等は既に定義済み）

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// 組み込みフォーマット宣言
// ========================================================================

namespace BuiltinFormats {
    extern const PixelFormatDescriptor Index8;
}

namespace PixelFormatIDs {
    inline const PixelFormatID Index8 = &BuiltinFormats::Index8;
}

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

#include "../../core/format_metrics.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// Index8: パレットインデックス（8bit） → パレットフォーマットのピクセルデータ
// ========================================================================

// expandIndex: インデックス値をパレットフォーマットのピクセルに展開
// aux->palette, aux->paletteFormat を参照
// 出力はパレットフォーマットのピクセルデータ
static void index8_expandIndex(void* dst, const void* src,
                               int pixelCount, const PixelAuxInfo* aux) {
    FLEXIMG_FMT_METRICS(Index8, ToStraight, pixelCount);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);

    if (!aux || !aux->palette || !aux->paletteFormat) {
        // パレットなし: ゼロ埋め
        std::memset(dst, 0, static_cast<size_t>(pixelCount));
        return;
    }

    const uint8_t* p = static_cast<const uint8_t*>(aux->palette);
    // bitsPerPixel / 8 でバイト数を取得（getBytesPerPixelはまだ定義前）
    int_fast8_t bpc = static_cast<int_fast8_t>((aux->paletteFormat->bitsPerPixel + 7) / 8);
    uint16_t maxIdx = static_cast<uint16_t>(aux->paletteColorCount > 0
                       ? aux->paletteColorCount - 1 : 0);

    for (int i = 0; i < pixelCount; ++i) {
        uint8_t idx = s[i];
        if (idx > maxIdx) idx = static_cast<uint8_t>(maxIdx);
        std::memcpy(d + static_cast<size_t>(i) * static_cast<size_t>(bpc),
                    p + static_cast<size_t>(idx) * static_cast<size_t>(bpc),
                    static_cast<size_t>(bpc));
    }
}

// ------------------------------------------------------------------------
// フォーマット定義
// ------------------------------------------------------------------------

namespace BuiltinFormats {

const PixelFormatDescriptor Index8 = {
    "Index8",
    8,   // bitsPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    1,   // channelCount
    { ChannelDescriptor(ChannelType::Index, 8, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },  // Index only
    false,   // hasAlpha
    true,    // isIndexed
    256,     // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    nullptr,  // toStraight (インデックスフォーマットはexpandIndex経由)
    nullptr,  // fromStraight (逆変換は未実装)
    index8_expandIndex,  // expandIndex
    nullptr,  // blendUnderStraight
    nullptr,  // siblingEndian
    nullptr   // swapEndian
};

} // namespace BuiltinFormats

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_INDEX8_H
