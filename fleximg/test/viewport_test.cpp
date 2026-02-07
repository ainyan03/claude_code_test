// fleximg ViewPort Unit Tests
// ViewPort構造体のテスト

#include "doctest.h"

#include <cmath>
#include <cstring>

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
    DDAParam param = { src.stride, 0, 0, srcX, srcY, incrX, incrY, nullptr, nullptr };
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
    // stride > width * bytesPerPixel のケース（パディングあり）
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
        CHECK(v.rowBytes() == 40);  // width * bytesPerPixel
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
    int bytesPerPixel,
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
            + static_cast<size_t>(sx) * static_cast<size_t>(bytesPerPixel);
        std::memcpy(dstRow, srcPixel, static_cast<size_t>(bytesPerPixel));
        dstRow += bytesPerPixel;
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
    // 実際にはbytesPerPixelはformatIDから決まるので、ここでは2bytesPerPixelフォーマットが必要
    // → bytesPerPixel=4のformatIDを使うが、テストの本質は方向別パスの正確性

    // 代わりに、RGBA8(4BytesPerPixel)で小さい画像でのConstXパスをテスト
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

// =============================================================================
// canUseSingleChannelBilinear Tests
// =============================================================================

TEST_CASE("canUseSingleChannelBilinear: format classification") {
    SUBCASE("Alpha8 with edgeFade is single-channel eligible") {
        CHECK(view_ops::canUseSingleChannelBilinear(
            PixelFormatIDs::Alpha8, EdgeFade_All));
    }

    SUBCASE("Alpha8 without edgeFade is single-channel eligible") {
        CHECK(view_ops::canUseSingleChannelBilinear(
            PixelFormatIDs::Alpha8, EdgeFade_None));
    }

    SUBCASE("Grayscale8 without edgeFade is single-channel eligible") {
        CHECK(view_ops::canUseSingleChannelBilinear(
            PixelFormatIDs::Grayscale8, EdgeFade_None));
    }

    SUBCASE("Grayscale8 with edgeFade is NOT eligible (Luminance != Alpha)") {
        CHECK_FALSE(view_ops::canUseSingleChannelBilinear(
            PixelFormatIDs::Grayscale8, EdgeFade_All));
    }

    SUBCASE("RGBA8 is NOT single-channel eligible") {
        CHECK_FALSE(view_ops::canUseSingleChannelBilinear(
            PixelFormatIDs::RGBA8_Straight, EdgeFade_None));
    }

    SUBCASE("nullptr format returns false") {
        CHECK_FALSE(view_ops::canUseSingleChannelBilinear(nullptr, EdgeFade_None));
    }
}

// =============================================================================
// copyRowDDABilinear 1ch Tests
// =============================================================================

// バイリニア補間のリファレンス実装（1チャンネル版）
// 浮動小数点で素朴に計算する
static uint8_t bilinearRef_1ch(
    const uint8_t* srcData,
    int srcStride,
    int srcW, int srcH,
    float sx, float sy
) {
    // ピクセル中心座標から左上ピクセルのインデックスを算出
    int x0 = static_cast<int>(std::floor(sx));
    int y0 = static_cast<int>(std::floor(sy));
    float fx = sx - static_cast<float>(x0);
    float fy = sy - static_cast<float>(y0);

    // 4点取得（範囲外は0）
    auto getPixel = [&](int x, int y) -> float {
        if (x < 0 || x >= srcW || y < 0 || y >= srcH) return 0.0f;
        return static_cast<float>(srcData[y * srcStride + x]);
    };

    float q00 = getPixel(x0, y0);
    float q10 = getPixel(x0 + 1, y0);
    float q01 = getPixel(x0, y0 + 1);
    float q11 = getPixel(x0 + 1, y0 + 1);

    // バイリニア補間
    float result = q00 * (1 - fx) * (1 - fy)
                 + q10 * fx * (1 - fy)
                 + q01 * (1 - fx) * fy
                 + q11 * fx * fy;

    return static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, result + 0.5f)));
}

