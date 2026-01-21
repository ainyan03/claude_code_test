#include "blend.h"
#include "../core/types.h"
#include "../image/pixel_format.h"
#include <cstring>
#include <algorithm>

namespace FLEXIMG_NAMESPACE {
namespace blend {

// ========================================================================
// 逆数テーブル（除算回避用）
// ========================================================================
// invAlphaTable[a] = (255 << 16) / a  (for a > 0)
// 使用: (value * invAlphaTable[alpha]) >> 24 ≈ value * 255 / alpha
//
namespace {
constexpr uint32_t calcInvAlpha(int a) {
    return (a > 0) ? ((255u << 16) / static_cast<uint32_t>(a)) : 0;
}

// 256エントリの逆数テーブル
alignas(64) constexpr uint32_t invAlphaTable[256] = {
    calcInvAlpha(0),   calcInvAlpha(1),   calcInvAlpha(2),   calcInvAlpha(3),
    calcInvAlpha(4),   calcInvAlpha(5),   calcInvAlpha(6),   calcInvAlpha(7),
    calcInvAlpha(8),   calcInvAlpha(9),   calcInvAlpha(10),  calcInvAlpha(11),
    calcInvAlpha(12),  calcInvAlpha(13),  calcInvAlpha(14),  calcInvAlpha(15),
    calcInvAlpha(16),  calcInvAlpha(17),  calcInvAlpha(18),  calcInvAlpha(19),
    calcInvAlpha(20),  calcInvAlpha(21),  calcInvAlpha(22),  calcInvAlpha(23),
    calcInvAlpha(24),  calcInvAlpha(25),  calcInvAlpha(26),  calcInvAlpha(27),
    calcInvAlpha(28),  calcInvAlpha(29),  calcInvAlpha(30),  calcInvAlpha(31),
    calcInvAlpha(32),  calcInvAlpha(33),  calcInvAlpha(34),  calcInvAlpha(35),
    calcInvAlpha(36),  calcInvAlpha(37),  calcInvAlpha(38),  calcInvAlpha(39),
    calcInvAlpha(40),  calcInvAlpha(41),  calcInvAlpha(42),  calcInvAlpha(43),
    calcInvAlpha(44),  calcInvAlpha(45),  calcInvAlpha(46),  calcInvAlpha(47),
    calcInvAlpha(48),  calcInvAlpha(49),  calcInvAlpha(50),  calcInvAlpha(51),
    calcInvAlpha(52),  calcInvAlpha(53),  calcInvAlpha(54),  calcInvAlpha(55),
    calcInvAlpha(56),  calcInvAlpha(57),  calcInvAlpha(58),  calcInvAlpha(59),
    calcInvAlpha(60),  calcInvAlpha(61),  calcInvAlpha(62),  calcInvAlpha(63),
    calcInvAlpha(64),  calcInvAlpha(65),  calcInvAlpha(66),  calcInvAlpha(67),
    calcInvAlpha(68),  calcInvAlpha(69),  calcInvAlpha(70),  calcInvAlpha(71),
    calcInvAlpha(72),  calcInvAlpha(73),  calcInvAlpha(74),  calcInvAlpha(75),
    calcInvAlpha(76),  calcInvAlpha(77),  calcInvAlpha(78),  calcInvAlpha(79),
    calcInvAlpha(80),  calcInvAlpha(81),  calcInvAlpha(82),  calcInvAlpha(83),
    calcInvAlpha(84),  calcInvAlpha(85),  calcInvAlpha(86),  calcInvAlpha(87),
    calcInvAlpha(88),  calcInvAlpha(89),  calcInvAlpha(90),  calcInvAlpha(91),
    calcInvAlpha(92),  calcInvAlpha(93),  calcInvAlpha(94),  calcInvAlpha(95),
    calcInvAlpha(96),  calcInvAlpha(97),  calcInvAlpha(98),  calcInvAlpha(99),
    calcInvAlpha(100), calcInvAlpha(101), calcInvAlpha(102), calcInvAlpha(103),
    calcInvAlpha(104), calcInvAlpha(105), calcInvAlpha(106), calcInvAlpha(107),
    calcInvAlpha(108), calcInvAlpha(109), calcInvAlpha(110), calcInvAlpha(111),
    calcInvAlpha(112), calcInvAlpha(113), calcInvAlpha(114), calcInvAlpha(115),
    calcInvAlpha(116), calcInvAlpha(117), calcInvAlpha(118), calcInvAlpha(119),
    calcInvAlpha(120), calcInvAlpha(121), calcInvAlpha(122), calcInvAlpha(123),
    calcInvAlpha(124), calcInvAlpha(125), calcInvAlpha(126), calcInvAlpha(127),
    calcInvAlpha(128), calcInvAlpha(129), calcInvAlpha(130), calcInvAlpha(131),
    calcInvAlpha(132), calcInvAlpha(133), calcInvAlpha(134), calcInvAlpha(135),
    calcInvAlpha(136), calcInvAlpha(137), calcInvAlpha(138), calcInvAlpha(139),
    calcInvAlpha(140), calcInvAlpha(141), calcInvAlpha(142), calcInvAlpha(143),
    calcInvAlpha(144), calcInvAlpha(145), calcInvAlpha(146), calcInvAlpha(147),
    calcInvAlpha(148), calcInvAlpha(149), calcInvAlpha(150), calcInvAlpha(151),
    calcInvAlpha(152), calcInvAlpha(153), calcInvAlpha(154), calcInvAlpha(155),
    calcInvAlpha(156), calcInvAlpha(157), calcInvAlpha(158), calcInvAlpha(159),
    calcInvAlpha(160), calcInvAlpha(161), calcInvAlpha(162), calcInvAlpha(163),
    calcInvAlpha(164), calcInvAlpha(165), calcInvAlpha(166), calcInvAlpha(167),
    calcInvAlpha(168), calcInvAlpha(169), calcInvAlpha(170), calcInvAlpha(171),
    calcInvAlpha(172), calcInvAlpha(173), calcInvAlpha(174), calcInvAlpha(175),
    calcInvAlpha(176), calcInvAlpha(177), calcInvAlpha(178), calcInvAlpha(179),
    calcInvAlpha(180), calcInvAlpha(181), calcInvAlpha(182), calcInvAlpha(183),
    calcInvAlpha(184), calcInvAlpha(185), calcInvAlpha(186), calcInvAlpha(187),
    calcInvAlpha(188), calcInvAlpha(189), calcInvAlpha(190), calcInvAlpha(191),
    calcInvAlpha(192), calcInvAlpha(193), calcInvAlpha(194), calcInvAlpha(195),
    calcInvAlpha(196), calcInvAlpha(197), calcInvAlpha(198), calcInvAlpha(199),
    calcInvAlpha(200), calcInvAlpha(201), calcInvAlpha(202), calcInvAlpha(203),
    calcInvAlpha(204), calcInvAlpha(205), calcInvAlpha(206), calcInvAlpha(207),
    calcInvAlpha(208), calcInvAlpha(209), calcInvAlpha(210), calcInvAlpha(211),
    calcInvAlpha(212), calcInvAlpha(213), calcInvAlpha(214), calcInvAlpha(215),
    calcInvAlpha(216), calcInvAlpha(217), calcInvAlpha(218), calcInvAlpha(219),
    calcInvAlpha(220), calcInvAlpha(221), calcInvAlpha(222), calcInvAlpha(223),
    calcInvAlpha(224), calcInvAlpha(225), calcInvAlpha(226), calcInvAlpha(227),
    calcInvAlpha(228), calcInvAlpha(229), calcInvAlpha(230), calcInvAlpha(231),
    calcInvAlpha(232), calcInvAlpha(233), calcInvAlpha(234), calcInvAlpha(235),
    calcInvAlpha(236), calcInvAlpha(237), calcInvAlpha(238), calcInvAlpha(239),
    calcInvAlpha(240), calcInvAlpha(241), calcInvAlpha(242), calcInvAlpha(243),
    calcInvAlpha(244), calcInvAlpha(245), calcInvAlpha(246), calcInvAlpha(247),
    calcInvAlpha(248), calcInvAlpha(249), calcInvAlpha(250), calcInvAlpha(251),
    calcInvAlpha(252), calcInvAlpha(253), calcInvAlpha(254), calcInvAlpha(255)
};
} // anonymous namespace

// ========================================================================
// first - 透明キャンバスへの最初の描画
// ========================================================================

void first(ViewPort& dst, int_fixed dstOriginX, int_fixed dstOriginY,
           const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY) {
    if (!dst.isValid() || !src.isValid()) return;

    // 基準点を一致させるためのオフセット計算（固定小数点演算）
    int offsetX = from_fixed(dstOriginX - srcOriginX);
    int offsetY = from_fixed(dstOriginY - srcOriginY);

    // クリッピング範囲を計算
    int srcStartX = std::max(0, -offsetX);
    int srcStartY = std::max(0, -offsetY);
    int dstStartX = std::max(0, offsetX);
    int dstStartY = std::max(0, offsetY);
    int copyWidth = std::min(src.width - srcStartX, dst.width - dstStartX);
    int copyHeight = std::min(src.height - srcStartY, dst.height - dstStartY);

    if (copyWidth <= 0 || copyHeight <= 0) return;

    // フォーマットが同じならmemcpy
    if (src.formatID == dst.formatID) {
        size_t bpp = static_cast<size_t>(getBytesPerPixel(src.formatID));
        for (int y = 0; y < copyHeight; y++) {
            const void* srcRow = src.pixelAt(srcStartX, srcStartY + y);
            void* dstRow = dst.pixelAt(dstStartX, dstStartY + y);
            std::memcpy(dstRow, srcRow, static_cast<size_t>(copyWidth) * bpp);
        }
        return;
    }

#if 1  // RGBA16_Premultiplied サポート有効
    // RGBA8_Straight → RGBA16_Premultiplied 変換
    if (src.formatID == PixelFormatIDs::RGBA8_Straight &&
        dst.formatID == PixelFormatIDs::RGBA16_Premultiplied) {
        for (int y = 0; y < copyHeight; y++) {
            const uint8_t* srcRow = static_cast<const uint8_t*>(
                src.pixelAt(srcStartX, srcStartY + y));
            uint16_t* dstRow = static_cast<uint16_t*>(
                dst.pixelAt(dstStartX, dstStartY + y));

            for (int x = 0; x < copyWidth; x++) {
                uint16_t r = srcRow[x * 4 + 0];
                uint16_t g = srcRow[x * 4 + 1];
                uint16_t b = srcRow[x * 4 + 2];
                uint16_t a8 = srcRow[x * 4 + 3];

                // DESIGN_PIXEL_FORMAT.md に従った変換
                // A_tmp = A8 + 1 (範囲: 1-256)
                // A16 = 255 * A_tmp (範囲: 255-65280)
                // R16 = R8 * A_tmp (範囲: 0-65280)
                uint16_t a_tmp = a8 + 1;
                uint16_t a16 = 255 * a_tmp;
                uint16_t r16 = r * a_tmp;
                uint16_t g16 = g * a_tmp;
                uint16_t b16 = b * a_tmp;

                dstRow[x * 4 + 0] = r16;
                dstRow[x * 4 + 1] = g16;
                dstRow[x * 4 + 2] = b16;
                dstRow[x * 4 + 3] = a16;
            }
        }
        return;
    }
#endif

    // RGBA8_Straight 同士
    if (src.formatID == PixelFormatIDs::RGBA8_Straight &&
        dst.formatID == PixelFormatIDs::RGBA8_Straight) {
        size_t bpp = 4;
        for (int y = 0; y < copyHeight; y++) {
            const void* srcRow = src.pixelAt(srcStartX, srcStartY + y);
            void* dstRow = dst.pixelAt(dstStartX, dstStartY + y);
            std::memcpy(dstRow, srcRow, static_cast<size_t>(copyWidth) * bpp);
        }
        return;
    }

    // その他のフォーマット組み合わせ（未対応）
}

// ========================================================================
// onto - 既存画像への合成
// ========================================================================

void onto(ViewPort& dst, int_fixed dstOriginX, int_fixed dstOriginY,
          const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY) {
    if (!dst.isValid() || !src.isValid()) return;

    // 基準点を一致させるためのオフセット計算（固定小数点演算）
    int offsetX = from_fixed(dstOriginX - srcOriginX);
    int offsetY = from_fixed(dstOriginY - srcOriginY);

    // ループ範囲を事前計算
    int_fast32_t yStart = std::max(0, -offsetY);
    int_fast32_t yEnd = std::min<int_fast32_t>(src.height, dst.height - offsetY);
    int_fast32_t xStart = std::max(0, -offsetX);
    int_fast32_t xEnd = std::min<int_fast32_t>(src.width, dst.width - offsetX);

    if (yStart >= yEnd || xStart >= xEnd) return;

#if 1  // RGBA16_Premultiplied サポート有効
    // 閾値定数
    constexpr uint16_t ALPHA_TRANS_MAX = RGBA16Premul::ALPHA_TRANSPARENT_MAX;
    constexpr uint16_t ALPHA_OPAQUE_MIN = RGBA16Premul::ALPHA_OPAQUE_MIN;

    // RGBA8_Straight → RGBA16_Premultiplied への変換ブレンド
    if (src.formatID == PixelFormatIDs::RGBA8_Straight &&
        dst.formatID == PixelFormatIDs::RGBA16_Premultiplied) {
        for (int y = yStart; y < yEnd; y++) {
            const uint8_t* srcRow = static_cast<const uint8_t*>(src.pixelAt(0, y));
            uint16_t* dstRow = static_cast<uint16_t*>(dst.pixelAt(0, y + offsetY));

            for (int x = xStart; x < xEnd; x++) {
                const uint8_t* srcPixel8 = srcRow + x * 4;
                uint16_t* dstPixel = dstRow + (x + offsetX) * 4;

                uint16_t a8 = srcPixel8[3];
                uint16_t dstA = dstPixel[3];

                // dst に描画があり src が透明 → スキップ
                if (dstA > ALPHA_TRANS_MAX && a8 == 0) continue;

                // DESIGN_PIXEL_FORMAT.md に従った変換
                // A_tmp = A8 + 1 (範囲: 1-256)
                // A16 = 255 * A_tmp (範囲: 255-65280)
                // R16 = R8 * A_tmp (範囲: 0-65280)
                uint16_t a_tmp = a8 + 1;
                uint16_t srcA = 255 * a_tmp;
                uint16_t srcR = srcPixel8[0] * a_tmp;
                uint16_t srcG = srcPixel8[1] * a_tmp;
                uint16_t srcB = srcPixel8[2] * a_tmp;

                // 半透明ブレンド（dst に描画があり src が半透明の場合）
                if (dstA > ALPHA_TRANS_MAX && srcA < ALPHA_OPAQUE_MIN) {
                    uint16_t invSrcA = ALPHA_OPAQUE_MIN - srcA;
                    srcR += (dstPixel[0] * invSrcA) >> 16;
                    srcG += (dstPixel[1] * invSrcA) >> 16;
                    srcB += (dstPixel[2] * invSrcA) >> 16;
                    srcA += (dstA * invSrcA) >> 16;
                }

                dstPixel[0] = srcR;
                dstPixel[1] = srcG;
                dstPixel[2] = srcB;
                dstPixel[3] = srcA;
            }
        }
        return;
    }
#endif

    // RGBA8_Straight 同士のブレンド
    if (src.formatID == PixelFormatIDs::RGBA8_Straight &&
        dst.formatID == PixelFormatIDs::RGBA8_Straight) {
        for (int y = yStart; y < yEnd; y++) {
            const uint8_t* srcRow = static_cast<const uint8_t*>(src.pixelAt(0, y));
            uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(0, y + offsetY));

            for (int x = xStart; x < xEnd; x++) {
                const uint8_t* srcPixel = srcRow + x * 4;
                uint8_t* dstPixel = dstRow + (x + offsetX) * 4;

                uint32_t srcA = srcPixel[3];
                uint32_t dstA = dstPixel[3];

                // src が透明 → スキップ
                if (srcA == 0) continue;

                // src が不透明 → 単純コピー
                if (srcA == 255) {
                    dstPixel[0] = srcPixel[0];
                    dstPixel[1] = srcPixel[1];
                    dstPixel[2] = srcPixel[2];
                    dstPixel[3] = 255;
                    continue;
                }

                // dst が透明 → 単純コピー
                if (dstA == 0) {
                    dstPixel[0] = srcPixel[0];
                    dstPixel[1] = srcPixel[1];
                    dstPixel[2] = srcPixel[2];
                    dstPixel[3] = static_cast<uint8_t>(srcA);
                    continue;
                }

                // dst が不透明 → outAは必ず255、除算不要の最適化パス
                if (dstA == 255) {
                    uint32_t invSrcA = 256 - srcA;
                    // ブレンド結果を直接255で正規化（シフトのみ）
                    dstPixel[0] = static_cast<uint8_t>(
                        (srcPixel[0] * srcA + dstPixel[0] * invSrcA) >> 8);
                    dstPixel[1] = static_cast<uint8_t>(
                        (srcPixel[1] * srcA + dstPixel[1] * invSrcA) >> 8);
                    dstPixel[2] = static_cast<uint8_t>(
                        (srcPixel[2] * srcA + dstPixel[2] * invSrcA) >> 8);
                    dstPixel[3] = 255;
                    continue;
                }

                // 半透明ブレンド（ストレート形式でのover演算）
                // 256スケールで計算
                uint32_t invSrcA = 256 - srcA;  // 1 - srcA (256 scale)

                // プリマルチプライド相当に変換してブレンド
                uint32_t srcR_pm = srcPixel[0] * srcA;
                uint32_t srcG_pm = srcPixel[1] * srcA;
                uint32_t srcB_pm = srcPixel[2] * srcA;

                uint32_t dstR_pm = dstPixel[0] * dstA;
                uint32_t dstG_pm = dstPixel[1] * dstA;
                uint32_t dstB_pm = dstPixel[2] * dstA;

                // ブレンド結果（プリマルチプライド）
                uint32_t outR_pm = srcR_pm + ((dstR_pm * invSrcA) >> 8);
                uint32_t outG_pm = srcG_pm + ((dstG_pm * invSrcA) >> 8);
                uint32_t outB_pm = srcB_pm + ((dstB_pm * invSrcA) >> 8);
                uint32_t outA = srcA + ((dstA * invSrcA) >> 8);

                // ストレートに戻す（逆数テーブルで除算回避）
                uint32_t inv = invAlphaTable[outA];
                dstPixel[0] = static_cast<uint8_t>((outR_pm * inv) >> 24);
                dstPixel[1] = static_cast<uint8_t>((outG_pm * inv) >> 24);
                dstPixel[2] = static_cast<uint8_t>((outB_pm * inv) >> 24);
                dstPixel[3] = static_cast<uint8_t>(outA);
            }
        }
        return;
    }

#if 1  // RGBA16_Premultiplied サポート有効
    // 閾値定数（16bit用）
    constexpr uint16_t ALPHA_TRANS_MAX_16 = RGBA16Premul::ALPHA_TRANSPARENT_MAX;
    constexpr uint16_t ALPHA_OPAQUE_MIN_16 = RGBA16Premul::ALPHA_OPAQUE_MIN;

