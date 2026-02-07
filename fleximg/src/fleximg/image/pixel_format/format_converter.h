#ifndef FLEXIMG_PIXEL_FORMAT_FORMAT_CONVERTER_H
#define FLEXIMG_PIXEL_FORMAT_FORMAT_CONVERTER_H

// pixel_format.h の末尾からインクルードされることを前提
// （FormatConverter, PixelFormatDescriptor, PixelFormatIDs 等は既に定義済み）

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {

// チャンクサイズ（スタック上の中間バッファ用）
static constexpr int FCV_CHUNK_SIZE = 64;

// 対応フォーマットの最大バイト/ピクセル（RGBA8 = 4）
// 将来RGBA16等を追加する場合は更新が必要
static constexpr int MAX_BYTES_PER_PIXEL = 4;

// ========================================================================
// カラーキー適用ヘルパー（toStraight後のRGBA8バッファにin-placeで適用）
// ========================================================================

static inline void applyColorKey(uint32_t* rgba8, int pixelCount,
                                  uint32_t colorKey, uint32_t replace) {
    if (colorKey == replace) return;
    while (pixelCount & 3) {
        --pixelCount;
        if (rgba8[0] == colorKey) { rgba8[0] = replace; }
        ++rgba8;
    }
    pixelCount >>= 2;
    while (pixelCount--) {
        auto c0 = rgba8[0];
        auto c1 = rgba8[1];
        auto c2 = rgba8[2];
        auto c3 = rgba8[3];
        if (c0 == colorKey) { rgba8[0] = replace; }
        if (c1 == colorKey) { rgba8[1] = replace; }
        if (c2 == colorKey) { rgba8[2] = replace; }
        if (c3 == colorKey) { rgba8[3] = replace; }
        rgba8 += 4;
    }
}

// ========================================================================
// 解決済み変換関数群（FormatConverter::func に設定される static 関数）
// ========================================================================

// 同一フォーマット: memcpy
static void fcv_memcpy(void* dst, const void* src,
                       int pixelCount, const void* ctx) {
    auto* c = static_cast<const FormatConverter::Context*>(ctx);
    size_t units = static_cast<size_t>(
        (pixelCount + c->pixelsPerUnit - 1) / c->pixelsPerUnit);
    std::memcpy(dst, src, units * c->bytesPerUnit);
}

// 1段階変換: toStraight フィールドに格納された関数を直接呼び出し
// （swapEndian, toStraight(dst=RGBA8), fromStraight(src=RGBA8) 共通）
static void fcv_single(void* dst, const void* src,
                       int pixelCount, const void* ctx) {
    auto* c = static_cast<const FormatConverter::Context*>(ctx);
    c->toStraight(dst, src, pixelCount, nullptr);
    applyColorKey(static_cast<uint32_t*>(dst), pixelCount,
                  c->colorKeyRGBA8, c->colorKeyReplace);
}

// Index展開: パレットフォーマット == 出力フォーマット（直接展開）
static void fcv_expandIndex_direct(void* dst, const void* src,
                                   int pixelCount, const void* ctx) {
    auto* c = static_cast<const FormatConverter::Context*>(ctx);
    PixelAuxInfo aux;
    aux.palette = c->palette;
    aux.paletteFormat = c->paletteFormat;
    aux.paletteColorCount = c->paletteColorCount;
    aux.pixelOffsetInByte = c->pixelOffsetInByte;  // bit-packed用
    c->expandIndex(dst, src, pixelCount, &aux);
}

// Index展開 + fromStraight（パレットフォーマット == RGBA8）
// チャンク処理でアロケーション不要
static void fcv_expandIndex_fromStraight(void* dst, const void* src,
                                         int pixelCount, const void* ctx) {
    auto* c = static_cast<const FormatConverter::Context*>(ctx);
    uint8_t straightBuf[FCV_CHUNK_SIZE * MAX_BYTES_PER_PIXEL];

    PixelAuxInfo aux;
    aux.palette = c->palette;
    aux.paletteFormat = c->paletteFormat;
    aux.paletteColorCount = c->paletteColorCount;
    aux.pixelOffsetInByte = c->pixelOffsetInByte;  // bit-packed用

    auto* dstPtr = static_cast<uint8_t*>(dst);
    auto* srcPtr = static_cast<const uint8_t*>(src);
    int remaining = pixelCount;

    while (remaining > 0) {
        int chunk = (remaining < FCV_CHUNK_SIZE) ? remaining : FCV_CHUNK_SIZE;
        c->expandIndex(straightBuf, srcPtr, chunk, &aux);
        applyColorKey(reinterpret_cast<uint32_t*>(straightBuf), chunk,
                      c->colorKeyRGBA8, c->colorKeyReplace);
        c->fromStraight(dstPtr, straightBuf, chunk, nullptr);
        srcPtr += chunk * c->srcBytesPerPixel;
        dstPtr += chunk * c->dstBytesPerPixel;
        remaining -= chunk;
    }
}

// Index展開 + toStraight + fromStraight（パレットフォーマット != RGBA8, 一般）
// 単一バッファでin-place処理（expandIndex出力を末尾詰めし、toStraightで先頭から上書き）
static void fcv_expandIndex_toStraight_fromStraight(
    void* dst, const void* src, int pixelCount, const void* ctx) {
    auto* c = static_cast<const FormatConverter::Context*>(ctx);
    FLEXIMG_ASSERT(c->paletteBytesPerPixel <= MAX_BYTES_PER_PIXEL,
                   "paletteBytesPerPixel exceeds MAX_BYTES_PER_PIXEL");

    // 単一バッファ: expandIndex出力を末尾に配置し、toStraightで先頭から上書き
    uint8_t buf[FCV_CHUNK_SIZE * MAX_BYTES_PER_PIXEL];

    PixelAuxInfo aux;
    aux.palette = c->palette;
    aux.paletteFormat = c->paletteFormat;
    aux.paletteColorCount = c->paletteColorCount;
    aux.pixelOffsetInByte = c->pixelOffsetInByte;  // bit-packed用

    auto* dstPtr = static_cast<uint8_t*>(dst);
    auto* srcPtr = static_cast<const uint8_t*>(src);
    int remaining = pixelCount;

    // 末尾詰めオフセット: toStraightが前から処理する際に上書きが発生しない位置（固定）
    uint8_t* expandPtr = buf + (MAX_BYTES_PER_PIXEL - c->paletteBytesPerPixel) * FCV_CHUNK_SIZE;

    while (remaining > 0) {
        int chunk = (remaining < FCV_CHUNK_SIZE) ? remaining : FCV_CHUNK_SIZE;
        c->expandIndex(expandPtr, srcPtr, chunk, &aux);
        c->toStraight(buf, expandPtr, chunk, nullptr);
        applyColorKey(reinterpret_cast<uint32_t*>(buf), chunk,
                      c->colorKeyRGBA8, c->colorKeyReplace);
        c->fromStraight(dstPtr, buf, chunk, nullptr);
        srcPtr += chunk * c->srcBytesPerPixel;
        dstPtr += chunk * c->dstBytesPerPixel;
        remaining -= chunk;
    }
}

// 一般: toStraight + fromStraight（RGBA8 経由 2段階変換）
// チャンク処理でアロケーション不要
static void fcv_toStraight_fromStraight(void* dst, const void* src,
                                        int pixelCount, const void* ctx) {
    auto* c = static_cast<const FormatConverter::Context*>(ctx);
    uint8_t straightBuf[FCV_CHUNK_SIZE * MAX_BYTES_PER_PIXEL];

    auto* dstPtr = static_cast<uint8_t*>(dst);
    auto* srcPtr = static_cast<const uint8_t*>(src);
    int remaining = pixelCount;

    while (remaining > 0) {
        int chunk = (remaining < FCV_CHUNK_SIZE) ? remaining : FCV_CHUNK_SIZE;
        c->toStraight(straightBuf, srcPtr, chunk, nullptr);
        applyColorKey(reinterpret_cast<uint32_t*>(straightBuf), chunk,
                      c->colorKeyRGBA8, c->colorKeyReplace);
        c->fromStraight(dstPtr, straightBuf, chunk, nullptr);
        srcPtr += chunk * c->srcBytesPerPixel;
        dstPtr += chunk * c->dstBytesPerPixel;
        remaining -= chunk;
    }
}

// ========================================================================
// resolveConverter 実装
// ========================================================================

FormatConverter resolveConverter(
    PixelFormatID srcFormat,
    PixelFormatID dstFormat,
    const PixelAuxInfo* srcAux)
{
    FormatConverter result;

    if (!srcFormat || !dstFormat) return result;

    // BytesPerPixel情報を設定（チャンク処理のポインタ進行用）
    result.ctx.srcBytesPerPixel = static_cast<int_fast8_t>(getBytesPerPixel(srcFormat));
    result.ctx.dstBytesPerPixel = static_cast<int_fast8_t>(getBytesPerPixel(dstFormat));

    // pixelOffsetInByteを伝播（bit-packed用）
    if (srcAux) {
        result.ctx.pixelOffsetInByte = srcAux->pixelOffsetInByte;
    }

    // 同一フォーマット → memcpy
    if (srcFormat == dstFormat) {
        result.ctx.pixelsPerUnit = srcFormat->pixelsPerUnit;
        result.ctx.bytesPerUnit = srcFormat->bytesPerUnit;
        result.func = fcv_memcpy;
        return result;
    }

    // エンディアン兄弟 → swapEndian
    if (srcFormat->siblingEndian == dstFormat && srcFormat->swapEndian) {
        result.ctx.toStraight = srcFormat->swapEndian;
        result.func = fcv_single;
        return result;
    }

    // インデックスフォーマット + パレット
    if (srcFormat->expandIndex && srcAux && srcAux->palette) {
        PixelFormatID palFmt = srcAux->paletteFormat;
        result.ctx.palette = srcAux->palette;
        result.ctx.paletteFormat = palFmt;
        result.ctx.paletteColorCount = srcAux->paletteColorCount;
        result.ctx.expandIndex = srcFormat->expandIndex;

        if (palFmt == dstFormat) {
            // 直接展開: Index → パレットフォーマット == 出力フォーマット
            result.func = fcv_expandIndex_direct;
            return result;
        }

        // インデックスフォーマットのcolorKey設定（共通）
        if (srcAux->colorKeyRGBA8 != srcAux->colorKeyReplace) {
            result.ctx.colorKeyRGBA8 = srcAux->colorKeyRGBA8;
            result.ctx.colorKeyReplace = srcAux->colorKeyReplace;
        }

        if (palFmt == PixelFormatIDs::RGBA8_Straight) {
            // expandIndex → fromStraight
            if (dstFormat->fromStraight) {
                result.ctx.fromStraight = dstFormat->fromStraight;
                result.func = fcv_expandIndex_fromStraight;
            }
            return result;
        }

        // expandIndex → toStraight → fromStraight
        if (palFmt && palFmt->toStraight && dstFormat->fromStraight) {
            result.ctx.toStraight = palFmt->toStraight;
            result.ctx.fromStraight = dstFormat->fromStraight;
            result.ctx.paletteBytesPerPixel = static_cast<int_fast8_t>(getBytesPerPixel(palFmt));
            result.func = fcv_expandIndex_toStraight_fromStraight;
        }
        return result;
    }

    // src == RGBA8 → fromStraight 直接（中間バッファ不要）
    if (srcFormat == PixelFormatIDs::RGBA8_Straight) {
        if (dstFormat->fromStraight) {
            result.ctx.toStraight = dstFormat->fromStraight;
            result.func = fcv_single;
        }
        return result;
    }

    // dst == RGBA8 → toStraight 直接（中間バッファ不要）
    if (dstFormat == PixelFormatIDs::RGBA8_Straight) {
        if (srcFormat->toStraight) {
            result.ctx.toStraight = srcFormat->toStraight;
            if (srcAux && !srcFormat->hasAlpha
                && srcAux->colorKeyRGBA8 != srcAux->colorKeyReplace) {
                result.ctx.colorKeyRGBA8 = srcAux->colorKeyRGBA8;
                result.ctx.colorKeyReplace = srcAux->colorKeyReplace;
            }
            result.func = fcv_single;
        }
        return result;
    }

    // 一般: toStraight + fromStraight
    if (srcFormat->toStraight && dstFormat->fromStraight) {
        result.ctx.toStraight = srcFormat->toStraight;
        result.ctx.fromStraight = dstFormat->fromStraight;
        if (srcAux && !srcFormat->hasAlpha
            && srcAux->colorKeyRGBA8 != srcAux->colorKeyReplace) {
            result.ctx.colorKeyRGBA8 = srcAux->colorKeyRGBA8;
            result.ctx.colorKeyReplace = srcAux->colorKeyReplace;
        }
        result.func = fcv_toStraight_fromStraight;
    }

    return result;
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_FORMAT_CONVERTER_H
