// fleximg blendUnderStraight Verification Tests
// ピクセルフォーマット別のblendUnder関数の検証テスト
//
// 検証方針:
//   直接パス: srcFormat->blendUnderStraight(dst, src, ...)
//   参照パス: srcFormat->toStraight(tmp, src, ...) + RGBA8Straight->blendUnderStraight(dst, tmp, ...)
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

// RGBA8_Straight形式のdstバッファを初期化
void initDstStraight(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    dst[0] = r;
    dst[1] = g;
    dst[2] = b;
    dst[3] = a;
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
