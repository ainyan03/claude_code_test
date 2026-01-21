/**
 * @file precision_analysis.cpp
 * @brief ピクセルフォーマット変換の精度損失分析ツール
 *
 * 用途:
 * - toPremul/fromPremul変換のラウンドトリップ精度を評価
 * - invUnpremulTableの各方式（floor/round/ceil）の精度比較
 * - SWAR最適化による精度への影響を検証
 *
 * ビルド方法:
 *   cd fleximg/test
 *   g++ -std=c++17 -O2 precision_analysis.cpp -o precision_analysis
 *
 * 実行:
 *   ./precision_analysis
 *
 * 結果の見方:
 * - 誤差0が100%であればラウンドトリップで値が完全に保持される
 * - 誤差-1は元の値より1小さくなる（floor方向への丸め誤差）
 * - 255超はオーバーフロー（クランプ処理が必要）
 */

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <algorithm>

// 逆数計算関数
// 方式1: 切り捨て（現在のpixel_format.cppの実装）
constexpr uint16_t calcInvUnpremul_floor(int a) {
    return (a == 0) ? 0 : static_cast<uint16_t>(65536u / static_cast<uint32_t>(a + 1));
}

// 方式2: 四捨五入
constexpr uint16_t calcInvUnpremul_round(int a) {
    if (a == 0) return 0;
    uint32_t divisor = static_cast<uint32_t>(a + 1);
    return static_cast<uint16_t>((65536u + divisor / 2) / divisor);
}

// 方式3: 切り上げ (ceil)
constexpr uint16_t calcInvUnpremul_ceil(int a) {
    if (a == 0) return 0;
    uint32_t divisor = static_cast<uint32_t>(a + 1);
    return static_cast<uint16_t>((65536u + divisor - 1) / divisor);
}

// 現在使用中（切り捨て）
constexpr uint16_t calcInvUnpremul(int a) {
    return calcInvUnpremul_floor(a);
}

