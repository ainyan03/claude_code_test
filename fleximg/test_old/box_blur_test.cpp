// BoxBlur テスト
// boxBlurWithPadding の動作検証

#include <iostream>
#include <cstdint>
#include <cstring>
#include <cmath>

#define FLEXIMG_NAMESPACE fleximg
#include "../src/fleximg/viewport.h"
#include "../src/fleximg/image_buffer.h"
#include "../src/fleximg/operations/filters.h"

using namespace fleximg;

int testCount = 0;
int passCount = 0;

void check(bool condition, const char* testName) {
    testCount++;
    if (condition) {
        passCount++;
        std::cout << "  PASS: " << testName << std::endl;
    } else {
        std::cout << "  FAIL: " << testName << std::endl;
    }
}

// ピクセル値を設定
void setPixel(ViewPort& vp, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t* p = static_cast<uint8_t*>(vp.pixelAt(x, y));
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

// ピクセル値を取得
void getPixel(const ViewPort& vp, int x, int y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    const uint8_t* p = static_cast<const uint8_t*>(vp.pixelAt(x, y));
    r = p[0]; g = p[1]; b = p[2]; a = p[3];
}

// テスト1: 基本動作 - 入力と出力が同サイズ、オフセットなし
void testBasicSameSize() {
    std::cout << "Test: Basic same size (no offset)" << std::endl;

    // 5x5 の入力画像（中央に赤いピクセル）
    ImageBuffer src(5, 5, PixelFormatIDs::RGBA8_Straight);
    ViewPort srcView = src.view();

    // 全体を黒で初期化
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            setPixel(srcView, x, y, 0, 0, 0, 255);
        }
    }
    // 中央に赤
    setPixel(srcView, 2, 2, 255, 0, 0, 255);

    // 同サイズの出力
    ImageBuffer dst(5, 5, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dst.view();

    // radius=1 でブラー
    filters::boxBlurWithPadding(dstView, srcView, 0, 0, 1);

    // 中央は赤が残る（周囲8ピクセルと混合）
    uint8_t r, g, b, a;
    getPixel(dstView, 2, 2, r, g, b, a);
    check(r > 0, "Center pixel has red component");
    check(a == 255, "Center pixel is opaque");

    // 角は透明とブレンドされて半透明になる（透明拡張の効果）
    getPixel(dstView, 0, 0, r, g, b, a);
    check(a < 255, "Corner pixel is semi-transparent (blended with transparent)");
}

