// fleximg blendUnderPremul/blendUnderStraight Verification Tests
// ピクセルフォーマット別のblendUnder関数の検証テスト
//
// 検証方針:
//   直接パス: srcFormat->blendUnderPremul(dst, src, ...)
//   参照パス: srcFormat->toPremul(tmp, src, ...) + RGBA16Premul->blendUnderPremul(dst, tmp, ...)
//   両者の結果が一致することを検証
//
// テストパターン:
//   - 各チャネルの単独スイープ（0〜最大値）
//   - 特殊値（黒、白、グレー）
//   - dst/srcアルファの代表値組み合わせ

#include "doctest.h"
#include <cstring>
#include <vector>
#include <sstream>
#include <iomanip>

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/image/pixel_format.h"

using namespace fleximg;

// =============================================================================
// Test Utilities
// =============================================================================

namespace {

// 代表的なアルファ値（8値）
constexpr uint8_t kTestAlphas[] = {0, 1, 64, 127, 128, 192, 254, 255};
constexpr size_t kNumTestAlphas = sizeof(kTestAlphas) / sizeof(kTestAlphas[0]);

// dst色パターン
struct DstColorPattern {
    uint8_t r, g, b;
    const char* name;
};

constexpr DstColorPattern kDstColors[] = {
    {0, 0, 0, "black"},
    {255, 255, 255, "white"},
    {128, 128, 128, "gray"},
    {100, 150, 200, "mixed"},
};
constexpr size_t kNumDstColors = sizeof(kDstColors) / sizeof(kDstColors[0]);

// RGBA16_Premultiplied形式のdstバッファを初期化
void initDstPremul(uint16_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    // 8bit→16bit変換（上位バイトに配置）
    // Premultiplied: 色 = 色 * (α/255)、ここでは簡易的にα=255想定で色をそのまま配置
    // 実際のPremul変換: color16 = color8 * (a + 1)
    uint16_t a16 = static_cast<uint16_t>(a) * 256;  // 0-255 → 0-65280
    if (a == 0) {
        dst[0] = 0;
        dst[1] = 0;
        dst[2] = 0;
        dst[3] = 0;
    } else {
        // Premultiplied: color = color * alpha / 255
        dst[0] = static_cast<uint16_t>((static_cast<uint32_t>(r) * a16) / 255);
        dst[1] = static_cast<uint16_t>((static_cast<uint32_t>(g) * a16) / 255);
        dst[2] = static_cast<uint16_t>((static_cast<uint32_t>(b) * a16) / 255);
        dst[3] = a16;
    }
}

// RGBA8_Straight形式のdstバッファを初期化
void initDstStraight(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    dst[0] = r;
    dst[1] = g;
    dst[2] = b;
    dst[3] = a;
}

// RGBA16バッファの比較（許容誤差付き）
bool compareRGBA16(const uint16_t* a, const uint16_t* b, uint16_t tolerance = 0) {
    for (int i = 0; i < 4; i++) {
        int diff = static_cast<int>(a[i]) - static_cast<int>(b[i]);
        if (diff < 0) diff = -diff;
        if (diff > tolerance) return false;
    }
    return true;
}

// RGBA8バッファの比較（許容誤差付き）
bool compareRGBA8(const uint8_t* a, const uint8_t* b, uint8_t tolerance = 0) {
    for (int i = 0; i < 4; i++) {
        int diff = static_cast<int>(a[i]) - static_cast<int>(b[i]);
        if (diff < 0) diff = -diff;
        if (diff > tolerance) return false;
    }
    return true;
}

// RGBA16バッファを文字列化（デバッグ用）
std::string rgba16ToString(const uint16_t* p) {
    std::ostringstream oss;
    oss << "(" << p[0] << "," << p[1] << "," << p[2] << "," << p[3] << ")";
    return oss.str();
}

// RGBA8バッファを文字列化（デバッグ用）
std::string rgba8ToString(const uint8_t* p) {
    std::ostringstream oss;
    oss << "(" << static_cast<int>(p[0]) << ","
        << static_cast<int>(p[1]) << ","
        << static_cast<int>(p[2]) << ","
        << static_cast<int>(p[3]) << ")";
    return oss.str();
}

// =============================================================================
// RGB332 Encoding/Decoding
// =============================================================================

uint8_t encodeRGB332(uint8_t r3, uint8_t g3, uint8_t b2) {
    return static_cast<uint8_t>((r3 << 5) | (g3 << 2) | b2);
}

// =============================================================================
// RGB565 Encoding/Decoding
// =============================================================================

uint16_t encodeRGB565(uint8_t r5, uint8_t g6, uint8_t b5) {
    return static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
}

// RGB565_BE: バイトスワップ
void encodeRGB565_BE(uint8_t* dst, uint8_t r5, uint8_t g6, uint8_t b5) {
    uint16_t le = encodeRGB565(r5, g6, b5);
    dst[0] = static_cast<uint8_t>(le >> 8);
    dst[1] = static_cast<uint8_t>(le & 0xFF);
}

// =============================================================================
// blendUnderPremul Verification Framework
// =============================================================================

// 単一ピクセルのblendUnderPremul検証
// 直接パス vs 参照パス（toPremul + RGBA16Premul::blendUnderPremul）
bool verifyBlendUnderPremul(
    PixelFormatID srcFormat,
    const void* srcPixel,
    uint8_t dstR, uint8_t dstG, uint8_t dstB, uint8_t dstA,
    std::string& errorMsg,
    uint16_t tolerance = 0)
{
    // dst初期化（両方同じ値で開始）
    uint16_t dstDirect[4];
    uint16_t dstReference[4];
    initDstPremul(dstDirect, dstR, dstG, dstB, dstA);
    initDstPremul(dstReference, dstR, dstG, dstB, dstA);

    // 直接パス: srcFormat->blendUnderPremul
    if (srcFormat->blendUnderPremul) {
        srcFormat->blendUnderPremul(dstDirect, srcPixel, 1, nullptr);
    } else {
        errorMsg = "blendUnderPremul not implemented for " + std::string(srcFormat->name);
        return false;
    }

    // 参照パス: srcFormat->toPremul → RGBA16Premul->blendUnderPremul
    if (srcFormat->toPremul) {
        uint16_t srcConverted[4];
        srcFormat->toPremul(srcConverted, srcPixel, 1, nullptr);
        PixelFormatIDs::RGBA16_Premultiplied->blendUnderPremul(
            dstReference, srcConverted, 1, nullptr);
    } else {
        errorMsg = "toPremul not implemented for " + std::string(srcFormat->name);
        return false;
    }

    // 比較
    if (!compareRGBA16(dstDirect, dstReference, tolerance)) {
        std::ostringstream oss;
        oss << "Mismatch for " << srcFormat->name
            << " dst=(" << static_cast<int>(dstR) << "," << static_cast<int>(dstG)
            << "," << static_cast<int>(dstB) << "," << static_cast<int>(dstA) << ")"
            << " direct=" << rgba16ToString(dstDirect)
            << " reference=" << rgba16ToString(dstReference);
        errorMsg = oss.str();
        return false;
    }

    return true;
}

// =============================================================================
// blendUnderStraight Verification Framework
// =============================================================================

// 単一ピクセルのblendUnderStraight検証
bool verifyBlendUnderStraight(
    PixelFormatID srcFormat,
    const void* srcPixel,
    uint8_t dstR, uint8_t dstG, uint8_t dstB, uint8_t dstA,
    std::string& errorMsg,
    uint8_t tolerance = 0)
{
    // dst初期化
    uint8_t dstDirect[4];
    uint8_t dstReference[4];
    initDstStraight(dstDirect, dstR, dstG, dstB, dstA);
    initDstStraight(dstReference, dstR, dstG, dstB, dstA);

    // 直接パス
    if (srcFormat->blendUnderStraight) {
        srcFormat->blendUnderStraight(dstDirect, srcPixel, 1, nullptr);
    } else {
        errorMsg = "blendUnderStraight not implemented for " + std::string(srcFormat->name);
        return false;
    }

    // 参照パス: srcFormat->toStraight → RGBA8Straight->blendUnderStraight
    if (srcFormat->toStraight) {
        uint8_t srcConverted[4];
        srcFormat->toStraight(srcConverted, srcPixel, 1, nullptr);
        PixelFormatIDs::RGBA8_Straight->blendUnderStraight(
            dstReference, srcConverted, 1, nullptr);
    } else {
        errorMsg = "toStraight not implemented for " + std::string(srcFormat->name);
        return false;
    }

    // 比較
    if (!compareRGBA8(dstDirect, dstReference, tolerance)) {
        std::ostringstream oss;
        oss << "Mismatch for " << srcFormat->name
            << " dst=(" << static_cast<int>(dstR) << "," << static_cast<int>(dstG)
            << "," << static_cast<int>(dstB) << "," << static_cast<int>(dstA) << ")"
            << " direct=" << rgba8ToString(dstDirect)
            << " reference=" << rgba8ToString(dstReference);
        errorMsg = oss.str();
        return false;
    }

    return true;
}

} // anonymous namespace