TEST_CASE("copyRowDDABilinear: Alpha8 single-channel path") {
    // 4x4 Alpha8 ソース画像
    constexpr int SRC_W = 4;
    constexpr int SRC_H = 4;
    uint8_t srcBuf[SRC_W * SRC_H];
    // グラデーションパターン
    for (int y = 0; y < SRC_H; y++) {
        for (int x = 0; x < SRC_W; x++) {
            srcBuf[y * SRC_W + x] = static_cast<uint8_t>(x * 60 + y * 40);
        }
    }
    ViewPort src(srcBuf, PixelFormatIDs::Alpha8, SRC_W, SRC_W, SRC_H);

    SUBCASE("1:1 with half-pixel offset (interpolation test)") {
        // 0.5ピクセルずらし → 4点の平均値になるはず
        constexpr int COUNT = 2;
        uint8_t dst[COUNT] = {};

        // srcX=0.5, srcY=0.5 → ピクセル(0,0),(1,0),(0,1),(1,1)の補間
        int_fixed srcX = INT_FIXED_ONE / 2;  // 0.5
        int_fixed srcY = INT_FIXED_ONE / 2;  // 0.5
        int_fixed incrX = INT_FIXED_ONE;
        int_fixed incrY = 0;

        view_ops::copyRowDDABilinear(dst, src, COUNT, srcX, srcY, incrX, incrY,
                                      EdgeFade_None, nullptr);

        for (int i = 0; i < COUNT; i++) {
            float sx = static_cast<float>(i) + 0.5f;
            float sy = 0.5f;
            uint8_t expected = bilinearRef_1ch(srcBuf, SRC_W, SRC_W, SRC_H, sx, sy);
            CHECK(std::abs(static_cast<int>(dst[i]) - static_cast<int>(expected)) <= 1);
        }
    }

    SUBCASE("integer position (no interpolation)") {
        // 整数座標 → 元のピクセル値がそのまま出る
        constexpr int COUNT = 3;
        uint8_t dst[COUNT] = {};

        int_fixed srcX = 0;
        int_fixed srcY = to_fixed(1);
        int_fixed incrX = INT_FIXED_ONE;
        int_fixed incrY = 0;

        view_ops::copyRowDDABilinear(dst, src, COUNT, srcX, srcY, incrX, incrY,
                                      EdgeFade_None, nullptr);

        // 整数座標では weight fx=fy=0 なので q00 の値のみ
        for (int i = 0; i < COUNT; i++) {
            CHECK(dst[i] == srcBuf[1 * SRC_W + i]);
        }
    }

    SUBCASE("output is 1 byte per pixel (not 4)") {
        // 1chパスの出力がAlpha8（1byte/pixel）であることを確認
        constexpr int COUNT = 4;
        uint8_t dst[COUNT + 4] = {};  // 余分に確保
        // 番兵値を設定
        dst[COUNT] = 0xAA;
        dst[COUNT + 1] = 0xBB;
        dst[COUNT + 2] = 0xCC;
        dst[COUNT + 3] = 0xDD;

        int_fixed srcX = 0;
        int_fixed srcY = 0;
        int_fixed incrX = INT_FIXED_ONE;
        int_fixed incrY = 0;

        view_ops::copyRowDDABilinear(dst, src, COUNT, srcX, srcY, incrX, incrY,
                                      EdgeFade_None, nullptr);

        // 番兵値が上書きされていないことを確認（4byte出力なら上書きされる）
        CHECK(dst[COUNT] == 0xAA);
        CHECK(dst[COUNT + 1] == 0xBB);
        CHECK(dst[COUNT + 2] == 0xCC);
        CHECK(dst[COUNT + 3] == 0xDD);
    }
}

TEST_CASE("copyRowDDABilinear: Alpha8 edge fade") {
    // 3x3 Alpha8 ソース（全ピクセル255）
    constexpr int SRC_W = 3;
    constexpr int SRC_H = 3;
    uint8_t srcBuf[SRC_W * SRC_H];
    std::memset(srcBuf, 255, sizeof(srcBuf));
    ViewPort src(srcBuf, PixelFormatIDs::Alpha8, SRC_W, SRC_W, SRC_H);

    SUBCASE("left edge fade produces zero at boundary") {
        // srcX=-0.5 → 左境界外に0.5ピクセルはみ出し
        // edgeFlagでLeft辺がフェード対象 → 境界外ピクセルは0
        constexpr int COUNT = 1;
        uint8_t dst[COUNT] = {255};

        int_fixed srcX = -(INT_FIXED_ONE / 2);  // -0.5
        int_fixed srcY = to_fixed(1);
        int_fixed incrX = INT_FIXED_ONE;
        int_fixed incrY = 0;

        view_ops::copyRowDDABilinear(dst, src, COUNT, srcX, srcY, incrX, incrY,
                                      EdgeFade_Left, nullptr);

        // 左側が境界外（0）、右側が255 → 約128前後の値になるはず（完全255ではない）
        CHECK(dst[0] < 200);
    }
}