constexpr uint16_t invUnpremulTable[256] = {
    calcInvUnpremul(0), calcInvUnpremul(1), calcInvUnpremul(2), calcInvUnpremul(3),
    calcInvUnpremul(4), calcInvUnpremul(5), calcInvUnpremul(6), calcInvUnpremul(7),
    calcInvUnpremul(8), calcInvUnpremul(9), calcInvUnpremul(10), calcInvUnpremul(11),
    calcInvUnpremul(12), calcInvUnpremul(13), calcInvUnpremul(14), calcInvUnpremul(15),
    calcInvUnpremul(16), calcInvUnpremul(17), calcInvUnpremul(18), calcInvUnpremul(19),
    calcInvUnpremul(20), calcInvUnpremul(21), calcInvUnpremul(22), calcInvUnpremul(23),
    calcInvUnpremul(24), calcInvUnpremul(25), calcInvUnpremul(26), calcInvUnpremul(27),
    calcInvUnpremul(28), calcInvUnpremul(29), calcInvUnpremul(30), calcInvUnpremul(31),
    calcInvUnpremul(32), calcInvUnpremul(33), calcInvUnpremul(34), calcInvUnpremul(35),
    calcInvUnpremul(36), calcInvUnpremul(37), calcInvUnpremul(38), calcInvUnpremul(39),
    calcInvUnpremul(40), calcInvUnpremul(41), calcInvUnpremul(42), calcInvUnpremul(43),
    calcInvUnpremul(44), calcInvUnpremul(45), calcInvUnpremul(46), calcInvUnpremul(47),
    calcInvUnpremul(48), calcInvUnpremul(49), calcInvUnpremul(50), calcInvUnpremul(51),
    calcInvUnpremul(52), calcInvUnpremul(53), calcInvUnpremul(54), calcInvUnpremul(55),
    calcInvUnpremul(56), calcInvUnpremul(57), calcInvUnpremul(58), calcInvUnpremul(59),
    calcInvUnpremul(60), calcInvUnpremul(61), calcInvUnpremul(62), calcInvUnpremul(63),
    calcInvUnpremul(64), calcInvUnpremul(65), calcInvUnpremul(66), calcInvUnpremul(67),
    calcInvUnpremul(68), calcInvUnpremul(69), calcInvUnpremul(70), calcInvUnpremul(71),
    calcInvUnpremul(72), calcInvUnpremul(73), calcInvUnpremul(74), calcInvUnpremul(75),
    calcInvUnpremul(76), calcInvUnpremul(77), calcInvUnpremul(78), calcInvUnpremul(79),
    calcInvUnpremul(80), calcInvUnpremul(81), calcInvUnpremul(82), calcInvUnpremul(83),
    calcInvUnpremul(84), calcInvUnpremul(85), calcInvUnpremul(86), calcInvUnpremul(87),
    calcInvUnpremul(88), calcInvUnpremul(89), calcInvUnpremul(90), calcInvUnpremul(91),
    calcInvUnpremul(92), calcInvUnpremul(93), calcInvUnpremul(94), calcInvUnpremul(95),
    calcInvUnpremul(96), calcInvUnpremul(97), calcInvUnpremul(98), calcInvUnpremul(99),
    calcInvUnpremul(100), calcInvUnpremul(101), calcInvUnpremul(102), calcInvUnpremul(103),
    calcInvUnpremul(104), calcInvUnpremul(105), calcInvUnpremul(106), calcInvUnpremul(107),
    calcInvUnpremul(108), calcInvUnpremul(109), calcInvUnpremul(110), calcInvUnpremul(111),
    calcInvUnpremul(112), calcInvUnpremul(113), calcInvUnpremul(114), calcInvUnpremul(115),
    calcInvUnpremul(116), calcInvUnpremul(117), calcInvUnpremul(118), calcInvUnpremul(119),
    calcInvUnpremul(120), calcInvUnpremul(121), calcInvUnpremul(122), calcInvUnpremul(123),
    calcInvUnpremul(124), calcInvUnpremul(125), calcInvUnpremul(126), calcInvUnpremul(127),
    calcInvUnpremul(128), calcInvUnpremul(129), calcInvUnpremul(130), calcInvUnpremul(131),
    calcInvUnpremul(132), calcInvUnpremul(133), calcInvUnpremul(134), calcInvUnpremul(135),
    calcInvUnpremul(136), calcInvUnpremul(137), calcInvUnpremul(138), calcInvUnpremul(139),
    calcInvUnpremul(140), calcInvUnpremul(141), calcInvUnpremul(142), calcInvUnpremul(143),
    calcInvUnpremul(144), calcInvUnpremul(145), calcInvUnpremul(146), calcInvUnpremul(147),
    calcInvUnpremul(148), calcInvUnpremul(149), calcInvUnpremul(150), calcInvUnpremul(151),
    calcInvUnpremul(152), calcInvUnpremul(153), calcInvUnpremul(154), calcInvUnpremul(155),
    calcInvUnpremul(156), calcInvUnpremul(157), calcInvUnpremul(158), calcInvUnpremul(159),
    calcInvUnpremul(160), calcInvUnpremul(161), calcInvUnpremul(162), calcInvUnpremul(163),
    calcInvUnpremul(164), calcInvUnpremul(165), calcInvUnpremul(166), calcInvUnpremul(167),
    calcInvUnpremul(168), calcInvUnpremul(169), calcInvUnpremul(170), calcInvUnpremul(171),
    calcInvUnpremul(172), calcInvUnpremul(173), calcInvUnpremul(174), calcInvUnpremul(175),
    calcInvUnpremul(176), calcInvUnpremul(177), calcInvUnpremul(178), calcInvUnpremul(179),
    calcInvUnpremul(180), calcInvUnpremul(181), calcInvUnpremul(182), calcInvUnpremul(183),
    calcInvUnpremul(184), calcInvUnpremul(185), calcInvUnpremul(186), calcInvUnpremul(187),
    calcInvUnpremul(188), calcInvUnpremul(189), calcInvUnpremul(190), calcInvUnpremul(191),
    calcInvUnpremul(192), calcInvUnpremul(193), calcInvUnpremul(194), calcInvUnpremul(195),
    calcInvUnpremul(196), calcInvUnpremul(197), calcInvUnpremul(198), calcInvUnpremul(199),
    calcInvUnpremul(200), calcInvUnpremul(201), calcInvUnpremul(202), calcInvUnpremul(203),
    calcInvUnpremul(204), calcInvUnpremul(205), calcInvUnpremul(206), calcInvUnpremul(207),
    calcInvUnpremul(208), calcInvUnpremul(209), calcInvUnpremul(210), calcInvUnpremul(211),
    calcInvUnpremul(212), calcInvUnpremul(213), calcInvUnpremul(214), calcInvUnpremul(215),
    calcInvUnpremul(216), calcInvUnpremul(217), calcInvUnpremul(218), calcInvUnpremul(219),
    calcInvUnpremul(220), calcInvUnpremul(221), calcInvUnpremul(222), calcInvUnpremul(223),
    calcInvUnpremul(224), calcInvUnpremul(225), calcInvUnpremul(226), calcInvUnpremul(227),
    calcInvUnpremul(228), calcInvUnpremul(229), calcInvUnpremul(230), calcInvUnpremul(231),
    calcInvUnpremul(232), calcInvUnpremul(233), calcInvUnpremul(234), calcInvUnpremul(235),
    calcInvUnpremul(236), calcInvUnpremul(237), calcInvUnpremul(238), calcInvUnpremul(239),
    calcInvUnpremul(240), calcInvUnpremul(241), calcInvUnpremul(242), calcInvUnpremul(243),
    calcInvUnpremul(244), calcInvUnpremul(245), calcInvUnpremul(246), calcInvUnpremul(247),
    calcInvUnpremul(248), calcInvUnpremul(249), calcInvUnpremul(250), calcInvUnpremul(251),
    calcInvUnpremul(252), calcInvUnpremul(253), calcInvUnpremul(254), calcInvUnpremul(255)
};