// =============================================================================
// RGB332 blendUnderPremul Tests
// =============================================================================

TEST_CASE("RGB332 blendUnderPremul verification") {
    std::string errorMsg;

    SUBCASE("R channel sweep (G=0, B=0)") {
        for (uint8_t r = 0; r < 8; r++) {
            uint8_t src = encodeRGB332(r, 0, 0);
            for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok = verifyBlendUnderPremul(
                        PixelFormatIDs::RGB332, &src,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[ai], errorMsg);
                    CHECK_MESSAGE(ok, errorMsg);
                }
            }
        }
    }

    SUBCASE("G channel sweep (R=0, B=0)") {
        for (uint8_t g = 0; g < 8; g++) {
            uint8_t src = encodeRGB332(0, g, 0);
            for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok = verifyBlendUnderPremul(
                        PixelFormatIDs::RGB332, &src,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[ai], errorMsg);
                    CHECK_MESSAGE(ok, errorMsg);
                }
            }
        }
    }

    SUBCASE("B channel sweep (R=0, G=0)") {
        for (uint8_t b = 0; b < 4; b++) {
            uint8_t src = encodeRGB332(0, 0, b);
            for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok = verifyBlendUnderPremul(
                        PixelFormatIDs::RGB332, &src,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[ai], errorMsg);
                    CHECK_MESSAGE(ok, errorMsg);
                }
            }
        }
    }

    SUBCASE("Special values (black, white)") {
        uint8_t black = encodeRGB332(0, 0, 0);
        uint8_t white = encodeRGB332(7, 7, 3);

        for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
            for (size_t ci = 0; ci < kNumDstColors; ci++) {
                bool ok1 = verifyBlendUnderPremul(
                    PixelFormatIDs::RGB332, &black,
                    kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                    kTestAlphas[ai], errorMsg);
                CHECK_MESSAGE(ok1, errorMsg);

                bool ok2 = verifyBlendUnderPremul(
                    PixelFormatIDs::RGB332, &white,
                    kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                    kTestAlphas[ai], errorMsg);
                CHECK_MESSAGE(ok2, errorMsg);
            }
        }
    }
}

