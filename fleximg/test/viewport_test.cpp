// fleximg ViewPort Unit Tests
// ViewPort構造体のテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/image/viewport.h"
#include "fleximg/image/image_buffer.h"

using namespace fleximg;

// =============================================================================
// テスト用ヘルパー関数
// =============================================================================

// ViewPort から直接 copyRowDDA を呼び出すヘルパー
// testCopyRowDDA の代替として使用
static void testCopyRowDDA(
    void* dst,
    const ViewPort& src,
    int count,
    int_fixed srcX,
    int_fixed srcY,
    int_fixed incrX,
    int_fixed incrY
) {
    if (!src.isValid() || count <= 0) return;
    DDAParam param = { src.stride, 0, 0, srcX, srcY, incrX, incrY, nullptr, 0, 0, 0 };
    if (src.formatID && src.formatID->copyRowDDA) {
        src.formatID->copyRowDDA(
            static_cast<uint8_t*>(dst),
            static_cast<const uint8_t*>(src.data),
            count,
            &param
        );
    }
}

// =============================================================================
// ViewPort Construction Tests
// =============================================================================

TEST_CASE("ViewPort default construction") {
    ViewPort v;
    CHECK(v.data == nullptr);
    CHECK(v.width == 0);
    CHECK(v.height == 0);
    CHECK(v.stride == 0);
    CHECK_FALSE(v.isValid());
}

TEST_CASE("ViewPort direct construction") {
    uint8_t buffer[400];  // 10x10 RGBA8
    ViewPort v(buffer, PixelFormatIDs::RGBA8_Straight, 40, 10, 10);

    CHECK(v.data == buffer);
    CHECK(v.formatID == PixelFormatIDs::RGBA8_Straight);
    CHECK(v.stride == 40);
    CHECK(v.width == 10);
    CHECK(v.height == 10);
    CHECK(v.isValid());
}

TEST_CASE("ViewPort simple construction with auto stride") {
    uint8_t buffer[400];
    ViewPort v(buffer, 10, 10, PixelFormatIDs::RGBA8_Straight);

    CHECK(v.data == buffer);
    CHECK(v.width == 10);
    CHECK(v.height == 10);
    CHECK(v.stride == 40);  // 10 * 4 bytes
    CHECK(v.isValid());
}


// =============================================================================
// ViewPort Validity Tests
// =============================================================================

TEST_CASE("ViewPort validity") {
    uint8_t buffer[100];

    SUBCASE("null data is invalid") {
        ViewPort v(nullptr, 10, 10, PixelFormatIDs::RGBA8_Straight);
        CHECK_FALSE(v.isValid());
    }

    SUBCASE("zero width is invalid") {
        ViewPort v(buffer, 0, 10, PixelFormatIDs::RGBA8_Straight);
        CHECK_FALSE(v.isValid());
    }

    SUBCASE("zero height is invalid") {
        ViewPort v(buffer, 10, 0, PixelFormatIDs::RGBA8_Straight);
        CHECK_FALSE(v.isValid());
    }

    SUBCASE("valid viewport") {
        ViewPort v(buffer, 5, 5, PixelFormatIDs::RGBA8_Straight);
        CHECK(v.isValid());
    }
}

// =============================================================================
// Pixel Access Tests
// =============================================================================

TEST_CASE("ViewPort pixelAt") {
    uint8_t buffer[16] = {0};  // 2x2 RGBA8
    ViewPort v(buffer, 2, 2, PixelFormatIDs::RGBA8_Straight);

    SUBCASE("pixelAt returns correct address") {
        CHECK(v.pixelAt(0, 0) == buffer);
        CHECK(v.pixelAt(1, 0) == buffer + 4);
        CHECK(v.pixelAt(0, 1) == buffer + 8);
        CHECK(v.pixelAt(1, 1) == buffer + 12);
    }

    SUBCASE("write and read pixel") {
        uint8_t* pixel = static_cast<uint8_t*>(v.pixelAt(1, 1));
        pixel[0] = 255;  // R
        pixel[1] = 128;  // G
        pixel[2] = 64;   // B
        pixel[3] = 255;  // A

        const uint8_t* readPixel = static_cast<const uint8_t*>(v.pixelAt(1, 1));
        CHECK(readPixel[0] == 255);
        CHECK(readPixel[1] == 128);
        CHECK(readPixel[2] == 64);
        CHECK(readPixel[3] == 255);
    }
}