// toPremul: RGBA8 → RGBA16_Premul（SWAR版 - 現在のpixel_format.cppの実装）
// 注: アルファチャンネルに a を使用しているが、正しくは 255 を使うべき
void toPremul_swar_current(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                   uint16_t& r16, uint16_t& g16, uint16_t& b16, uint16_t& a16) {
    uint_fast16_t a_tmp = a + 1;
    uint32_t rg = (r + (static_cast<uint32_t>(g) << 16)) * a_tmp;
    uint32_t ba = (b + (static_cast<uint32_t>(a) << 16)) * a_tmp;  // バグ: a を使用
    r16 = static_cast<uint16_t>(rg & 0xFFFF);
    g16 = static_cast<uint16_t>(rg >> 16);
    b16 = static_cast<uint16_t>(ba & 0xFFFF);
    a16 = static_cast<uint16_t>(ba >> 16);
}

// toPremul: RGBA8 → RGBA16_Premul（修正版 - 255を使用）
void toPremul_swar_fixed(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                   uint16_t& r16, uint16_t& g16, uint16_t& b16, uint16_t& a16) {
    uint_fast16_t a_tmp = a + 1;
    uint32_t rg = (r + (static_cast<uint32_t>(g) << 16)) * a_tmp;
    uint32_t ba = (b + (static_cast<uint32_t>(255) << 16)) * a_tmp;  // 修正: 255 を使用
    r16 = static_cast<uint16_t>(rg & 0xFFFF);
    g16 = static_cast<uint16_t>(rg >> 16);
    b16 = static_cast<uint16_t>(ba & 0xFFFF);
    a16 = static_cast<uint16_t>(ba >> 16);
}

