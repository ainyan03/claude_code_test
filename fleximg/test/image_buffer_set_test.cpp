/**
 * @file image_buffer_set_test.cpp
 * @brief ImageBufferSet のテスト
 */

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/image/image_buffer_set.h"
#include "fleximg/image/image_buffer_entry_pool.h"

using namespace fleximg;
using namespace fleximg::core::memory;

// ============================================================================
// Phase 1: 基本構造テスト
// ============================================================================

TEST_CASE("ImageBufferSet basic construction") {
    DefaultAllocator& alloc = DefaultAllocator::instance();
    ImageBufferEntryPool pool;

    SUBCASE("default constructor") {
        ImageBufferSet set;
        CHECK(set.empty());
        CHECK(set.bufferCount() == 0);
        CHECK(set.allocator() == nullptr);
        CHECK(set.pool() == nullptr);
    }

    SUBCASE("constructor with pool and allocator") {
        ImageBufferSet set(&pool, &alloc);
        CHECK(set.empty());
        CHECK(set.bufferCount() == 0);
        CHECK(set.allocator() == &alloc);
        CHECK(set.pool() == &pool);
    }

    SUBCASE("setAllocator and setPool") {
        ImageBufferSet set;
        set.setAllocator(&alloc);
        set.setPool(&pool);
        CHECK(set.allocator() == &alloc);
        CHECK(set.pool() == &pool);
    }
}