// =============================================================================
// RGB565_LE blendUnderPremul Tests
// =============================================================================

TEST_CASE("RGB565_LE blendUnderPremul verification") {
    std::string errorMsg;

    SUBCASE("R channel sweep (G=0, B=0)") {
        for (uint8_t r = 0; r < 32; r++) {
            uint16_t src = encodeRGB565(r, 0, 0);
            for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok = verifyBlendUnderPremul(
                        PixelFormatIDs::RGB565_LE, &src,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[ai], errorMsg);
                    CHECK_MESSAGE(ok, errorMsg);
                }
            }
        }
    }

    SUBCASE("G channel sweep (R=0, B=0)") {
        for (uint8_t g = 0; g < 64; g++) {
            uint16_t src = encodeRGB565(0, g, 0);
            for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok = verifyBlendUnderPremul(
                        PixelFormatIDs::RGB565_LE, &src,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[ai], errorMsg);
                    CHECK_MESSAGE(ok, errorMsg);
                }
            }
        }
    }

    SUBCASE("B channel sweep (R=0, G=0)") {
        for (uint8_t b = 0; b < 32; b++) {
            uint16_t src = encodeRGB565(0, 0, b);
            for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok = verifyBlendUnderPremul(
                        PixelFormatIDs::RGB565_LE, &src,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[ai], errorMsg);
                    CHECK_MESSAGE(ok, errorMsg);
                }
            }
        }
    }

    SUBCASE("Special values") {
        uint16_t black = encodeRGB565(0, 0, 0);
        uint16_t white = encodeRGB565(31, 63, 31);

        for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
            for (size_t ci = 0; ci < kNumDstColors; ci++) {
                bool ok1 = verifyBlendUnderPremul(
                    PixelFormatIDs::RGB565_LE, &black,
                    kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                    kTestAlphas[ai], errorMsg);
                CHECK_MESSAGE(ok1, errorMsg);

                bool ok2 = verifyBlendUnderPremul(
                    PixelFormatIDs::RGB565_LE, &white,
                    kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                    kTestAlphas[ai], errorMsg);
                CHECK_MESSAGE(ok2, errorMsg);
            }
        }
    }
}