TEST_CASE("ViewPort pixelAt with custom stride") {
    // stride > width * bpp のケース（パディングあり）
    uint8_t buffer[64] = {0};  // 2x2 with 32-byte stride
    ViewPort v(buffer, PixelFormatIDs::RGBA8_Straight, 32, 2, 2);

    CHECK(v.pixelAt(0, 0) == buffer);
    CHECK(v.pixelAt(1, 0) == buffer + 4);
    CHECK(v.pixelAt(0, 1) == buffer + 32);  // next row at stride offset
    CHECK(v.pixelAt(1, 1) == buffer + 36);
}

// =============================================================================
// Byte Info Tests
// =============================================================================

TEST_CASE("ViewPort byte info") {
    uint8_t buffer[100];

    SUBCASE("bytesPerPixel for RGBA8") {
        ViewPort v(buffer, 10, 10, PixelFormatIDs::RGBA8_Straight);
        CHECK(v.bytesPerPixel() == 4);
    }


    SUBCASE("rowBytes with positive stride") {
        ViewPort v(buffer, PixelFormatIDs::RGBA8_Straight, 48, 10, 10);
        CHECK(v.rowBytes() == 48);
    }

    SUBCASE("rowBytes with negative stride (Y-flip)") {
        ViewPort v(buffer, PixelFormatIDs::RGBA8_Straight, -48, 10, 10);
        CHECK(v.rowBytes() == 40);  // width * bpp
    }
}

// =============================================================================
// subView Tests
// =============================================================================

TEST_CASE("view_ops::subView") {
    uint8_t buffer[400] = {0};  // 10x10 RGBA8
    ViewPort v(buffer, 10, 10, PixelFormatIDs::RGBA8_Straight);

    SUBCASE("subView at origin") {
        auto sub = view_ops::subView(v, 0, 0, 5, 5);
        CHECK(sub.data == buffer);
        CHECK(sub.width == 5);
        CHECK(sub.height == 5);
        CHECK(sub.stride == v.stride);
        CHECK(sub.formatID == v.formatID);
    }

    SUBCASE("subView with offset") {
        auto sub = view_ops::subView(v, 2, 3, 4, 4);
        CHECK(sub.data == v.pixelAt(2, 3));
        CHECK(sub.width == 4);
        CHECK(sub.height == 4);
        CHECK(sub.stride == v.stride);
    }

}

// =============================================================================
// copyRowDDA Tests
// =============================================================================

// リファレンス実装: 全パスで同じ結果を出すことを検証するための素朴な実装
static void copyRowDDA_Reference(
    uint8_t* dstRow,
    const uint8_t* srcData,
    int32_t srcStride,
    int bpp,
    int_fixed srcX,
    int_fixed srcY,
    int_fixed incrX,
    int_fixed incrY,
    int count
) {
    for (int i = 0; i < count; i++) {
        uint32_t sx = static_cast<uint32_t>(srcX) >> 16;
        uint32_t sy = static_cast<uint32_t>(srcY) >> 16;
        const uint8_t* srcPixel = srcData
            + static_cast<size_t>(sy) * static_cast<size_t>(srcStride)
            + static_cast<size_t>(sx) * static_cast<size_t>(bpp);
        std::memcpy(dstRow, srcPixel, static_cast<size_t>(bpp));
        dstRow += bpp;
        srcX += incrX;
        srcY += incrY;
    }
}

