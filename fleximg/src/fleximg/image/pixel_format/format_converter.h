#ifndef FLEXIMG_PIXEL_FORMAT_FORMAT_CONVERTER_H
#define FLEXIMG_PIXEL_FORMAT_FORMAT_CONVERTER_H

// pixel_format.h の末尾からインクルードされることを前提
// （FormatConverter, PixelFormatDescriptor, PixelFormatIDs 等は既に定義済み）

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

#include "../../core/memory/allocator.h"

namespace FLEXIMG_NAMESPACE {

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
}

// Index展開: パレットフォーマット == 出力フォーマット（直接展開）
static void fcv_expandIndex_direct(void* dst, const void* src,
                                   int pixelCount, const void* ctx) {
    auto* c = static_cast<const FormatConverter::Context*>(ctx);
    PixelAuxInfo aux;
    aux.palette = c->palette;
    aux.paletteFormat = c->paletteFormat;
    aux.paletteColorCount = c->paletteColorCount;
    c->expandIndex(dst, src, pixelCount, &aux);
}

// Index展開 + fromStraight（パレットフォーマット == RGBA8）
static void fcv_expandIndex_fromStraight(void* dst, const void* src,
                                         int pixelCount, const void* ctx) {
    auto* c = static_cast<const FormatConverter::Context*>(ctx);
    size_t bufSize = static_cast<size_t>(pixelCount) * 4;
    uint8_t* buf = static_cast<uint8_t*>(c->allocator->allocate(bufSize));
    PixelAuxInfo aux;
    aux.palette = c->palette;
    aux.paletteFormat = c->paletteFormat;
    aux.paletteColorCount = c->paletteColorCount;
    c->expandIndex(buf, src, pixelCount, &aux);
    c->fromStraight(dst, buf, pixelCount, nullptr);
    c->allocator->deallocate(buf);
}

// Index展開 + toStraight + fromStraight（パレットフォーマット != RGBA8, 一般）
static void fcv_expandIndex_toStraight_fromStraight(
    void* dst, const void* src, int pixelCount, const void* ctx) {
    auto* c = static_cast<const FormatConverter::Context*>(ctx);
    // intermediateBpp = palBpp + 4 (expand用 + RGBA8変換用)
    int_fast8_t expandBpp = static_cast<int_fast8_t>(c->intermediateBpp - 4);
    size_t totalBufSize = static_cast<size_t>(pixelCount)
                        * static_cast<size_t>(c->intermediateBpp);
    uint8_t* buf = static_cast<uint8_t*>(c->allocator->allocate(totalBufSize));
    uint8_t* expandBuf = buf;
    uint8_t* straightBuf = buf
        + static_cast<size_t>(pixelCount) * static_cast<size_t>(expandBpp);

    PixelAuxInfo aux;
    aux.palette = c->palette;
    aux.paletteFormat = c->paletteFormat;
    aux.paletteColorCount = c->paletteColorCount;
    c->expandIndex(expandBuf, src, pixelCount, &aux);
    c->toStraight(straightBuf, expandBuf, pixelCount, nullptr);
    c->fromStraight(dst, straightBuf, pixelCount, nullptr);
    c->allocator->deallocate(buf);
}

// 一般: toStraight + fromStraight（RGBA8 経由 2段階変換）
static void fcv_toStraight_fromStraight(void* dst, const void* src,
                                        int pixelCount, const void* ctx) {
    auto* c = static_cast<const FormatConverter::Context*>(ctx);
    size_t bufSize = static_cast<size_t>(pixelCount) * 4;
    uint8_t* buf = static_cast<uint8_t*>(c->allocator->allocate(bufSize));
    c->toStraight(buf, src, pixelCount, nullptr);
    c->fromStraight(dst, buf, pixelCount, nullptr);
    c->allocator->deallocate(buf);
}

// ========================================================================
// resolveConverter 実装
// ========================================================================

FormatConverter resolveConverter(
    PixelFormatID srcFormat,
    PixelFormatID dstFormat,
    const PixelAuxInfo* srcAux,
    core::memory::IAllocator* allocator)
{
    FormatConverter result;

    if (!srcFormat || !dstFormat) return result;

    // デフォルトアロケータ設定
    result.ctx.allocator = allocator
        ? allocator : &core::memory::DefaultAllocator::instance();

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

        if (palFmt == PixelFormatIDs::RGBA8_Straight) {
            // expandIndex → fromStraight
            if (dstFormat->fromStraight) {
                result.ctx.fromStraight = dstFormat->fromStraight;
                result.ctx.intermediateBpp = 4;
                result.func = fcv_expandIndex_fromStraight;
            }
            return result;
        }

        // expandIndex → toStraight → fromStraight
        if (palFmt && palFmt->toStraight && dstFormat->fromStraight) {
            auto palBpp = getBytesPerPixel(palFmt);
            result.ctx.toStraight = palFmt->toStraight;
            result.ctx.fromStraight = dstFormat->fromStraight;
            result.ctx.intermediateBpp =
                static_cast<int_fast8_t>(palBpp + 4);
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
            result.func = fcv_single;
        }
        return result;
    }

    // 一般: toStraight + fromStraight
    if (srcFormat->toStraight && dstFormat->fromStraight) {
        result.ctx.toStraight = srcFormat->toStraight;
        result.ctx.fromStraight = dstFormat->fromStraight;
        result.ctx.intermediateBpp = 4;
        result.func = fcv_toStraight_fromStraight;
    }

    return result;
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_FORMAT_CONVERTER_H