// =============================================================================
// RGB565_BE blendUnderPremul Tests
// =============================================================================

TEST_CASE("RGB565_BE blendUnderPremul verification") {
    std::string errorMsg;

    SUBCASE("R channel sweep (G=0, B=0)") {
        for (uint8_t r = 0; r < 32; r++) {
            uint8_t src[2];
            encodeRGB565_BE(src, r, 0, 0);
            for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok = verifyBlendUnderPremul(
                        PixelFormatIDs::RGB565_BE, src,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[ai], errorMsg);
                    CHECK_MESSAGE(ok, errorMsg);
                }
            }
        }
    }

    SUBCASE("G channel sweep (R=0, B=0)") {
        for (uint8_t g = 0; g < 64; g++) {
            uint8_t src[2];
            encodeRGB565_BE(src, 0, g, 0);
            for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok = verifyBlendUnderPremul(
                        PixelFormatIDs::RGB565_BE, src,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[ai], errorMsg);
                    CHECK_MESSAGE(ok, errorMsg);
                }
            }
        }
    }

    SUBCASE("B channel sweep (R=0, G=0)") {
        for (uint8_t b = 0; b < 32; b++) {
            uint8_t src[2];
            encodeRGB565_BE(src, 0, 0, b);
            for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok = verifyBlendUnderPremul(
                        PixelFormatIDs::RGB565_BE, src,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[ai], errorMsg);
                    CHECK_MESSAGE(ok, errorMsg);
                }
            }
        }
    }

    SUBCASE("Special values") {
        uint8_t black[2], white[2];
        encodeRGB565_BE(black, 0, 0, 0);
        encodeRGB565_BE(white, 31, 63, 31);

        for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
            for (size_t ci = 0; ci < kNumDstColors; ci++) {
                bool ok1 = verifyBlendUnderPremul(
                    PixelFormatIDs::RGB565_BE, black,
                    kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                    kTestAlphas[ai], errorMsg);
                CHECK_MESSAGE(ok1, errorMsg);

                bool ok2 = verifyBlendUnderPremul(
                    PixelFormatIDs::RGB565_BE, white,
                    kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                    kTestAlphas[ai], errorMsg);
                CHECK_MESSAGE(ok2, errorMsg);
            }
        }
    }
}

// =============================================================================
// RGB888 blendUnderPremul Tests
// =============================================================================

TEST_CASE("RGB888 blendUnderPremul verification") {
    std::string errorMsg;

    SUBCASE("R channel sweep (G=0, B=0)") {
        for (int r = 0; r < 256; r++) {
            uint8_t src[3] = {static_cast<uint8_t>(r), 0, 0};
            for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok = verifyBlendUnderPremul(
                        PixelFormatIDs::RGB888, src,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[ai], errorMsg);
                    CHECK_MESSAGE(ok, errorMsg);
                }
            }
        }
    }

    SUBCASE("G channel sweep (R=0, B=0)") {
        for (int g = 0; g < 256; g++) {
            uint8_t src[3] = {0, static_cast<uint8_t>(g), 0};
            for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok = verifyBlendUnderPremul(
                        PixelFormatIDs::RGB888, src,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[ai], errorMsg);
                    CHECK_MESSAGE(ok, errorMsg);
                }
            }
        }
    }

    SUBCASE("B channel sweep (R=0, G=0)") {
        for (int b = 0; b < 256; b++) {
            uint8_t src[3] = {0, 0, static_cast<uint8_t>(b)};
            for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok = verifyBlendUnderPremul(
                        PixelFormatIDs::RGB888, src,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[ai], errorMsg);
                    CHECK_MESSAGE(ok, errorMsg);
                }
            }
        }
    }

    SUBCASE("Special values") {
        uint8_t black[3] = {0, 0, 0};
        uint8_t white[3] = {255, 255, 255};

        for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
            for (size_t ci = 0; ci < kNumDstColors; ci++) {
                bool ok1 = verifyBlendUnderPremul(
                    PixelFormatIDs::RGB888, black,
                    kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                    kTestAlphas[ai], errorMsg);
                CHECK_MESSAGE(ok1, errorMsg);

                bool ok2 = verifyBlendUnderPremul(
                    PixelFormatIDs::RGB888, white,
                    kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                    kTestAlphas[ai], errorMsg);
                CHECK_MESSAGE(ok2, errorMsg);
            }
        }
    }
}

