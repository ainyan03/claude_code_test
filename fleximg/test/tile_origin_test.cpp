// fleximg Tile Origin Test
// タイル分割時の origin 端数処理問題の検証
// コンパイル: g++ -std=c++17 -I../src tile_origin_test.cpp ../src/fleximg/viewport.cpp ../src/fleximg/pixel_format_registry.cpp -o tile_origin_test

#include <iostream>
#include <cstdint>
#include <iomanip>
#include "fleximg/common.h"
#include "fleximg/viewport.h"
#include "fleximg/image_buffer.h"
#include "fleximg/render_types.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/sink_node.h"
#include "fleximg/nodes/renderer_node.h"

using namespace fleximg;

int testsPassed = 0;
int testsFailed = 0;

void check(const char* name, bool condition) {
    if (condition) {
        std::cout << "[PASS] " << name << std::endl;
        testsPassed++;
    } else {
        std::cout << "[FAIL] " << name << std::endl;
        testsFailed++;
    }
}

// ========================================================================
// テスト: 奇数サイズ画像の中心配置
// ========================================================================
//
// 問題の再現:
// - 63x63 画像を中心基準 (31.5, 31.5) で配置
// - 800x600 キャンバス、中心 (400, 300) に配置
// - タイル分割なし vs あり で描画位置が異なる
//

