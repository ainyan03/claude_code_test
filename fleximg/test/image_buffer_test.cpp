// fleximg ImageBuffer Unit Tests
// メモリ所有画像クラスのテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/image/image_buffer.h"

using namespace fleximg;

// =============================================================================
// Construction Tests
// =============================================================================

TEST_CASE("ImageBuffer default construction") {
    ImageBuffer buf;
    CHECK(buf.data() == nullptr);
    CHECK(buf.width() == 0);
    CHECK(buf.height() == 0);
    CHECK_FALSE(buf.isValid());
    // 注: デフォルトコンストラクタはallocatorを持つがdataはnull
    // ownsMemory()はallocator != nullptrをチェックするのでtrue
    CHECK(buf.ownsMemory());
}

TEST_CASE("ImageBuffer sized construction") {
    SUBCASE("RGBA8 format") {
        ImageBuffer buf(100, 50, PixelFormatIDs::RGBA8_Straight);

        CHECK(buf.data() != nullptr);
        CHECK(buf.width() == 100);
        CHECK(buf.height() == 50);
        CHECK(buf.formatID() == PixelFormatIDs::RGBA8_Straight);
        CHECK(buf.isValid());
        CHECK(buf.ownsMemory());
        CHECK(buf.bytesPerPixel() == 4);
        CHECK(buf.stride() == 400);  // 100 * 4
    }

    SUBCASE("RGBA16 format") {
        ImageBuffer buf(64, 64, PixelFormatIDs::RGBA16_Premultiplied);

        CHECK(buf.data() != nullptr);
        CHECK(buf.width() == 64);
        CHECK(buf.height() == 64);
        CHECK(buf.formatID() == PixelFormatIDs::RGBA16_Premultiplied);
        CHECK(buf.bytesPerPixel() == 8);
        CHECK(buf.stride() == 512);  // 64 * 8
    }
}

TEST_CASE("ImageBuffer reference mode") {
    uint8_t externalBuffer[400] = {0};  // 10x10 RGBA8
    ViewPort externalView(externalBuffer, 10, 10, PixelFormatIDs::RGBA8_Straight);

    ImageBuffer ref(externalView);

    CHECK(ref.data() == externalBuffer);
    CHECK(ref.width() == 10);
    CHECK(ref.height() == 10);
    CHECK(ref.isValid());
    CHECK_FALSE(ref.ownsMemory());  // 参照モード
}

// =============================================================================
// Memory and Size Tests
// =============================================================================

TEST_CASE("ImageBuffer totalBytes") {
    SUBCASE("RGBA8 100x50") {
        ImageBuffer buf(100, 50, PixelFormatIDs::RGBA8_Straight);
        CHECK(buf.totalBytes() == 100 * 50 * 4);
    }

    SUBCASE("RGBA16 64x64") {
        ImageBuffer buf(64, 64, PixelFormatIDs::RGBA16_Premultiplied);
        CHECK(buf.totalBytes() == 64 * 64 * 8);
    }
}

TEST_CASE("ImageBuffer InitPolicy") {
    SUBCASE("Zero initialization") {
        ImageBuffer buf(10, 10, PixelFormatIDs::RGBA8_Straight, InitPolicy::Zero);
        const uint8_t* data = static_cast<const uint8_t*>(buf.data());
        bool allZero = true;
        for (size_t i = 0; i < buf.totalBytes(); ++i) {
            if (data[i] != 0) { allZero = false; break; }
        }
        CHECK(allZero);
    }

    SUBCASE("DebugPattern initialization") {
        ImageBuffer buf(10, 10, PixelFormatIDs::RGBA8_Straight, InitPolicy::DebugPattern);
        const uint8_t* data = static_cast<const uint8_t*>(buf.data());
        // パターン値は連番なので、最初のバイトが0でないことを確認
        // （連続確保時はカウンタが進む）
        CHECK(data[0] != 0);  // デバッグパターンで埋まっている
    }
}

// =============================================================================
// Move Semantics Tests
// =============================================================================

TEST_CASE("ImageBuffer move construction") {
    ImageBuffer original(100, 50, PixelFormatIDs::RGBA8_Straight);
    void* originalData = original.data();

    ImageBuffer moved(std::move(original));

    CHECK(moved.data() == originalData);
    CHECK(moved.width() == 100);
    CHECK(moved.height() == 50);
    CHECK(moved.isValid());

    CHECK(original.data() == nullptr);
    CHECK_FALSE(original.isValid());
}