// =============================================================================
// BGR888 blendUnderPremul Tests
// =============================================================================

TEST_CASE("BGR888 blendUnderPremul verification") {
    std::string errorMsg;

    SUBCASE("R channel sweep (G=0, B=0)") {
        // BGR888: B, G, R order
        for (int r = 0; r < 256; r++) {
            uint8_t src[3] = {0, 0, static_cast<uint8_t>(r)};  // B=0, G=0, R=r
            for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok = verifyBlendUnderPremul(
                        PixelFormatIDs::BGR888, src,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[ai], errorMsg);
                    CHECK_MESSAGE(ok, errorMsg);
                }
            }
        }
    }

    SUBCASE("G channel sweep (R=0, B=0)") {
        for (int g = 0; g < 256; g++) {
            uint8_t src[3] = {0, static_cast<uint8_t>(g), 0};  // B=0, G=g, R=0
            for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok = verifyBlendUnderPremul(
                        PixelFormatIDs::BGR888, src,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[ai], errorMsg);
                    CHECK_MESSAGE(ok, errorMsg);
                }
            }
        }
    }

    SUBCASE("B channel sweep (R=0, G=0)") {
        for (int b = 0; b < 256; b++) {
            uint8_t src[3] = {static_cast<uint8_t>(b), 0, 0};  // B=b, G=0, R=0
            for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok = verifyBlendUnderPremul(
                        PixelFormatIDs::BGR888, src,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[ai], errorMsg);
                    CHECK_MESSAGE(ok, errorMsg);
                }
            }
        }
    }

    SUBCASE("Special values") {
        uint8_t black[3] = {0, 0, 0};
        uint8_t white[3] = {255, 255, 255};

        for (size_t ai = 0; ai < kNumTestAlphas; ai++) {
            for (size_t ci = 0; ci < kNumDstColors; ci++) {
                bool ok1 = verifyBlendUnderPremul(
                    PixelFormatIDs::BGR888, black,
                    kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                    kTestAlphas[ai], errorMsg);
                CHECK_MESSAGE(ok1, errorMsg);

                bool ok2 = verifyBlendUnderPremul(
                    PixelFormatIDs::BGR888, white,
                    kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                    kTestAlphas[ai], errorMsg);
                CHECK_MESSAGE(ok2, errorMsg);
            }
        }
    }
}

// =============================================================================
// RGBA8_Straight blendUnderPremul Tests (with source alpha)
// =============================================================================