    // 16bit Premultiplied 同士のブレンド
    for (int y = yStart; y < yEnd; y++) {
        const uint16_t* srcRow = static_cast<const uint16_t*>(src.pixelAt(0, y));
        uint16_t* dstRow = static_cast<uint16_t*>(dst.pixelAt(0, y + offsetY));

        for (int x = xStart; x < xEnd; x++) {
            const uint16_t* srcPixel = srcRow + x * 4;
            uint16_t* dstPixel = dstRow + (x + offsetX) * 4;

            uint16_t srcA = srcPixel[3];
            uint16_t dstA = dstPixel[3];

            // dst に描画があり src が透明 → スキップ
            if (dstA > ALPHA_TRANS_MAX_16 && srcA <= ALPHA_TRANS_MAX_16) continue;

            uint16_t srcR = srcPixel[0];
            uint16_t srcG = srcPixel[1];
            uint16_t srcB = srcPixel[2];

            // 半透明ブレンド（dst に描画があり src が半透明の場合）
            if (dstA > ALPHA_TRANS_MAX_16 && srcA < ALPHA_OPAQUE_MIN_16) {
                uint16_t invSrcA = ALPHA_OPAQUE_MIN_16 - srcA;
                srcR += (dstPixel[0] * invSrcA) >> 16;
                srcG += (dstPixel[1] * invSrcA) >> 16;
                srcB += (dstPixel[2] * invSrcA) >> 16;
                srcA += (dstA * invSrcA) >> 16;
            }

            dstPixel[0] = srcR;
            dstPixel[1] = srcG;
            dstPixel[2] = srcB;
            dstPixel[3] = srcA;
        }
    }
#endif
}

} // namespace blend
} // namespace FLEXIMG_NAMESPACE