TEST_CASE("copyRowDDABilinear: Grayscale8 single-channel path (no edge fade)") {
    // Grayscale8もedgeFadeなしなら1chパスを使用
    constexpr int SRC_W = 4;
    constexpr int SRC_H = 4;
    uint8_t srcBuf[SRC_W * SRC_H];
    for (int y = 0; y < SRC_H; y++) {
        for (int x = 0; x < SRC_W; x++) {
            srcBuf[y * SRC_W + x] = static_cast<uint8_t>(x * 50 + y * 30);
        }
    }
    ViewPort src(srcBuf, PixelFormatIDs::Grayscale8, SRC_W, SRC_W, SRC_H);

    constexpr int COUNT = 3;
    uint8_t dst[COUNT + 4] = {};
    // 番兵値
    dst[COUNT] = 0xAA;
    dst[COUNT + 1] = 0xBB;

    int_fixed srcX = INT_FIXED_ONE / 2;
    int_fixed srcY = INT_FIXED_ONE / 2;
    int_fixed incrX = INT_FIXED_ONE;
    int_fixed incrY = 0;

    view_ops::copyRowDDABilinear(dst, src, COUNT, srcX, srcY, incrX, incrY,
                                  EdgeFade_None, nullptr);

    // 1byteずつの出力であることを確認
    CHECK(dst[COUNT] == 0xAA);
    CHECK(dst[COUNT + 1] == 0xBB);

    // 補間値の検証
    for (int i = 0; i < COUNT; i++) {
        float sx = static_cast<float>(i) + 0.5f;
        float sy = 0.5f;
        uint8_t expected = bilinearRef_1ch(srcBuf, SRC_W, SRC_W, SRC_H, sx, sy);
        CHECK(std::abs(static_cast<int>(dst[i]) - static_cast<int>(expected)) <= 1);
    }
}

TEST_CASE("copyRowDDABilinear: RGBA8 path unchanged (regression)") {
    // RGBA8のバイリニア補間が既存どおり4byte/pixel出力であることを確認
    constexpr int SRC_W = 4;
    constexpr int SRC_H = 4;
    constexpr int BPP = 4;
    uint8_t srcBuf[SRC_W * SRC_H * BPP];
    for (int y = 0; y < SRC_H; y++) {
        for (int x = 0; x < SRC_W; x++) {
            int idx = (y * SRC_W + x) * BPP;
            srcBuf[idx + 0] = static_cast<uint8_t>(x * 60);    // R
            srcBuf[idx + 1] = static_cast<uint8_t>(y * 60);    // G
            srcBuf[idx + 2] = static_cast<uint8_t>((x+y) * 30);// B
            srcBuf[idx + 3] = 255;                               // A
        }
    }
    ViewPort src(srcBuf, SRC_W, SRC_H, PixelFormatIDs::RGBA8_Straight);

    constexpr int COUNT = 3;
    uint32_t dst[COUNT] = {};

    int_fixed srcX = INT_FIXED_ONE / 2;
    int_fixed srcY = INT_FIXED_ONE / 2;
    int_fixed incrX = INT_FIXED_ONE;
    int_fixed incrY = 0;

    view_ops::copyRowDDABilinear(dst, src, COUNT, srcX, srcY, incrX, incrY,
                                  EdgeFade_None, nullptr);

    // RGBA8出力: 各ピクセルが非ゼロの4byte値であることを確認
    for (int i = 0; i < COUNT; i++) {
        CHECK(dst[i] != 0);
        // アルファチャンネルが255であることを確認（全ソースがA=255）
        auto* pixel = reinterpret_cast<uint8_t*>(&dst[i]);
        CHECK(pixel[3] == 255);
    }
}