TEST_CASE("RGBA8_Straight blendUnderPremul verification") {
    std::string errorMsg;

    SUBCASE("R channel sweep with various srcA") {
        for (int r = 0; r < 256; r++) {
            for (size_t srcAi = 0; srcAi < kNumTestAlphas; srcAi++) {
                uint8_t src[4] = {static_cast<uint8_t>(r), 0, 0, kTestAlphas[srcAi]};
                for (size_t dstAi = 0; dstAi < kNumTestAlphas; dstAi++) {
                    for (size_t ci = 0; ci < kNumDstColors; ci++) {
                        bool ok = verifyBlendUnderPremul(
                            PixelFormatIDs::RGBA8_Straight, src,
                            kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                            kTestAlphas[dstAi], errorMsg);
                        CHECK_MESSAGE(ok, errorMsg);
                    }
                }
            }
        }
    }

    SUBCASE("G channel sweep with various srcA") {
        for (int g = 0; g < 256; g++) {
            for (size_t srcAi = 0; srcAi < kNumTestAlphas; srcAi++) {
                uint8_t src[4] = {0, static_cast<uint8_t>(g), 0, kTestAlphas[srcAi]};
                for (size_t dstAi = 0; dstAi < kNumTestAlphas; dstAi++) {
                    for (size_t ci = 0; ci < kNumDstColors; ci++) {
                        bool ok = verifyBlendUnderPremul(
                            PixelFormatIDs::RGBA8_Straight, src,
                            kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                            kTestAlphas[dstAi], errorMsg);
                        CHECK_MESSAGE(ok, errorMsg);
                    }
                }
            }
        }
    }

    SUBCASE("B channel sweep with various srcA") {
        for (int b = 0; b < 256; b++) {
            for (size_t srcAi = 0; srcAi < kNumTestAlphas; srcAi++) {
                uint8_t src[4] = {0, 0, static_cast<uint8_t>(b), kTestAlphas[srcAi]};
                for (size_t dstAi = 0; dstAi < kNumTestAlphas; dstAi++) {
                    for (size_t ci = 0; ci < kNumDstColors; ci++) {
                        bool ok = verifyBlendUnderPremul(
                            PixelFormatIDs::RGBA8_Straight, src,
                            kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                            kTestAlphas[dstAi], errorMsg);
                        CHECK_MESSAGE(ok, errorMsg);
                    }
                }
            }
        }
    }

    SUBCASE("Special values with all srcA") {
        for (size_t srcAi = 0; srcAi < kNumTestAlphas; srcAi++) {
            uint8_t black[4] = {0, 0, 0, kTestAlphas[srcAi]};
            uint8_t white[4] = {255, 255, 255, kTestAlphas[srcAi]};

            for (size_t dstAi = 0; dstAi < kNumTestAlphas; dstAi++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok1 = verifyBlendUnderPremul(
                        PixelFormatIDs::RGBA8_Straight, black,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[dstAi], errorMsg);
                    CHECK_MESSAGE(ok1, errorMsg);

                    bool ok2 = verifyBlendUnderPremul(
                        PixelFormatIDs::RGBA8_Straight, white,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[dstAi], errorMsg);
                    CHECK_MESSAGE(ok2, errorMsg);
                }
            }
        }
    }
}

// =============================================================================
// RGBA8_Straight blendUnderStraight Tests
// =============================================================================

TEST_CASE("RGBA8_Straight blendUnderStraight verification") {
    std::string errorMsg;

    SUBCASE("R channel sweep with various srcA") {
        for (int r = 0; r < 256; r++) {
            for (size_t srcAi = 0; srcAi < kNumTestAlphas; srcAi++) {
                uint8_t src[4] = {static_cast<uint8_t>(r), 0, 0, kTestAlphas[srcAi]};
                for (size_t dstAi = 0; dstAi < kNumTestAlphas; dstAi++) {
                    for (size_t ci = 0; ci < kNumDstColors; ci++) {
                        bool ok = verifyBlendUnderStraight(
                            PixelFormatIDs::RGBA8_Straight, src,
                            kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                            kTestAlphas[dstAi], errorMsg);
                        CHECK_MESSAGE(ok, errorMsg);
                    }
                }
            }
        }
    }

    SUBCASE("Special values with all srcA") {
        for (size_t srcAi = 0; srcAi < kNumTestAlphas; srcAi++) {
            uint8_t black[4] = {0, 0, 0, kTestAlphas[srcAi]};
            uint8_t white[4] = {255, 255, 255, kTestAlphas[srcAi]};

            for (size_t dstAi = 0; dstAi < kNumTestAlphas; dstAi++) {
                for (size_t ci = 0; ci < kNumDstColors; ci++) {
                    bool ok1 = verifyBlendUnderStraight(
                        PixelFormatIDs::RGBA8_Straight, black,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[dstAi], errorMsg);
                    CHECK_MESSAGE(ok1, errorMsg);

                    bool ok2 = verifyBlendUnderStraight(
                        PixelFormatIDs::RGBA8_Straight, white,
                        kDstColors[ci].r, kDstColors[ci].g, kDstColors[ci].b,
                        kTestAlphas[dstAi], errorMsg);
                    CHECK_MESSAGE(ok2, errorMsg);
                }
            }
        }
    }
}