TEST_CASE("ImageBuffer move assignment") {
    ImageBuffer original(100, 50, PixelFormatIDs::RGBA8_Straight);
    void* originalData = original.data();

    ImageBuffer target;
    target = std::move(original);

    CHECK(target.data() == originalData);
    CHECK(target.width() == 100);
    CHECK(target.height() == 50);
    CHECK(target.isValid());

    CHECK(original.data() == nullptr);
    CHECK_FALSE(original.isValid());
}

// =============================================================================
// Copy Semantics Tests
// =============================================================================

TEST_CASE("ImageBuffer copy construction") {
    ImageBuffer original(10, 10, PixelFormatIDs::RGBA8_Straight);
    // データを書き込む
    uint8_t* data = static_cast<uint8_t*>(original.data());
    for (int i = 0; i < 10 * 10 * 4; ++i) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }

    ImageBuffer copied(original);

    CHECK(copied.data() != original.data());  // 異なるメモリ
    CHECK(copied.width() == original.width());
    CHECK(copied.height() == original.height());
    CHECK(copied.formatID() == original.formatID());
    CHECK(copied.ownsMemory());

    // データが一致することを確認
    const uint8_t* origData = static_cast<const uint8_t*>(original.data());
    const uint8_t* copyData = static_cast<const uint8_t*>(copied.data());
    bool dataMatch = true;
    for (size_t i = 0; i < original.totalBytes(); ++i) {
        if (origData[i] != copyData[i]) { dataMatch = false; break; }
    }
    CHECK(dataMatch);
}

TEST_CASE("ImageBuffer copy from reference mode") {
    uint8_t externalBuffer[400] = {0};
    for (int i = 0; i < 400; ++i) externalBuffer[i] = static_cast<uint8_t>(i);

    ViewPort externalView(externalBuffer, 10, 10, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer ref(externalView);

    // 参照モードからコピー → 所有モードになる
    ImageBuffer copied(ref);

    CHECK(copied.data() != externalBuffer);
    CHECK(copied.ownsMemory());

    // データが一致
    const uint8_t* copyData = static_cast<const uint8_t*>(copied.data());
    bool dataMatch = true;
    for (int i = 0; i < 400; ++i) {
        if (copyData[i] != externalBuffer[i]) { dataMatch = false; break; }
    }
    CHECK(dataMatch);
}

// =============================================================================
// View Access Tests
// =============================================================================

TEST_CASE("ImageBuffer view access") {
    ImageBuffer buf(10, 10, PixelFormatIDs::RGBA8_Straight);

    SUBCASE("view() returns copy") {
        ViewPort v = buf.view();
        CHECK(v.data == buf.data());
        CHECK(v.width == buf.width());
        CHECK(v.height == buf.height());
    }

    SUBCASE("viewRef() returns reference") {
        ViewPort& vref = buf.viewRef();
        CHECK(&vref.data == &buf.viewRef().data);
    }

    SUBCASE("subView") {
        ViewPort sub = buf.subView(2, 2, 5, 5);
        CHECK(sub.data == buf.pixelAt(2, 2));
        CHECK(sub.width == 5);
        CHECK(sub.height == 5);
    }

    SUBCASE("subBuffer") {
        ImageBuffer sub = buf.subBuffer(2, 2, 5, 5);
        CHECK(sub.data() == buf.pixelAt(2, 2));
        CHECK(sub.width() == 5);
        CHECK(sub.height() == 5);
        CHECK_FALSE(sub.ownsMemory());  // 参照モード
    }
}

// =============================================================================
// Pixel Access Tests
// =============================================================================

TEST_CASE("ImageBuffer pixel access") {
    ImageBuffer buf(10, 10, PixelFormatIDs::RGBA8_Straight);

    SUBCASE("pixelAt returns correct address") {
        CHECK(buf.pixelAt(0, 0) == buf.data());
        CHECK(buf.pixelAt(1, 0) == static_cast<uint8_t*>(buf.data()) + 4);
        CHECK(buf.pixelAt(0, 1) == static_cast<uint8_t*>(buf.data()) + buf.stride());
    }

    SUBCASE("write and read pixel") {
        uint8_t* pixel = static_cast<uint8_t*>(buf.pixelAt(5, 5));
        pixel[0] = 255;  // R
        pixel[1] = 128;  // G
        pixel[2] = 64;   // B
        pixel[3] = 255;  // A

        const uint8_t* readPixel = static_cast<const uint8_t*>(buf.pixelAt(5, 5));
        CHECK(readPixel[0] == 255);
        CHECK(readPixel[1] == 128);
        CHECK(readPixel[2] == 64);
        CHECK(readPixel[3] == 255);
    }
}