// toPremul: リファレンス実装
void toPremul_ref(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                  uint16_t& r16, uint16_t& g16, uint16_t& b16, uint16_t& a16) {
    uint16_t a_tmp = a + 1;
    r16 = r * a_tmp;
    g16 = g * a_tmp;
    b16 = b * a_tmp;
    a16 = a * a_tmp;
}

// fromPremul: RGBA16_Premul → RGBA8（逆数テーブル版）
void fromPremul_table(uint16_t r16, uint16_t g16, uint16_t b16, uint16_t a16,
                      uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    a = static_cast<uint8_t>(a16 >> 8);
    uint32_t inv = invUnpremulTable[a];
    r = static_cast<uint8_t>((r16 * inv) >> 16);
    g = static_cast<uint8_t>((g16 * inv) >> 16);
    b = static_cast<uint8_t>((b16 * inv) >> 16);
}

// fromPremul: 除算版（正確な実装）
void fromPremul_div(uint16_t r16, uint16_t g16, uint16_t b16, uint16_t a16,
                    uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    a = static_cast<uint8_t>(a16 >> 8);
    uint16_t a_tmp = a + 1;
    r = static_cast<uint8_t>(r16 / a_tmp);
    g = static_cast<uint8_t>(g16 / a_tmp);
    b = static_cast<uint8_t>(b16 / a_tmp);
}