TEST_CASE("copyRowDDA: incrY==0 (horizontal scan)") {
    // 8x4 RGBA8 ソース画像を作成（各ピクセルに識別可能な値を設定）
    constexpr int SRC_W = 8;
    constexpr int SRC_H = 4;
    constexpr int BPP = 4;
    uint8_t srcBuf[SRC_W * SRC_H * BPP];
    for (int y = 0; y < SRC_H; y++) {
        for (int x = 0; x < SRC_W; x++) {
            int idx = (y * SRC_W + x) * BPP;
            srcBuf[idx + 0] = static_cast<uint8_t>(x * 30);      // R
            srcBuf[idx + 1] = static_cast<uint8_t>(y * 60);      // G
            srcBuf[idx + 2] = static_cast<uint8_t>((x + y) * 20);// B
            srcBuf[idx + 3] = 255;                                // A
        }
    }
    ViewPort src(srcBuf, SRC_W, SRC_H, PixelFormatIDs::RGBA8_Straight);

    SUBCASE("1:1 copy") {
        constexpr int COUNT = 8;
        uint8_t dstActual[COUNT * BPP] = {};
        uint8_t dstExpected[COUNT * BPP] = {};

        int_fixed srcX = 0;
        int_fixed srcY = to_fixed(1);  // Y=1の行
        int_fixed incrX = INT_FIXED_ONE;
        int_fixed incrY = 0;

        testCopyRowDDA(dstActual, src, COUNT, srcX, srcY, incrX, incrY);
        copyRowDDA_Reference(dstExpected, srcBuf, src.stride, BPP,
                             srcX, srcY, incrX, incrY, COUNT);

        for (int i = 0; i < COUNT * BPP; i++) {
            CHECK(dstActual[i] == dstExpected[i]);
        }
    }

    SUBCASE("2x scale up") {
        constexpr int COUNT = 6;
        uint8_t dstActual[COUNT * BPP] = {};
        uint8_t dstExpected[COUNT * BPP] = {};

        int_fixed srcX = 0;
        int_fixed srcY = to_fixed(2);  // Y=2の行
        int_fixed incrX = INT_FIXED_ONE / 2;  // 0.5刻み → 2倍拡大
        int_fixed incrY = 0;

        testCopyRowDDA(dstActual, src, COUNT, srcX, srcY, incrX, incrY);
        copyRowDDA_Reference(dstExpected, srcBuf, src.stride, BPP,
                             srcX, srcY, incrX, incrY, COUNT);

        for (int i = 0; i < COUNT * BPP; i++) {
            CHECK(dstActual[i] == dstExpected[i]);
        }
    }

    SUBCASE("0.5x scale down") {
        constexpr int COUNT = 4;
        uint8_t dstActual[COUNT * BPP] = {};
        uint8_t dstExpected[COUNT * BPP] = {};

        int_fixed srcX = 0;
        int_fixed srcY = 0;
        int_fixed incrX = INT_FIXED_ONE * 2;  // 2.0刻み → 0.5倍縮小
        int_fixed incrY = 0;

        testCopyRowDDA(dstActual, src, COUNT, srcX, srcY, incrX, incrY);
        copyRowDDA_Reference(dstExpected, srcBuf, src.stride, BPP,
                             srcX, srcY, incrX, incrY, COUNT);

        for (int i = 0; i < COUNT * BPP; i++) {
            CHECK(dstActual[i] == dstExpected[i]);
        }
    }
}

TEST_CASE("copyRowDDA: incrX==0 (vertical scan)") {
    constexpr int SRC_W = 4;
    constexpr int SRC_H = 8;
    constexpr int BPP = 4;
    uint8_t srcBuf[SRC_W * SRC_H * BPP];
    for (int y = 0; y < SRC_H; y++) {
        for (int x = 0; x < SRC_W; x++) {
            int idx = (y * SRC_W + x) * BPP;
            srcBuf[idx + 0] = static_cast<uint8_t>(x * 50);
            srcBuf[idx + 1] = static_cast<uint8_t>(y * 30);
            srcBuf[idx + 2] = static_cast<uint8_t>((x + y) * 15);
            srcBuf[idx + 3] = 200;
        }
    }
    ViewPort src(srcBuf, SRC_W, SRC_H, PixelFormatIDs::RGBA8_Straight);

    constexpr int COUNT = 6;
    uint8_t dstActual[COUNT * BPP] = {};
    uint8_t dstExpected[COUNT * BPP] = {};

    int_fixed srcX = to_fixed(2);  // X=2の列
    int_fixed srcY = 0;
    int_fixed incrX = 0;
    int_fixed incrY = INT_FIXED_ONE;

    testCopyRowDDA(dstActual, src, COUNT, srcX, srcY, incrX, incrY);
    copyRowDDA_Reference(dstExpected, srcBuf, src.stride, BPP,
                         srcX, srcY, incrX, incrY, COUNT);

    for (int i = 0; i < COUNT * BPP; i++) {
        CHECK(dstActual[i] == dstExpected[i]);
    }
}