void testOddSizeImageCentered() {
    std::cout << "\n=== Odd Size Image Centered Test ===" << std::endl;

    // 63x63 画像（奇数サイズ）
    const int imgW = 63;
    const int imgH = 63;
    const float imgOriginX = imgW / 2.0f;  // 31.5
    const float imgOriginY = imgH / 2.0f;  // 31.5

    // キャンバス 800x600
    const int canvasW = 800;
    const int canvasH = 600;
    const float canvasOriginX = canvasW / 2.0f;  // 400.0
    const float canvasOriginY = canvasH / 2.0f;  // 300.0

    std::cout << "Image: " << imgW << "x" << imgH << ", origin: ("
              << imgOriginX << ", " << imgOriginY << ")" << std::endl;
    std::cout << "Canvas: " << canvasW << "x" << canvasH << ", origin: ("
              << canvasOriginX << ", " << canvasOriginY << ")" << std::endl;

    // 固定小数点変換
    int_fixed8 imgOrgX = float_to_fixed8(imgOriginX);  // 31.5 * 256 = 8064
    int_fixed8 imgOrgY = float_to_fixed8(imgOriginY);
    int_fixed8 canvasOrgX = float_to_fixed8(canvasOriginX);  // 400 * 256 = 102400
    int_fixed8 canvasOrgY = float_to_fixed8(canvasOriginY);

    std::cout << "imgOrgX (fixed8): " << imgOrgX << " = " << fixed8_to_float(imgOrgX) << std::endl;
    std::cout << "canvasOrgX (fixed8): " << canvasOrgX << " = " << fixed8_to_float(canvasOrgX) << std::endl;

    // ========================================
    // テスト1: タイル分割なし
    // ========================================
    std::cout << "\n--- Test 1: No Tile Split ---" << std::endl;

    // 要求: キャンバス全体
    RenderRequest reqNoTile;
    reqNoTile.width = canvasW;
    reqNoTile.height = canvasH;
    reqNoTile.origin.x = canvasOrgX;
    reqNoTile.origin.y = canvasOrgY;

    // ソース画像の基準相対座標範囲
    int_fixed8 imgLeft = -imgOrgX;
    int_fixed8 imgTop = -imgOrgY;
    int_fixed8 imgRight = imgLeft + to_fixed8(imgW);
    int_fixed8 imgBottom = imgTop + to_fixed8(imgH);

    // 要求範囲の基準相対座標
    int_fixed8 reqLeft = -reqNoTile.origin.x;
    int_fixed8 reqTop = -reqNoTile.origin.y;
    int_fixed8 reqRight = reqLeft + to_fixed8(reqNoTile.width);
    int_fixed8 reqBottom = reqTop + to_fixed8(reqNoTile.height);

    // 交差領域
    int_fixed8 interLeft = std::max(imgLeft, reqLeft);
    int_fixed8 interTop = std::max(imgTop, reqTop);
    int_fixed8 interRight = std::min(imgRight, reqRight);
    int_fixed8 interBottom = std::min(imgBottom, reqBottom);

    std::cout << "imgLeft/Right: " << fixed8_to_float(imgLeft) << " to " << fixed8_to_float(imgRight) << std::endl;
    std::cout << "reqLeft/Right: " << fixed8_to_float(reqLeft) << " to " << fixed8_to_float(reqRight) << std::endl;
    std::cout << "interLeft/Right: " << fixed8_to_float(interLeft) << " to " << fixed8_to_float(interRight) << std::endl;

    int srcX_noTile = from_fixed8(interLeft - imgLeft);
    int interW_noTile = from_fixed8(interRight - interLeft);
    int_fixed8 resultOriginX_noTile = -interLeft;

    std::cout << "srcX: " << srcX_noTile << ", interW: " << interW_noTile << std::endl;
    std::cout << "resultOriginX (fixed8): " << resultOriginX_noTile << " = " << fixed8_to_float(resultOriginX_noTile) << std::endl;

    // SinkNode での配置計算
    int dstX_noTile = from_fixed8(canvasOrgX - resultOriginX_noTile);
    std::cout << "dstX (no tile): " << dstX_noTile << std::endl;
    std::cout << "  (exact: " << (canvasOriginX - imgOriginX) << ")" << std::endl;

    // ========================================
    // テスト2: タイル分割あり（タイル64x64）
    // ========================================
    std::cout << "\n--- Test 2: With Tile Split (64x64) ---" << std::endl;

    const int tileW = 64;
    const int tileH = 64;

    // 画像が含まれるタイルを探す
    // 画像は dstX=368.5 付近に配置される（368〜369）
    // タイル(5,4) = (320,256)〜(383,319) に含まれる
    int tileX = 5;
    int tileY = 4;
    int tileLeft = tileX * tileW;  // 320
    int tileTop = tileY * tileH;   // 256

    std::cout << "Tile (" << tileX << "," << tileY << "): ("
              << tileLeft << "," << tileTop << ") to ("
              << (tileLeft + tileW) << "," << (tileTop + tileH) << ")" << std::endl;

    // タイル用要求
    RenderRequest reqTile;
    reqTile.width = tileW;
    reqTile.height = tileH;
    reqTile.origin.x = canvasOrgX - to_fixed8(tileLeft);
    reqTile.origin.y = canvasOrgY - to_fixed8(tileTop);

    std::cout << "reqTile.origin: (" << fixed8_to_float(reqTile.origin.x) << ", "
              << fixed8_to_float(reqTile.origin.y) << ")" << std::endl;

    // 要求範囲の基準相対座標
    int_fixed8 reqLeft2 = -reqTile.origin.x;
    int_fixed8 reqTop2 = -reqTile.origin.y;
    int_fixed8 reqRight2 = reqLeft2 + to_fixed8(reqTile.width);
    int_fixed8 reqBottom2 = reqTop2 + to_fixed8(reqTile.height);

    std::cout << "reqLeft2/Right2: " << fixed8_to_float(reqLeft2) << " to " << fixed8_to_float(reqRight2) << std::endl;

    // 交差領域
    int_fixed8 interLeft2 = std::max(imgLeft, reqLeft2);
    int_fixed8 interTop2 = std::max(imgTop, reqTop2);
    int_fixed8 interRight2 = std::min(imgRight, reqRight2);
    int_fixed8 interBottom2 = std::min(imgBottom, reqBottom2);

    std::cout << "interLeft2/Right2: " << fixed8_to_float(interLeft2) << " to " << fixed8_to_float(interRight2) << std::endl;

    int srcX_tile = from_fixed8(interLeft2 - imgLeft);
    int interW_tile = from_fixed8(interRight2 - interLeft2);
    int_fixed8 resultOriginX_tile = -interLeft2;

    std::cout << "srcX: " << srcX_tile << ", interW: " << interW_tile << std::endl;
    std::cout << "resultOriginX (fixed8): " << resultOriginX_tile << " = " << fixed8_to_float(resultOriginX_tile) << std::endl;

    // SinkNode での配置計算
    int dstX_tile = from_fixed8(canvasOrgX - resultOriginX_tile);
    std::cout << "dstX (with tile): " << dstX_tile << std::endl;

    // ========================================
    // 比較
    // ========================================
    std::cout << "\n--- Comparison ---" << std::endl;
    std::cout << "No Tile: dstX = " << dstX_noTile << std::endl;
    std::cout << "With Tile: dstX = " << dstX_tile << std::endl;

    check("dstX should match between tile/no-tile", dstX_noTile == dstX_tile);

    // 期待値との比較
    float expectedDstX = canvasOriginX - imgOriginX;  // 400.0 - 31.5 = 368.5
    int expectedDstXInt = static_cast<int>(expectedDstX);  // 368 (切り捨て)
    std::cout << "Expected dstX (float): " << expectedDstX << std::endl;
    std::cout << "Expected dstX (int): " << expectedDstXInt << std::endl;

    check("dstX_noTile matches expected", dstX_noTile == expectedDstXInt);
    check("dstX_tile matches expected", dstX_tile == expectedDstXInt);
}