int main() {
    printf("=== PixelFormat Precision Analysis ===\n\n");

    // 1. toPremul: SWAR vs Reference
    printf("1. toPremul精度（SWAR vs Reference）\n");
    {
        int maxDiff = 0;
        int diffCount = 0;
        for (int a = 0; a <= 255; ++a) {
            for (int c = 0; c <= 255; ++c) {
                uint16_t swar_r, swar_g, swar_b, swar_a;
                uint16_t ref_r, ref_g, ref_b, ref_a;
                toPremul_swar_current(c, c, c, a, swar_r, swar_g, swar_b, swar_a);
                toPremul_ref(c, c, c, a, ref_r, ref_g, ref_b, ref_a);

                int diff = std::abs(static_cast<int>(swar_r) - static_cast<int>(ref_r));
                if (diff > 0) diffCount++;
                maxDiff = std::max(maxDiff, diff);
            }
        }
        printf("   最大誤差: %d, 誤差ケース数: %d / %d\n", maxDiff, diffCount, 256*256);
    }

    // 2. fromPremul: テーブル vs 除算（修正版の a16 = 255 * (a+1) を使用）
    printf("\n2. fromPremul精度（テーブル vs 除算）修正版実装\n");
    {
        int maxDiff = 0;
        int diffCount = 0;
        int worstA = 0, worstC = 0;
        int totalTests = 0;
        int tableHigher = 0;  // テーブル > 除算
        int tableLower = 0;   // テーブル < 除算

        for (int a = 1; a <= 255; ++a) {
            uint16_t a_tmp = a + 1;
            for (int c = 0; c <= 255; ++c) {
                // 修正版: a16 = 255 * (a+1)
                uint16_t c16 = static_cast<uint16_t>(c * a_tmp);
                uint16_t a16 = static_cast<uint16_t>(255 * a_tmp);

                uint8_t table_r, table_g, table_b, table_a;
                uint8_t div_r, div_g, div_b, div_a;

                fromPremul_table(c16, c16, c16, a16, table_r, table_g, table_b, table_a);
                fromPremul_div(c16, c16, c16, a16, div_r, div_g, div_b, div_a);

                int signedDiff = static_cast<int>(table_r) - static_cast<int>(div_r);
                int diff = std::abs(signedDiff);
                if (diff > 0) {
                    diffCount++;
                    if (signedDiff > 0) tableHigher++;
                    else tableLower++;
                }
                if (diff > maxDiff) {
                    maxDiff = diff;
                    worstA = a;
                    worstC = c;
                }
                totalTests++;
            }
        }
        printf("   テーブル vs 除算: 最大誤差=%d (a=%d, c=%d), 誤差発生=%d / %d\n",
               maxDiff, worstA, worstC, diffCount, totalTests);
        printf("   誤差方向: テーブル>除算=%d, テーブル<除算=%d\n", tableHigher, tableLower);
    }

    // 2b. 除算版のラウンドトリップ誤差（理論上の最小誤差）
    printf("\n2b. 除算版ラウンドトリップ誤差（下位8bit保持の場合の誤差）\n");
    {
        int maxDiff = 0;
        int diffCount = 0;
        int diffHistogram[10] = {0};
        int totalTests = 0;

        for (int a = 1; a <= 255; ++a) {
            uint16_t a_tmp = a + 1;
            for (int c = 0; c <= 255; ++c) {
                // toPremul（正確）
                uint16_t c16 = static_cast<uint16_t>(c * a_tmp);
                uint16_t a16 = static_cast<uint16_t>(255 * a_tmp);

                // fromPremul（除算版、正確）
                uint8_t a8 = static_cast<uint8_t>(a16 >> 8);
                uint8_t result_c = static_cast<uint8_t>(c16 / (a8 + 1));

                int diff = std::abs(static_cast<int>(result_c) - c);
                diffHistogram[std::min(diff, 9)]++;
                if (diff > 0) diffCount++;
                maxDiff = std::max(maxDiff, diff);
                totalTests++;
            }
        }
        printf("   最大誤差: %d, 誤差発生: %d / %d\n", maxDiff, diffCount, totalTests);
        printf("   誤差分布:\n");
        for (int i = 0; i <= 9; ++i) {
            if (diffHistogram[i] > 0) {
                printf("     誤差%d%s: %d ケース (%.2f%%)\n",
                       i, i == 9 ? "+" : "", diffHistogram[i],
                       100.0 * diffHistogram[i] / totalTests);
            }
        }
    }

    // 3. ラウンドトリップ: RGBA8 → RGBA16_Premul → RGBA8
    printf("\n3. ラウンドトリップ精度（RGBA8 → RGBA16_Premul → RGBA8）\n");

    // 3a. 現在の実装（バグあり: a16 = a * (a+1)）
    printf("   3a. 現在の実装（a16 = a * (a+1) - バグあり）:\n");
    {
        int maxDiff = 0;
        int diffCount = 0;
        int worstA = 0, worstC = 0;
        int diffHistogram[10] = {0};
        int totalTests = 0;

        for (int a = 1; a <= 255; ++a) {
            for (int c = 0; c <= 255; ++c) {
                uint16_t r16, g16, b16, a16;
                toPremul_swar_current(c, c, c, a, r16, g16, b16, a16);

                uint8_t result_r, result_g, result_b, result_a;
                fromPremul_table(r16, g16, b16, a16, result_r, result_g, result_b, result_a);

                int diff = std::abs(static_cast<int>(result_r) - c);
                diffHistogram[std::min(diff, 9)]++;
                if (diff > 0) diffCount++;
                if (diff > maxDiff) {
                    maxDiff = diff;
                    worstA = a;
                    worstC = c;
                }
                totalTests++;
            }
        }
        printf("       最大誤差: %d (a=%d, c=%d)\n", maxDiff, worstA, worstC);
        printf("       誤差分布（全ケース %d）:\n", totalTests);
        for (int i = 0; i <= 9; ++i) {
            if (diffHistogram[i] > 0) {
                printf("         誤差%d%s: %d ケース (%.2f%%)\n",
                       i, i == 9 ? "+" : "", diffHistogram[i],
                       100.0 * diffHistogram[i] / totalTests);
            }
        }
    }

    // 3b. 修正版の実装（a16 = 255 * (a+1)）
    printf("   3b. 修正版の実装（a16 = 255 * (a+1) - 正しい設計）:\n");
    {
        int maxDiff = 0;
        int diffCount = 0;
        int worstA = 0, worstC = 0;
        int diffHistogram[10] = {0};
        int totalTests = 0;
        int plusErrors = 0;   // result > original
        int minusErrors = 0;  // result < original

        for (int a = 1; a <= 255; ++a) {
            for (int c = 0; c <= 255; ++c) {
                uint16_t r16, g16, b16, a16;
                toPremul_swar_fixed(c, c, c, a, r16, g16, b16, a16);

                uint8_t result_r, result_g, result_b, result_a;
                fromPremul_table(r16, g16, b16, a16, result_r, result_g, result_b, result_a);

                int signedDiff = static_cast<int>(result_r) - c;
                int diff = std::abs(signedDiff);
                diffHistogram[std::min(diff, 9)]++;
                if (diff > 0) {
                    diffCount++;
                    if (signedDiff > 0) plusErrors++;
                    else minusErrors++;
                }
                if (diff > maxDiff) {
                    maxDiff = diff;
                    worstA = a;
                    worstC = c;
                }
                totalTests++;
            }
        }
        printf("       最大誤差: %d (a=%d, c=%d)\n", maxDiff, worstA, worstC);
        printf("       誤差方向: +%d / -%d (プラス/マイナス)\n", plusErrors, minusErrors);
        printf("       誤差分布（全ケース %d）:\n", totalTests);
        for (int i = 0; i <= 9; ++i) {
            if (diffHistogram[i] > 0) {
                printf("         誤差%d%s: %d ケース (%.2f%%)\n",
                       i, i == 9 ? "+" : "", diffHistogram[i],
                       100.0 * diffHistogram[i] / totalTests);
            }
        }
    }

    // 3c. 比較: 現在 vs 修正版
    printf("\n   3c. 現在実装 vs 修正版の比較:\n");
    {
        int a = 253, c = 255;

        // 現在の実装
        uint16_t cur_r16, cur_g16, cur_b16, cur_a16;
        toPremul_swar_current(c, c, c, a, cur_r16, cur_g16, cur_b16, cur_a16);
        uint8_t cur_r, cur_g, cur_b, cur_a;
        fromPremul_table(cur_r16, cur_g16, cur_b16, cur_a16, cur_r, cur_g, cur_b, cur_a);

        // 修正版
        uint16_t fix_r16, fix_g16, fix_b16, fix_a16;
        toPremul_swar_fixed(c, c, c, a, fix_r16, fix_g16, fix_b16, fix_a16);
        uint8_t fix_r, fix_g, fix_b, fix_a;
        fromPremul_table(fix_r16, fix_g16, fix_b16, fix_a16, fix_r, fix_g, fix_b, fix_a);

        printf("       入力: r=%d, g=%d, b=%d, a=%d\n", c, c, c, a);
        printf("       現在版:\n");
        printf("         Premul: r16=%d, a16=%d (a16>>8=%d)\n", cur_r16, cur_a16, cur_a16>>8);
        printf("         復元:  r=%d, a=%d (誤差=%d)\n", cur_r, cur_a, std::abs(c - cur_r));
        printf("       修正版:\n");
        printf("         Premul: r16=%d, a16=%d (a16>>8=%d)\n", fix_r16, fix_a16, fix_a16>>8);
        printf("         復元:  r=%d, a=%d (誤差=%d)\n", fix_r, fix_a, std::abs(c - fix_r));
    }

    // 4. a=0 の特殊ケース
    printf("\n4. a=0（透明）の特殊ケース\n");
    {
        for (int c = 0; c <= 255; c += 51) {  // 0, 51, 102, 153, 204, 255
            uint16_t r16, g16, b16, a16;
            toPremul_swar_current(c, c, c, 0, r16, g16, b16, a16);

            uint8_t result_r, result_g, result_b, result_a;
            fromPremul_table(r16, g16, b16, a16, result_r, result_g, result_b, result_a);

            printf("   入力(c=%3d, a=0) → Premul(r16=%5d, a16=%5d) → 出力(r=%3d, a=%d)\n",
                   c, r16, a16, result_r, result_a);
        }
    }

    // 5. blendUnderの8bit精度変換による誤差
    printf("\n5. blendUnder: 8bit精度変換による誤差（srcRG >> 8）\n");
    {
        int maxDiff = 0;
        int diffCount = 0;

        for (int a = 1; a <= 255; ++a) {
            for (int c = 0; c <= 255; ++c) {
                // toPremul後の16bit値
                uint16_t a_tmp = a + 1;
                uint32_t rg_32 = c * a_tmp;  // 正確な16bit値

                // 8bit精度に変換後の値
                uint16_t rg_8bit = (rg_32 >> 8) & 0xFF;  // 上位8bit

                // 16bit精度との差（8bitスケール）
                uint16_t rg_ideal = static_cast<uint16_t>(rg_32 >> 8);  // 正確な上位8bit
                int diff = std::abs(static_cast<int>(rg_8bit) - static_cast<int>(rg_ideal));

                if (diff > 0) diffCount++;
                maxDiff = std::max(maxDiff, diff);
            }
        }
        printf("   8bit精度変換誤差: 最大%d, 誤差ケース数: %d\n", maxDiff, diffCount);
        printf("   （注: >>8とマスクで上位8bitを取り出すだけなので誤差0は正常）\n");
    }

    // 6. under合成の精度
    printf("\n6. under合成精度（dst + src * (1 - dstA)）\n");
    {
        int maxDiff = 0;
        int totalTests = 0;

        // 代表的なケースをテスト
        int testAlphas[] = {1, 64, 128, 192, 254};
        int testColors[] = {0, 64, 128, 192, 255};

        for (int srcA : testAlphas) {
            for (int srcC : testColors) {
                for (int dstA : testAlphas) {
                    for (int dstC : testColors) {
                        // SWAR実装
                        uint16_t a_tmp = srcA + 1;
                        uint32_t srcRG = srcC * a_tmp;
                        uint16_t invDstA8 = 255 - dstA;

                        // dstの16bit値を模擬（既存のPremul値）
                        uint16_t dstR16 = static_cast<uint16_t>(dstC * (dstA + 1));

                        // SWAR合成
                        uint32_t result_swar = dstR16 + ((srcRG >> 8) & 0xFF) * invDstA8;

                        // 理想的な合成（浮動小数点）
                        double srcPremul = srcC * (srcA / 255.0);
                        double dstPremul = dstC * (dstA / 255.0);
                        double idealResult = dstPremul + srcPremul * (1.0 - dstA / 255.0);
                        uint16_t ideal16 = static_cast<uint16_t>(idealResult * 256.0);

                        int diff = std::abs(static_cast<int>(result_swar) - static_cast<int>(ideal16));
                        maxDiff = std::max(maxDiff, diff);
                        totalTests++;
                    }
                }
            }
        }
        printf("   最大誤差（16bit単位）: %d / 65536\n", maxDiff);
        printf("   （%d ケーステスト）\n", totalTests);
    }

    // 7. テーブル改良案の比較
    printf("\n7. テーブル改良案の比較\n");
    {
        // 方式A: 切り捨てテーブル + 切り捨て演算（現在）
        // 方式B: 四捨五入テーブル + 切り捨て演算
        // 方式C: 切り捨てテーブル + 四捨五入演算
        // 方式D: 四捨五入テーブル + 四捨五入演算
        // 方式E: 切り上げテーブル + 切り捨て演算
        // 方式F: 切り上げテーブル + 四捨五入演算

        struct Stats {
            int exact = 0;
            int plus1 = 0;
            int minus1 = 0;
            int other = 0;
            int overflow = 0;  // 255を超えた回数
        };

        Stats statsA, statsB, statsC, statsD, statsE, statsF;
        int totalTests = 0;

        for (int a = 1; a <= 255; ++a) {
            uint16_t a_tmp = a + 1;
            uint16_t inv_floor = calcInvUnpremul_floor(a);
            uint16_t inv_round = calcInvUnpremul_round(a);
            uint16_t inv_ceil = calcInvUnpremul_ceil(a);

            for (int c = 0; c <= 255; ++c) {
                uint16_t c16 = static_cast<uint16_t>(c * a_tmp);

                // 方式A: 切り捨て + 切り捨て
                uint32_t rawA = c16 * inv_floor;
                int resultA = static_cast<int>(rawA >> 16);

                // 方式B: 四捨五入テーブル + 切り捨て
                uint32_t rawB = c16 * inv_round;
                int resultB = static_cast<int>(rawB >> 16);

                // 方式C: 切り捨てテーブル + 四捨五入演算
                int resultC = static_cast<int>((rawA + 32768) >> 16);

                // 方式D: 四捨五入テーブル + 四捨五入演算
                int resultD = static_cast<int>((rawB + 32768) >> 16);

                // 方式E: 切り上げテーブル + 切り捨て演算
                uint32_t rawE = c16 * inv_ceil;
                int resultE = static_cast<int>(rawE >> 16);

                // 方式F: 切り上げテーブル + 四捨五入演算
                int resultF = static_cast<int>((rawE + 32768) >> 16);

                auto updateStats = [&](Stats& s, int result) {
                    int diff = result - c;
                    if (result > 255) s.overflow++;
                    if (diff == 0) s.exact++;
                    else if (diff == 1) s.plus1++;
                    else if (diff == -1) s.minus1++;
                    else s.other++;
                };

                updateStats(statsA, resultA);
                updateStats(statsB, resultB);
                updateStats(statsC, resultC);
                updateStats(statsD, resultD);
                updateStats(statsE, resultE);
                updateStats(statsF, resultF);
                totalTests++;
            }
        }

        printf("   全%dケースでの誤差分布:\n", totalTests);
        printf("   ┌─────────────────────────────────────────────────────────────────┐\n");
        printf("   │ 方式                        │ 誤差0   │ +1     │ -1     │ 255超 │\n");
        printf("   ├─────────────────────────────────────────────────────────────────┤\n");
        printf("   │ A: floor+floor (現在)       │ %5.1f%% │ %5.1f%% │ %5.1f%% │ %5d │\n",
               100.0*statsA.exact/totalTests, 100.0*statsA.plus1/totalTests,
               100.0*statsA.minus1/totalTests, statsA.overflow);
        printf("   │ B: round+floor              │ %5.1f%% │ %5.1f%% │ %5.1f%% │ %5d │\n",
               100.0*statsB.exact/totalTests, 100.0*statsB.plus1/totalTests,
               100.0*statsB.minus1/totalTests, statsB.overflow);
        printf("   │ C: floor+round              │ %5.1f%% │ %5.1f%% │ %5.1f%% │ %5d │\n",
               100.0*statsC.exact/totalTests, 100.0*statsC.plus1/totalTests,
               100.0*statsC.minus1/totalTests, statsC.overflow);
        printf("   │ D: round+round              │ %5.1f%% │ %5.1f%% │ %5.1f%% │ %5d │\n",
               100.0*statsD.exact/totalTests, 100.0*statsD.plus1/totalTests,
               100.0*statsD.minus1/totalTests, statsD.overflow);
        printf("   │ E: ceil+floor               │ %5.1f%% │ %5.1f%% │ %5.1f%% │ %5d │\n",
               100.0*statsE.exact/totalTests, 100.0*statsE.plus1/totalTests,
               100.0*statsE.minus1/totalTests, statsE.overflow);
        printf("   │ F: ceil+round               │ %5.1f%% │ %5.1f%% │ %5.1f%% │ %5d │\n",
               100.0*statsF.exact/totalTests, 100.0*statsF.plus1/totalTests,
               100.0*statsF.minus1/totalTests, statsF.overflow);
        printf("   └─────────────────────────────────────────────────────────────────┘\n");

        if (statsE.overflow > 0 || statsF.overflow > 0) {
            printf("   ※ ceil版でオーバーフロー発生（クランプ処理が必要）\n");
        }
    }

    printf("\n=== 分析完了 ===\n");
    return 0;
}