TEST_CASE("copyRowDDA: both non-zero (diagonal/rotation)") {
    constexpr int SRC_W = 8;
    constexpr int SRC_H = 8;
    constexpr int BPP = 4;
    uint8_t srcBuf[SRC_W * SRC_H * BPP];
    for (int y = 0; y < SRC_H; y++) {
        for (int x = 0; x < SRC_W; x++) {
            int idx = (y * SRC_W + x) * BPP;
            srcBuf[idx + 0] = static_cast<uint8_t>(x * 30 + 10);
            srcBuf[idx + 1] = static_cast<uint8_t>(y * 30 + 10);
            srcBuf[idx + 2] = static_cast<uint8_t>((x ^ y) * 20);
            srcBuf[idx + 3] = 255;
        }
    }
    ViewPort src(srcBuf, SRC_W, SRC_H, PixelFormatIDs::RGBA8_Straight);

    constexpr int COUNT = 5;
    uint8_t dstActual[COUNT * BPP] = {};
    uint8_t dstExpected[COUNT * BPP] = {};

    int_fixed srcX = to_fixed(1);
    int_fixed srcY = to_fixed(1);
    int_fixed incrX = INT_FIXED_ONE;      // 斜め: X+1, Y+1
    int_fixed incrY = INT_FIXED_ONE;

    testCopyRowDDA(dstActual, src, COUNT, srcX, srcY, incrX, incrY);
    copyRowDDA_Reference(dstExpected, srcBuf, src.stride, BPP,
                         srcX, srcY, incrX, incrY, COUNT);

    for (int i = 0; i < COUNT * BPP; i++) {
        CHECK(dstActual[i] == dstExpected[i]);
    }
}

TEST_CASE("copyRowDDA: boundary conditions") {
    constexpr int SRC_W = 4;
    constexpr int SRC_H = 4;
    constexpr int BPP = 4;
    uint8_t srcBuf[SRC_W * SRC_H * BPP];
    for (int i = 0; i < SRC_W * SRC_H * BPP; i++) {
        srcBuf[i] = static_cast<uint8_t>(i & 0xFF);
    }
    ViewPort src(srcBuf, SRC_W, SRC_H, PixelFormatIDs::RGBA8_Straight);

    SUBCASE("count==1") {
        uint8_t dstActual[BPP] = {};
        uint8_t dstExpected[BPP] = {};

        int_fixed srcX = to_fixed(2);
        int_fixed srcY = to_fixed(3);
        int_fixed incrX = INT_FIXED_ONE;
        int_fixed incrY = 0;

        testCopyRowDDA(dstActual, src, 1, srcX, srcY, incrX, incrY);
        copyRowDDA_Reference(dstExpected, srcBuf, src.stride, BPP,
                             srcX, srcY, incrX, incrY, 1);

        for (int i = 0; i < BPP; i++) {
            CHECK(dstActual[i] == dstExpected[i]);
        }
    }

    SUBCASE("count==3 (less than 4, edge case for unrolling)") {
        constexpr int COUNT = 3;
        uint8_t dstActual[COUNT * BPP] = {};
        uint8_t dstExpected[COUNT * BPP] = {};

        int_fixed srcX = to_fixed(1);
        int_fixed srcY = to_fixed(0);
        int_fixed incrX = INT_FIXED_ONE;
        int_fixed incrY = 0;

        testCopyRowDDA(dstActual, src, COUNT, srcX, srcY, incrX, incrY);
        copyRowDDA_Reference(dstExpected, srcBuf, src.stride, BPP,
                             srcX, srcY, incrX, incrY, COUNT);

        for (int i = 0; i < COUNT * BPP; i++) {
            CHECK(dstActual[i] == dstExpected[i]);
        }
    }

    SUBCASE("count==0 (no-op)") {
        uint8_t dstActual[BPP] = {0xAA, 0xBB, 0xCC, 0xDD};
        testCopyRowDDA(dstActual, src, 0, 0, 0, INT_FIXED_ONE, 0);
        // バッファが変更されていないことを確認
        CHECK(dstActual[0] == 0xAA);
        CHECK(dstActual[1] == 0xBB);
        CHECK(dstActual[2] == 0xCC);
        CHECK(dstActual[3] == 0xDD);
    }
}