// テスト2: 透明拡張 - 出力が入力より大きい
void testTransparentExpansion() {
    std::cout << "Test: Transparent expansion (dst > src)" << std::endl;

    // 3x3 の赤い画像
    ImageBuffer src(3, 3, PixelFormatIDs::RGBA8_Straight);
    ViewPort srcView = src.view();
    for (int y = 0; y < 3; y++) {
        for (int x = 0; x < 3; x++) {
            setPixel(srcView, x, y, 255, 0, 0, 255);
        }
    }

    // 7x7 の出力（中央に3x3がオフセット2で配置）
    ImageBuffer dst(7, 7, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dst.view();

    // オフセット(2, 2) でブラー
    filters::boxBlurWithPadding(dstView, srcView, 2, 2, 1);

    // 角(0,0)は透明領域のみ参照 → 透明
    uint8_t r, g, b, a;
    getPixel(dstView, 0, 0, r, g, b, a);
    check(a == 0, "Far corner is transparent");

    // 中央(3,3)は赤い画像の中央 → 不透明
    getPixel(dstView, 3, 3, r, g, b, a);
    check(a > 0, "Center has some opacity");
    check(r > 0, "Center has red");

    // 境界(1,1)は透明と赤の境界 → 半透明
    getPixel(dstView, 1, 1, r, g, b, a);
    check(a > 0 && a < 255, "Boundary is semi-transparent");
}

// テスト3: α加重平均の検証
void testAlphaWeightedBlend() {
    std::cout << "Test: Alpha-weighted blending" << std::endl;

    // 2x1 の画像: 赤(不透明) と 緑(透明)
    ImageBuffer src(2, 1, PixelFormatIDs::RGBA8_Straight);
    ViewPort srcView = src.view();
    setPixel(srcView, 0, 0, 255, 0, 0, 255);  // 赤 α=255
    setPixel(srcView, 1, 0, 0, 255, 0, 0);    // 緑 α=0（透明）

    // 同サイズの出力
    ImageBuffer dst(2, 1, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dst.view();

    // radius=1 でブラー（カーネル3だが幅2なので実質2ピクセル参照）
    filters::boxBlurWithPadding(dstView, srcView, 0, 0, 1);

    // α加重平均なので、透明ピクセルの緑は無視され、赤が維持される
    uint8_t r, g, b, a;
    getPixel(dstView, 0, 0, r, g, b, a);
    check(r > g, "Red component dominates (alpha-weighted)");
}

// テスト4: スライディングウィンドウの一貫性
void testSlidingWindowConsistency() {
    std::cout << "Test: Sliding window consistency" << std::endl;

    // 10x10 の均一色画像
    ImageBuffer src(10, 10, PixelFormatIDs::RGBA8_Straight);
    ViewPort srcView = src.view();
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            setPixel(srcView, x, y, 100, 150, 200, 255);
        }
    }

    // 同サイズの出力
    ImageBuffer dst(10, 10, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dst.view();

    filters::boxBlurWithPadding(dstView, srcView, 0, 0, 2);

    // 均一色なので、中央部分は同じ色のまま
    uint8_t r1, g1, b1, a1;
    uint8_t r2, g2, b2, a2;
    getPixel(dstView, 5, 5, r1, g1, b1, a1);
    getPixel(dstView, 6, 6, r2, g2, b2, a2);
    check(r1 == r2 && g1 == g2 && b1 == b2, "Uniform region stays uniform");
    check(a1 == 255 && a2 == 255, "Alpha remains opaque");
}

// テスト5: 大きなradiusでの動作
void testLargeRadius() {
    std::cout << "Test: Large radius blur" << std::endl;

    // 5x5 の中央に点
    ImageBuffer src(5, 5, PixelFormatIDs::RGBA8_Straight);
    ViewPort srcView = src.view();
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            setPixel(srcView, x, y, 0, 0, 0, 255);
        }
    }
    setPixel(srcView, 2, 2, 255, 255, 255, 255);

    // 15x15 の出力（大きく拡張）
    ImageBuffer dst(15, 15, PixelFormatIDs::RGBA8_Straight);
    ViewPort dstView = dst.view();

    // radius=5 でブラー、オフセット(5,5)
    filters::boxBlurWithPadding(dstView, srcView, 5, 5, 5);

    // 遠い角(0,0): src範囲外だが、水平ブラーで一部参照可能
    // dst(0,0) のウィンドウ x ∈ [-5, 5] → src x ∈ [-10, 0]
    // src範囲は [0, 5) なので src(0, y) のみ参照される
    // よって完全に透明ではなく、低いα値になる
    uint8_t r, g, b, a;
    getPixel(dstView, 0, 0, r, g, b, a);
    check(a < 128, "Far corner has low alpha (mostly transparent)");

    // 中央付近は影響を受ける
    getPixel(dstView, 7, 7, r, g, b, a);
    check(a > 0, "Center area has some opacity");
}

int main() {
    std::cout << "=== BoxBlur Tests ===" << std::endl;

    testBasicSameSize();
    testTransparentExpansion();
    testAlphaWeightedBlend();
    testSlidingWindowConsistency();
    testLargeRadius();

    std::cout << "\n=== Results: " << passCount << "/" << testCount << " passed ===" << std::endl;

    return (passCount == testCount) ? 0 : 1;
}