// ========================================================================
// テスト: タイル境界をまたぐ場合のピクセル欠落
// ========================================================================

void testTileBoundaryPixelLoss() {
    std::cout << "\n=== Tile Boundary Pixel Loss Test ===" << std::endl;

    // 63x63 画像
    const int imgW = 63;
    const float imgOriginX = imgW / 2.0f;  // 31.5

    // タイル境界をまたぐ配置
    // 画像は dstX=368.5 付近、タイル(5) [320,383] と タイル(6) [384,447] にまたがる

    int_fixed8 imgOrgX = float_to_fixed8(imgOriginX);
    int_fixed8 imgLeft = -imgOrgX;
    int_fixed8 imgRight = imgLeft + to_fixed8(imgW);
    int_fixed8 canvasOrgX = to_fixed8(400);

    const int tileW = 64;

    // タイル(5)
    int tileLeft5 = 320;
    int_fixed8 reqOriginX5 = canvasOrgX - to_fixed8(tileLeft5);  // 80
    int_fixed8 reqLeft5 = -reqOriginX5;  // -80
    int_fixed8 reqRight5 = reqLeft5 + to_fixed8(tileW);  // -16

    int_fixed8 interLeft5 = std::max(imgLeft, reqLeft5);
    int_fixed8 interRight5 = std::min(imgRight, reqRight5);

    // 修正後の計算: floor/ceil を使用
    int srcX5 = from_fixed8_floor(interLeft5 - imgLeft);
    int srcEndX5 = from_fixed8_ceil(interRight5 - imgLeft);
    int interW5 = srcEndX5 - srcX5;

    std::cout << "Tile 5: interLeft=" << fixed8_to_float(interLeft5)
              << ", interRight=" << fixed8_to_float(interRight5)
              << ", srcX=" << srcX5 << ", srcEndX=" << srcEndX5
              << ", interW=" << interW5 << std::endl;

    // タイル(6)
    int tileLeft6 = 384;
    int_fixed8 reqOriginX6 = canvasOrgX - to_fixed8(tileLeft6);  // 16
    int_fixed8 reqLeft6 = -reqOriginX6;  // -16
    int_fixed8 reqRight6 = reqLeft6 + to_fixed8(tileW);  // 48

    int_fixed8 interLeft6 = std::max(imgLeft, reqLeft6);
    int_fixed8 interRight6 = std::min(imgRight, reqRight6);

    // 修正後の計算: floor/ceil を使用
    int srcX6 = from_fixed8_floor(interLeft6 - imgLeft);
    int srcEndX6 = from_fixed8_ceil(interRight6 - imgLeft);
    int interW6 = srcEndX6 - srcX6;

    std::cout << "Tile 6: interLeft=" << fixed8_to_float(interLeft6)
              << ", interRight=" << fixed8_to_float(interRight6)
              << ", srcX=" << srcX6 << ", srcEndX=" << srcEndX6
              << ", interW=" << interW6 << std::endl;

    // 重複を考慮した実効幅の計算
    // Tile 5: srcX=0 〜 srcEndX
    // Tile 6: srcX 〜 srcEndX
    // 重複領域: srcX6 〜 srcEndX5
    int overlap = std::max(0, srcEndX5 - srcX6);
    int effectiveTotal = interW5 + interW6 - overlap;
    std::cout << "Overlap: " << overlap << ", Effective total: " << effectiveTotal << std::endl;

    check("Effective total width should equal image width", effectiveTotal == imgW);

    // 追加チェック: 欠落がないこと（両端がカバーされている）
    check("Tile 5 should start at 0", srcX5 == 0);
    check("Tile 6 should end at image width", srcEndX6 == imgW);
}

// ========================================================================
// メイン
// ========================================================================

int main() {
    std::cout << "=== fleximg Tile Origin Test ===" << std::endl;

    testOddSizeImageCentered();
    testTileBoundaryPixelLoss();

    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Passed: " << testsPassed << std::endl;
    std::cout << "Failed: " << testsFailed << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