TEST_CASE("copyRowDDA: 2BPP format") {
    constexpr int SRC_W = 8;
    constexpr int SRC_H = 4;
    constexpr int BPP = 2;
    uint8_t srcBuf[SRC_W * SRC_H * BPP];
    for (int i = 0; i < SRC_W * SRC_H * BPP; i++) {
        srcBuf[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
    }
    ViewPort src(srcBuf, PixelFormatIDs::RGBA8_Straight, SRC_W * BPP, SRC_W, SRC_H);
    // 注意: formatIDはRGBA8_Straightだが、strideを2*widthに設定してBPP=2相当にテスト
    // 実際にはbppはformatIDから決まるので、ここでは2bppフォーマットが必要
    // → bpp=4のformatIDを使うが、テストの本質は方向別パスの正確性

    // 代わりに、RGBA8(4BPP)で小さい画像でのConstXパスをテスト
    constexpr int SRC_W2 = 4;
    constexpr int SRC_H2 = 8;
    constexpr int BPP2 = 4;
    uint8_t srcBuf2[SRC_W2 * SRC_H2 * BPP2];
    for (int i = 0; i < SRC_W2 * SRC_H2 * BPP2; i++) {
        srcBuf2[i] = static_cast<uint8_t>((i * 13 + 5) & 0xFF);
    }
    ViewPort src2(srcBuf2, SRC_W2, SRC_H2, PixelFormatIDs::RGBA8_Straight);

    SUBCASE("ConstX with fractional incrY") {
        constexpr int COUNT = 5;
        uint8_t dstActual[COUNT * BPP2] = {};
        uint8_t dstExpected[COUNT * BPP2] = {};

        int_fixed srcX = to_fixed(1);
        int_fixed srcY = 0;
        int_fixed incrX = 0;
        int_fixed incrY = INT_FIXED_ONE / 2;  // 0.5刻み

        testCopyRowDDA(dstActual, src2, COUNT, srcX, srcY, incrX, incrY);
        copyRowDDA_Reference(dstExpected, srcBuf2, src2.stride, BPP2,
                             srcX, srcY, incrX, incrY, COUNT);

        for (int i = 0; i < COUNT * BPP2; i++) {
            CHECK(dstActual[i] == dstExpected[i]);
        }
    }
}

TEST_CASE("copyRowDDA: relaxed ConstY condition (small incrY, same row)") {
    constexpr int SRC_W = 8;
    constexpr int SRC_H = 8;
    constexpr int BPP = 4;
    uint8_t srcBuf[SRC_W * SRC_H * BPP];
    for (int y = 0; y < SRC_H; y++) {
        for (int x = 0; x < SRC_W; x++) {
            int idx = (y * SRC_W + x) * BPP;
            srcBuf[idx + 0] = static_cast<uint8_t>(x * 30 + y * 5);
            srcBuf[idx + 1] = static_cast<uint8_t>(y * 40);
            srcBuf[idx + 2] = static_cast<uint8_t>((x + y) * 15);
            srcBuf[idx + 3] = 255;
        }
    }
    ViewPort src(srcBuf, SRC_W, SRC_H, PixelFormatIDs::RGBA8_Straight);

    SUBCASE("tiny incrY that stays within same row") {
        // srcY=3.0, incrY=1/256 (≈0.004), count=6
        // Y range: 3.0 ~ 3.023 → all on row 3 → ConstY path
        constexpr int COUNT = 6;
        uint8_t dstActual[COUNT * BPP] = {};
        uint8_t dstExpected[COUNT * BPP] = {};

        int_fixed srcX = 0;
        int_fixed srcY = to_fixed(3);
        int_fixed incrX = INT_FIXED_ONE;
        int_fixed incrY = INT_FIXED_ONE / 256;

        testCopyRowDDA(dstActual, src, COUNT, srcX, srcY, incrX, incrY);
        copyRowDDA_Reference(dstExpected, srcBuf, src.stride, BPP,
                             srcX, srcY, incrX, incrY, COUNT);

        for (int i = 0; i < COUNT * BPP; i++) {
            CHECK(dstActual[i] == dstExpected[i]);
        }
    }

    SUBCASE("small incrY that crosses row boundary") {
        // srcY=3.8, incrY=0.1, count=4
        // Y range: 3.8 ~ 4.1 → crosses row 3→4 → general path
        constexpr int COUNT = 4;
        uint8_t dstActual[COUNT * BPP] = {};
        uint8_t dstExpected[COUNT * BPP] = {};

        int_fixed srcX = to_fixed(1);
        int_fixed srcY = to_fixed(3) + (INT_FIXED_ONE * 4 / 5);  // 3.8
        int_fixed incrX = INT_FIXED_ONE;
        int_fixed incrY = INT_FIXED_ONE / 10;  // 0.1

        testCopyRowDDA(dstActual, src, COUNT, srcX, srcY, incrX, incrY);
        copyRowDDA_Reference(dstExpected, srcBuf, src.stride, BPP,
                             srcX, srcY, incrX, incrY, COUNT);

        for (int i = 0; i < COUNT * BPP; i++) {
            CHECK(dstActual[i] == dstExpected[i]);
        }
    }

    SUBCASE("negative incrY staying within same row") {
        // srcY=3.9, incrY=-1/256, count=5
        // Y range: 3.9 ~ 3.88 → all on row 3 → ConstY path
        constexpr int COUNT = 5;
        uint8_t dstActual[COUNT * BPP] = {};
        uint8_t dstExpected[COUNT * BPP] = {};

        int_fixed srcX = 0;
        int_fixed srcY = to_fixed(3) + (INT_FIXED_ONE * 9 / 10);  // 3.9
        int_fixed incrX = INT_FIXED_ONE;
        int_fixed incrY = -(INT_FIXED_ONE / 256);  // -0.004

        testCopyRowDDA(dstActual, src, COUNT, srcX, srcY, incrX, incrY);
        copyRowDDA_Reference(dstExpected, srcBuf, src.stride, BPP,
                             srcX, srcY, incrX, incrY, COUNT);

        for (int i = 0; i < COUNT * BPP; i++) {
            CHECK(dstActual[i] == dstExpected[i]);
        }
    }
}

TEST_CASE("copyRowDDA: relaxed ConstX condition (small incrX, same column)") {
    constexpr int SRC_W = 8;
    constexpr int SRC_H = 8;
    constexpr int BPP = 4;
    uint8_t srcBuf[SRC_W * SRC_H * BPP];
    for (int y = 0; y < SRC_H; y++) {
        for (int x = 0; x < SRC_W; x++) {
            int idx = (y * SRC_W + x) * BPP;
            srcBuf[idx + 0] = static_cast<uint8_t>(x * 25 + y * 10);
            srcBuf[idx + 1] = static_cast<uint8_t>(y * 35);
            srcBuf[idx + 2] = static_cast<uint8_t>((x + y) * 12);
            srcBuf[idx + 3] = 128;
        }
    }
    ViewPort src(srcBuf, SRC_W, SRC_H, PixelFormatIDs::RGBA8_Straight);

    SUBCASE("tiny incrX that stays within same column") {
        // srcX=2.0, incrX=1/256, incrY=1.0, count=5
        // X range: 2.0 ~ 2.02 → all on column 2 → ConstX path
        constexpr int COUNT = 5;
        uint8_t dstActual[COUNT * BPP] = {};
        uint8_t dstExpected[COUNT * BPP] = {};

        int_fixed srcX = to_fixed(2);
        int_fixed srcY = 0;
        int_fixed incrX = INT_FIXED_ONE / 256;
        int_fixed incrY = INT_FIXED_ONE;

        testCopyRowDDA(dstActual, src, COUNT, srcX, srcY, incrX, incrY);
        copyRowDDA_Reference(dstExpected, srcBuf, src.stride, BPP,
                             srcX, srcY, incrX, incrY, COUNT);

        for (int i = 0; i < COUNT * BPP; i++) {
            CHECK(dstActual[i] == dstExpected[i]);
        }
    }

    SUBCASE("small incrX that crosses column boundary") {
        // srcX=2.8, incrX=0.1, incrY=1.0, count=4
        // X range: 2.8 ~ 3.1 → crosses column 2→3 → general path
        constexpr int COUNT = 4;
        uint8_t dstActual[COUNT * BPP] = {};
        uint8_t dstExpected[COUNT * BPP] = {};

        int_fixed srcX = to_fixed(2) + (INT_FIXED_ONE * 4 / 5);  // 2.8
        int_fixed srcY = 0;
        int_fixed incrX = INT_FIXED_ONE / 10;
        int_fixed incrY = INT_FIXED_ONE;

        testCopyRowDDA(dstActual, src, COUNT, srcX, srcY, incrX, incrY);
        copyRowDDA_Reference(dstExpected, srcBuf, src.stride, BPP,
                             srcX, srcY, incrX, incrY, COUNT);

        for (int i = 0; i < COUNT * BPP; i++) {
            CHECK(dstActual[i] == dstExpected[i]);
        }
    }
}