TEST_CASE("ImageBufferSet move semantics") {
    DefaultAllocator& alloc = DefaultAllocator::instance();
    ImageBufferEntryPool pool;

    ImageBufferSet set1(&pool, &alloc);
    ImageBuffer buf(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
    set1.addBuffer(std::move(buf));
    CHECK(set1.bufferCount() == 1);

    SUBCASE("move constructor") {
        ImageBufferSet set2(std::move(set1));
        CHECK(set2.bufferCount() == 1);
        CHECK(set1.bufferCount() == 0);
    }

    SUBCASE("move assignment") {
        ImageBufferSet set2;
        set2 = std::move(set1);
        CHECK(set2.bufferCount() == 1);
        CHECK(set1.bufferCount() == 0);
    }
}

// ============================================================================
// Phase 2: バッファ登録テスト（重複なし）
// ============================================================================

TEST_CASE("ImageBufferSet addBuffer no overlap") {
    DefaultAllocator& alloc = DefaultAllocator::instance();
    ImageBufferEntryPool pool;
    ImageBufferSet set(&pool, &alloc);

    SUBCASE("single buffer") {
        ImageBuffer buf(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        CHECK(set.addBuffer(std::move(buf)));
        CHECK(set.bufferCount() == 1);
        CHECK(set.range(0).startX == 0);
        CHECK(set.range(0).endX == 10);
    }

    SUBCASE("multiple buffers no overlap - ascending order") {
        ImageBuffer buf1(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        ImageBuffer buf2(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        ImageBuffer buf3(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);

        buf2.setStartX(20);
        buf3.setStartX(40);
        CHECK(set.addBuffer(std::move(buf1)));
        CHECK(set.addBuffer(std::move(buf2)));
        CHECK(set.addBuffer(std::move(buf3)));

        CHECK(set.bufferCount() == 3);
        // ソート順を確認
        CHECK(set.range(0).startX == 0);
        CHECK(set.range(1).startX == 20);
        CHECK(set.range(2).startX == 40);
    }

    SUBCASE("multiple buffers no overlap - descending order insertion") {
        ImageBuffer buf1(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        ImageBuffer buf2(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        ImageBuffer buf3(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);

        buf1.setStartX(40);
        buf2.setStartX(20);
        CHECK(set.addBuffer(std::move(buf1)));
        CHECK(set.addBuffer(std::move(buf2)));
        CHECK(set.addBuffer(std::move(buf3)));

        CHECK(set.bufferCount() == 3);
        // ソート済みを確認
        CHECK(set.range(0).startX == 0);
        CHECK(set.range(1).startX == 20);
        CHECK(set.range(2).startX == 40);
    }

    SUBCASE("adjacent buffers (no gap, no overlap)") {
        ImageBuffer buf1(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        ImageBuffer buf2(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);

        buf2.setStartX(10);
        CHECK(set.addBuffer(std::move(buf1)));   // [0, 10)
        CHECK(set.addBuffer(std::move(buf2)));   // [10, 20)

        CHECK(set.bufferCount() == 2);
        CHECK(set.range(0).endX == 10);
        CHECK(set.range(1).startX == 10);
    }
}

TEST_CASE("ImageBufferSet totalRange") {
    DefaultAllocator& alloc = DefaultAllocator::instance();
    ImageBufferEntryPool pool;
    ImageBufferSet set(&pool, &alloc);

    SUBCASE("empty set") {
        DataRange r = set.totalRange();
        CHECK(r.startX == 0);
        CHECK(r.endX == 0);
    }

    SUBCASE("single buffer") {
        ImageBuffer buf(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        buf.setStartX(5);
        set.addBuffer(std::move(buf));
        DataRange r = set.totalRange();
        CHECK(r.startX == 5);
        CHECK(r.endX == 15);
    }

    SUBCASE("multiple buffers with gap") {
        ImageBuffer buf1(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        ImageBuffer buf2(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        buf2.setStartX(50);
        set.addBuffer(std::move(buf1));
        set.addBuffer(std::move(buf2));
        DataRange r = set.totalRange();
        CHECK(r.startX == 0);
        CHECK(r.endX == 60);
    }
}

TEST_CASE("ImageBufferSet clear") {
    DefaultAllocator& alloc = DefaultAllocator::instance();
    ImageBufferEntryPool pool;
    ImageBufferSet set(&pool, &alloc);

    ImageBuffer buf(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
    set.addBuffer(std::move(buf));
    CHECK(set.bufferCount() == 1);

    set.clear();
    CHECK(set.empty());
    CHECK(set.bufferCount() == 0);
}

TEST_CASE("ImageBufferSet invalid buffer rejected") {
    ImageBufferEntryPool pool;
    ImageBufferSet set(&pool);

    ImageBuffer invalidBuf;  // 無効なバッファ
    CHECK_FALSE(set.addBuffer(std::move(invalidBuf)));
    CHECK(set.empty());
}

// ============================================================================
// Phase 3: 重複合成テスト
// ============================================================================

TEST_CASE("ImageBufferSet overlap merging") {
    DefaultAllocator& alloc = DefaultAllocator::instance();
    ImageBufferEntryPool pool;
    ImageBufferSet set(&pool, &alloc);

    SUBCASE("simple overlap - merged to single buffer") {
        ImageBuffer buf1(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        ImageBuffer buf2(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);

        buf2.setStartX(5);
        set.addBuffer(std::move(buf1));   // [0, 10)
        set.addBuffer(std::move(buf2));   // [5, 15) - 重複

        // 重複があるので1つのバッファに合成される
        CHECK(set.bufferCount() == 1);
        CHECK(set.range(0).startX == 0);
        CHECK(set.range(0).endX == 15);
    }

    SUBCASE("complete overlap") {
        ImageBuffer buf1(20, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        ImageBuffer buf2(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);

        buf2.setStartX(5);
        set.addBuffer(std::move(buf1));   // [0, 20)
        set.addBuffer(std::move(buf2));   // [5, 15) - 完全に内包

        CHECK(set.bufferCount() == 1);
        CHECK(set.range(0).startX == 0);
        CHECK(set.range(0).endX == 20);
    }

    SUBCASE("multiple overlaps") {
        ImageBuffer buf1(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        ImageBuffer buf2(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        ImageBuffer buf3(20, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);

        buf2.setStartX(15);
        buf3.setStartX(5);
        set.addBuffer(std::move(buf1));    // [0, 10)
        set.addBuffer(std::move(buf2));    // [15, 25) - ギャップあり
        CHECK(set.bufferCount() == 2);

        set.addBuffer(std::move(buf3));    // [5, 25) - 両方と重複

        CHECK(set.bufferCount() == 1);
        CHECK(set.range(0).startX == 0);
        CHECK(set.range(0).endX == 25);
    }
}

TEST_CASE("ImageBufferSet overlap with pixel data") {
    DefaultAllocator& alloc = DefaultAllocator::instance();
    ImageBufferEntryPool pool;
    ImageBufferSet set(&pool, &alloc);

    // 赤色のバッファ（不透明）
    ImageBuffer redBuf(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
    uint8_t* redRow = static_cast<uint8_t*>(redBuf.view().pixelAt(0, 0));
    for (int i = 0; i < 10; ++i) {
        redRow[i * 4 + 0] = 255;  // R
        redRow[i * 4 + 1] = 0;    // G
        redRow[i * 4 + 2] = 0;    // B
        redRow[i * 4 + 3] = 255;  // A (不透明)
    }

    // 青色のバッファ（半透明）
    ImageBuffer blueBuf(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
    uint8_t* blueRow = static_cast<uint8_t*>(blueBuf.view().pixelAt(0, 0));
    for (int i = 0; i < 10; ++i) {
        blueRow[i * 4 + 0] = 0;    // R
        blueRow[i * 4 + 1] = 0;    // G
        blueRow[i * 4 + 2] = 255;  // B
        blueRow[i * 4 + 3] = 128;  // A (半透明)
    }

    blueBuf.setStartX(5);
    set.addBuffer(std::move(redBuf));    // [0, 10) 赤（前面）
    set.addBuffer(std::move(blueBuf));   // [5, 15) 青（背面、under合成）

    CHECK(set.bufferCount() == 1);
    CHECK(set.range(0).startX == 0);
    CHECK(set.range(0).endX == 15);

    // 合成結果を確認
    const uint8_t* resultRow = static_cast<const uint8_t*>(set.buffer(0).view().pixelAt(0, 0));

    // [0, 5): 赤のみ
    CHECK(resultRow[0] == 255);  // R
    CHECK(resultRow[3] == 255);  // A

    // [5, 10): 赤が前面で不透明なので、赤のまま
    CHECK(resultRow[5 * 4 + 0] == 255);  // R
    CHECK(resultRow[5 * 4 + 3] == 255);  // A (不透明)

    // [10, 15): 青のみ
    CHECK(resultRow[10 * 4 + 2] == 255);  // B
    CHECK(resultRow[10 * 4 + 3] == 128);  // A (半透明)
}

// ============================================================================
// Phase 4: consolidate テスト（基本）
// ============================================================================

TEST_CASE("ImageBufferSet consolidate single buffer") {
    DefaultAllocator& alloc = DefaultAllocator::instance();
    ImageBufferEntryPool pool;
    ImageBufferSet set(&pool, &alloc);

    ImageBuffer buf(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
    set.addBuffer(std::move(buf));

    ImageBuffer result = set.consolidate();
    CHECK(result.isValid());
    CHECK(result.width() == 10);
    CHECK(set.empty());  // consolidate後はクリアされる
}

TEST_CASE("ImageBufferSet consolidate empty") {
    ImageBufferSet set;
    ImageBuffer result = set.consolidate();
    CHECK_FALSE(result.isValid());
}

TEST_CASE("ImageBufferSet consolidate multiple buffers") {
    DefaultAllocator& alloc = DefaultAllocator::instance();
    ImageBufferEntryPool pool;
    ImageBufferSet set(&pool, &alloc);

    // 赤色バッファ
    ImageBuffer redBuf(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
    uint8_t* redRow = static_cast<uint8_t*>(redBuf.view().pixelAt(0, 0));
    for (int i = 0; i < 10; ++i) {
        redRow[i * 4 + 0] = 255;
        redRow[i * 4 + 3] = 255;
    }

    // 青色バッファ
    ImageBuffer blueBuf(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
    uint8_t* blueRow = static_cast<uint8_t*>(blueBuf.view().pixelAt(0, 0));
    for (int i = 0; i < 10; ++i) {
        blueRow[i * 4 + 2] = 255;
        blueRow[i * 4 + 3] = 255;
    }

    blueBuf.setStartX(20);
    set.addBuffer(std::move(redBuf));    // [0, 10)
    set.addBuffer(std::move(blueBuf));   // [20, 30) - ギャップあり

    CHECK(set.bufferCount() == 2);

    ImageBuffer result = set.consolidate();
    CHECK(result.isValid());
    CHECK(result.width() == 30);  // [0, 30)
    CHECK(set.empty());

    // 結果を確認
    const uint8_t* resultRow = static_cast<const uint8_t*>(result.view().pixelAt(0, 0));
    CHECK(resultRow[0] == 255);      // R at [0]
    CHECK(resultRow[10 * 4] == 0);   // Gap at [10]
    CHECK(resultRow[20 * 4 + 2] == 255);  // B at [20]
}

TEST_CASE("ImageBufferSet mergeAdjacent") {
    DefaultAllocator& alloc = DefaultAllocator::instance();
    ImageBufferEntryPool pool;
    ImageBufferSet set(&pool, &alloc);

    SUBCASE("adjacent buffers merged") {
        ImageBuffer buf1(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        ImageBuffer buf2(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);

        buf2.setStartX(10);
        set.addBuffer(std::move(buf1));    // [0, 10)
        set.addBuffer(std::move(buf2));    // [10, 20) - 隣接

        CHECK(set.bufferCount() == 2);

        set.mergeAdjacent(0);  // ギャップ0以下を統合

        CHECK(set.bufferCount() == 1);
        CHECK(set.range(0).startX == 0);
        CHECK(set.range(0).endX == 20);
    }

    SUBCASE("small gap merged with threshold") {
        ImageBuffer buf1(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        ImageBuffer buf2(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);

        buf2.setStartX(15);
        set.addBuffer(std::move(buf1));    // [0, 10)
        set.addBuffer(std::move(buf2));    // [15, 25) - 5ピクセルギャップ

        CHECK(set.bufferCount() == 2);

        set.mergeAdjacent(8);  // ギャップ8以下を統合

        CHECK(set.bufferCount() == 1);
        CHECK(set.range(0).startX == 0);
        CHECK(set.range(0).endX == 25);
    }

    SUBCASE("large gap not merged") {
        ImageBuffer buf1(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);
        ImageBuffer buf2(10, 1, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero, &alloc);

        buf2.setStartX(50);
        set.addBuffer(std::move(buf1));    // [0, 10)
        set.addBuffer(std::move(buf2));    // [50, 60) - 40ピクセルギャップ

        CHECK(set.bufferCount() == 2);

        set.mergeAdjacent(8);  // ギャップ8以下を統合

        CHECK(set.bufferCount() == 2);  // 統合されない
    }
}
