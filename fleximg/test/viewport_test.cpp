// ViewPort Unit Tests
// Tests for the unified image type (ViewPort)

#include <gtest/gtest.h>
#include "fleximg/viewport.h"
#include "fleximg/pixel_format.h"
#include "fleximg/image_types.h"

using namespace FLEXIMG_NAMESPACE;

// ==============================================================================
// Construction Tests
// ==============================================================================

TEST(ViewPortTest, DefaultConstruction) {
    ViewPort vp;
    EXPECT_EQ(vp.data, nullptr);
    EXPECT_EQ(vp.width, 0);
    EXPECT_EQ(vp.height, 0);
    EXPECT_FALSE(vp.isValid());
}

TEST(ViewPortTest, SizedConstruction_RGBA8) {
    ViewPort vp(100, 50, PixelFormatIDs::RGBA8_Straight);

    EXPECT_NE(vp.data, nullptr);
    EXPECT_EQ(vp.width, 100);
    EXPECT_EQ(vp.height, 50);
    EXPECT_EQ(vp.formatID, PixelFormatIDs::RGBA8_Straight);
    EXPECT_TRUE(vp.isValid());
    EXPECT_TRUE(vp.isRootImage());
    EXPECT_FALSE(vp.isSubView());
    EXPECT_TRUE(vp.ownsData);
}

TEST(ViewPortTest, SizedConstruction_RGBA16) {
    ViewPort vp(64, 64, PixelFormatIDs::RGBA16_Premultiplied);

    EXPECT_NE(vp.data, nullptr);
    EXPECT_EQ(vp.width, 64);
    EXPECT_EQ(vp.height, 64);
    EXPECT_EQ(vp.formatID, PixelFormatIDs::RGBA16_Premultiplied);
    EXPECT_TRUE(vp.isValid());

    // RGBA16 = 8 bytes per pixel
    EXPECT_EQ(vp.getBytesPerPixel(), 8);
}

// ==============================================================================
// Memory Layout Tests
// ==============================================================================

TEST(ViewPortTest, BytesPerPixel_RGBA8) {
    ViewPort vp(10, 10, PixelFormatIDs::RGBA8_Straight);
    EXPECT_EQ(vp.getBytesPerPixel(), 4);
}

TEST(ViewPortTest, BytesPerPixel_RGBA16) {
    ViewPort vp(10, 10, PixelFormatIDs::RGBA16_Premultiplied);
    EXPECT_EQ(vp.getBytesPerPixel(), 8);
}

TEST(ViewPortTest, StrideCalculation) {
    ViewPort vp(100, 50, PixelFormatIDs::RGBA8_Straight);
    // stride should be at least width * bytesPerPixel
    EXPECT_GE(vp.stride, static_cast<size_t>(100 * 4));
}

TEST(ViewPortTest, TotalBytes) {
    ViewPort vp(100, 50, PixelFormatIDs::RGBA8_Straight);
    // Total bytes = stride * height
    EXPECT_EQ(vp.getTotalBytes(), vp.stride * 50);
}

// ==============================================================================
// Pixel Access Tests
// ==============================================================================

TEST(ViewPortTest, PixelAccess_RGBA8) {
    ViewPort vp(10, 10, PixelFormatIDs::RGBA8_Straight);

    // Write pixel at (5, 3)
    uint8_t* pixel = vp.getPixelPtr<uint8_t>(5, 3);
    pixel[0] = 255;  // R
    pixel[1] = 128;  // G
    pixel[2] = 64;   // B
    pixel[3] = 200;  // A

    // Read back
    const uint8_t* readPixel = vp.getPixelPtr<uint8_t>(5, 3);
    EXPECT_EQ(readPixel[0], 255);
    EXPECT_EQ(readPixel[1], 128);
    EXPECT_EQ(readPixel[2], 64);
    EXPECT_EQ(readPixel[3], 200);
}

TEST(ViewPortTest, PixelAccess_RGBA16) {
    ViewPort vp(10, 10, PixelFormatIDs::RGBA16_Premultiplied);

    // Write pixel at (2, 7)
    uint16_t* pixel = vp.getPixelPtr<uint16_t>(2, 7);
    pixel[0] = 65535;  // R
    pixel[1] = 32768;  // G
    pixel[2] = 16384;  // B
    pixel[3] = 49152;  // A

    // Read back
    const uint16_t* readPixel = vp.getPixelPtr<uint16_t>(2, 7);
    EXPECT_EQ(readPixel[0], 65535);
    EXPECT_EQ(readPixel[1], 32768);
    EXPECT_EQ(readPixel[2], 16384);
    EXPECT_EQ(readPixel[3], 49152);
}

// ==============================================================================
// Copy Semantics Tests
// ==============================================================================

TEST(ViewPortTest, CopyConstruction) {
    ViewPort original(50, 50, PixelFormatIDs::RGBA8_Straight);

    // Write test pattern
    uint8_t* pixel = original.getPixelPtr<uint8_t>(10, 10);
    pixel[0] = 123;
    pixel[1] = 45;
    pixel[2] = 67;
    pixel[3] = 89;

    // Copy construct
    ViewPort copy(original);

    // Verify copy has same dimensions
    EXPECT_EQ(copy.width, 50);
    EXPECT_EQ(copy.height, 50);
    EXPECT_EQ(copy.formatID, PixelFormatIDs::RGBA8_Straight);
    EXPECT_TRUE(copy.isValid());

    // Verify data is independent (different pointer)
    EXPECT_NE(copy.data, original.data);

    // Verify data was copied
    const uint8_t* copyPixel = copy.getPixelPtr<uint8_t>(10, 10);
    EXPECT_EQ(copyPixel[0], 123);
    EXPECT_EQ(copyPixel[1], 45);
    EXPECT_EQ(copyPixel[2], 67);
    EXPECT_EQ(copyPixel[3], 89);

    // Modify original, verify copy unchanged
    pixel[0] = 0;
    EXPECT_EQ(copyPixel[0], 123);  // Copy should still be 123
}

TEST(ViewPortTest, MoveConstruction) {
    ViewPort original(30, 30, PixelFormatIDs::RGBA8_Straight);
    void* originalData = original.data;

    ViewPort moved(std::move(original));

    // Moved-to object has the data
    EXPECT_EQ(moved.data, originalData);
    EXPECT_EQ(moved.width, 30);
    EXPECT_EQ(moved.height, 30);
    EXPECT_TRUE(moved.isValid());

    // Moved-from object is empty
    EXPECT_EQ(original.data, nullptr);
    EXPECT_FALSE(original.isValid());
}

// ==============================================================================
// SubView Tests
// ==============================================================================

TEST(ViewPortTest, SubView_Creation) {
    ViewPort root(100, 100, PixelFormatIDs::RGBA8_Straight);

    ViewPort sub = root.createSubView(10, 20, 30, 40);

    EXPECT_TRUE(sub.isValid());
    EXPECT_TRUE(sub.isSubView());
    EXPECT_FALSE(sub.isRootImage());
    EXPECT_FALSE(sub.ownsData);
    EXPECT_EQ(sub.width, 30);
    EXPECT_EQ(sub.height, 40);
    EXPECT_EQ(sub.offsetX, 10);
    EXPECT_EQ(sub.offsetY, 20);
}

TEST(ViewPortTest, SubView_PixelAccess) {
    ViewPort root(100, 100, PixelFormatIDs::RGBA8_Straight);

    // Write to root at (15, 25)
    uint8_t* rootPixel = root.getPixelPtr<uint8_t>(15, 25);
    rootPixel[0] = 111;
    rootPixel[1] = 222;

    // Create subview starting at (10, 20)
    ViewPort sub = root.createSubView(10, 20, 30, 40);

    // SubView (5, 5) should map to root (15, 25)
    const uint8_t* subPixel = sub.getPixelPtr<uint8_t>(5, 5);
    EXPECT_EQ(subPixel[0], 111);
    EXPECT_EQ(subPixel[1], 222);

    // Write through subview
    uint8_t* subWritePixel = sub.getPixelPtr<uint8_t>(5, 5);
    subWritePixel[2] = 99;

    // Verify root is modified
    EXPECT_EQ(rootPixel[2], 99);
}

// ==============================================================================
// Format Conversion Tests
// ==============================================================================

TEST(ViewPortTest, ConvertTo_SameFormat) {
    ViewPort vp(20, 20, PixelFormatIDs::RGBA8_Straight);

    // Set test data
    uint8_t* pixel = vp.getPixelPtr<uint8_t>(5, 5);
    pixel[0] = 100;
    pixel[1] = 150;
    pixel[2] = 200;
    pixel[3] = 255;

    // Convert to same format (should copy)
    ViewPort converted = vp.convertTo(PixelFormatIDs::RGBA8_Straight);

    EXPECT_EQ(converted.formatID, PixelFormatIDs::RGBA8_Straight);
    EXPECT_NE(converted.data, vp.data);  // Should be a copy

    const uint8_t* convPixel = converted.getPixelPtr<uint8_t>(5, 5);
    EXPECT_EQ(convPixel[0], 100);
    EXPECT_EQ(convPixel[1], 150);
    EXPECT_EQ(convPixel[2], 200);
    EXPECT_EQ(convPixel[3], 255);
}

TEST(ViewPortTest, ConvertTo_RGBA8_to_RGBA16Premul) {
    ViewPort vp(10, 10, PixelFormatIDs::RGBA8_Straight);

    // Set opaque red pixel
    uint8_t* pixel = vp.getPixelPtr<uint8_t>(3, 3);
    pixel[0] = 255;  // R
    pixel[1] = 0;    // G
    pixel[2] = 0;    // B
    pixel[3] = 255;  // A (opaque)

    ViewPort converted = vp.convertTo(PixelFormatIDs::RGBA16_Premultiplied);

    EXPECT_EQ(converted.formatID, PixelFormatIDs::RGBA16_Premultiplied);
    EXPECT_EQ(converted.width, 10);
    EXPECT_EQ(converted.height, 10);

    // 新方式: A_tmp = A8 + 1 = 256
    // R16 = R8 * A_tmp = 255 * 256 = 65280
    // A16 = 255 * A_tmp = 255 * 256 = 65280
    const uint16_t* convPixel = converted.getPixelPtr<uint16_t>(3, 3);
    EXPECT_EQ(convPixel[0], 65280);  // R = 255 * 256
    EXPECT_EQ(convPixel[1], 0);      // G = 0 * 256
    EXPECT_EQ(convPixel[2], 0);      // B = 0 * 256
    EXPECT_EQ(convPixel[3], 65280);  // A = 255 * 256 (ALPHA_OPAQUE_MIN)
}

TEST(ViewPortTest, ConvertTo_RGBA16Premul_to_RGBA8) {
    ViewPort vp(10, 10, PixelFormatIDs::RGBA16_Premultiplied);

    // 新方式で作られた不透明緑ピクセル (A8=255 → A16=65280)
    uint16_t* pixel = vp.getPixelPtr<uint16_t>(4, 4);
    pixel[0] = 0;      // R = 0 * 256 = 0
    pixel[1] = 65280;  // G = 255 * 256 = 65280
    pixel[2] = 0;      // B = 0 * 256 = 0
    pixel[3] = 65280;  // A = 255 * 256 = 65280 (ALPHA_OPAQUE_MIN)

    ViewPort converted = vp.convertTo(PixelFormatIDs::RGBA8_Straight);

    EXPECT_EQ(converted.formatID, PixelFormatIDs::RGBA8_Straight);

    // Reverse変換: A8 = 65280 >> 8 = 255, A_tmp = 256
    // G8 = 65280 / 256 = 255
    const uint8_t* convPixel = converted.getPixelPtr<uint8_t>(4, 4);
    EXPECT_EQ(convPixel[0], 0);    // R = 0 / 256 = 0
    EXPECT_EQ(convPixel[1], 255);  // G = 65280 / 256 = 255
    EXPECT_EQ(convPixel[2], 0);    // B = 0 / 256 = 0
    EXPECT_EQ(convPixel[3], 255);  // A = 65280 >> 8 = 255
}

// ==============================================================================
// 新アルファ変換方式のテスト
// ==============================================================================

TEST(ViewPortTest, AlphaConversion_TransparentPreservesRGB) {
    // 透明ピクセル（A8=0）でもRGB情報が保持されることを確認
    ViewPort vp(10, 10, PixelFormatIDs::RGBA8_Straight);

    uint8_t* pixel = vp.getPixelPtr<uint8_t>(5, 5);
    pixel[0] = 255;  // R
    pixel[1] = 128;  // G
    pixel[2] = 64;   // B
    pixel[3] = 0;    // A (transparent)

    ViewPort converted = vp.convertTo(PixelFormatIDs::RGBA16_Premultiplied);

    // 新方式: A_tmp = 0 + 1 = 1
    // R16 = 255 * 1 = 255, G16 = 128 * 1 = 128, B16 = 64 * 1 = 64
    // A16 = 255 * 1 = 255 (ALPHA_TRANSPARENT_MAX)
    const uint16_t* convPixel = converted.getPixelPtr<uint16_t>(5, 5);
    EXPECT_EQ(convPixel[0], 255);   // R = 255 * 1
    EXPECT_EQ(convPixel[1], 128);   // G = 128 * 1
    EXPECT_EQ(convPixel[2], 64);    // B = 64 * 1
    EXPECT_EQ(convPixel[3], 255);   // A = 255 * 1 (ALPHA_TRANSPARENT_MAX)

    // 往復変換でRGB情報が復元されることを確認
    ViewPort roundtrip = converted.convertTo(PixelFormatIDs::RGBA8_Straight);
    const uint8_t* rtPixel = roundtrip.getPixelPtr<uint8_t>(5, 5);
    EXPECT_EQ(rtPixel[0], 255);  // R preserved
    EXPECT_EQ(rtPixel[1], 128);  // G preserved
    EXPECT_EQ(rtPixel[2], 64);   // B preserved
    EXPECT_EQ(rtPixel[3], 0);    // A = 255 >> 8 = 0
}

TEST(ViewPortTest, AlphaConversion_Roundtrip) {
    // 往復変換（8bit→16bit→8bit）で値が保持されることを確認
    ViewPort original(10, 10, PixelFormatIDs::RGBA8_Straight);

    // 半透明ピクセル
    uint8_t* pixel = original.getPixelPtr<uint8_t>(3, 3);
    pixel[0] = 200;  // R
    pixel[1] = 100;  // G
    pixel[2] = 50;   // B
    pixel[3] = 128;  // A (semi-transparent)

    ViewPort converted = original.convertTo(PixelFormatIDs::RGBA16_Premultiplied);
    ViewPort roundtrip = converted.convertTo(PixelFormatIDs::RGBA8_Straight);

    const uint8_t* rtPixel = roundtrip.getPixelPtr<uint8_t>(3, 3);
    EXPECT_EQ(rtPixel[0], 200);  // R preserved
    EXPECT_EQ(rtPixel[1], 100);  // G preserved
    EXPECT_EQ(rtPixel[2], 50);   // B preserved
    EXPECT_EQ(rtPixel[3], 128);  // A preserved
}

TEST(ViewPortTest, AlphaConversion_ThresholdConstants) {
    // 閾値定数の値を確認
    using namespace PixelFormatIDs::RGBA16Premul;

    EXPECT_EQ(ALPHA_TRANSPARENT_MAX, 255);
    EXPECT_EQ(ALPHA_OPAQUE_MIN, 65280);

    // ヘルパー関数のテスト
    EXPECT_TRUE(isTransparent(0));
    EXPECT_TRUE(isTransparent(255));
    EXPECT_FALSE(isTransparent(256));
    EXPECT_FALSE(isTransparent(65280));

    EXPECT_FALSE(isOpaque(255));
    EXPECT_FALSE(isOpaque(65279));
    EXPECT_TRUE(isOpaque(65280));
    EXPECT_TRUE(isOpaque(65535));
}

// ==============================================================================
// Image Interop Tests
// ==============================================================================

TEST(ViewPortTest, FromImage) {
    Image img(32, 32);

    // Set a pixel in the Image
    img.data[0] = 10;
    img.data[1] = 20;
    img.data[2] = 30;
    img.data[3] = 40;

    ViewPort vp = ViewPort::fromImage(img);

    EXPECT_TRUE(vp.isValid());
    EXPECT_EQ(vp.width, 32);
    EXPECT_EQ(vp.height, 32);
    EXPECT_EQ(vp.formatID, PixelFormatIDs::RGBA8_Straight);

    const uint8_t* pixel = vp.getPixelPtr<uint8_t>(0, 0);
    EXPECT_EQ(pixel[0], 10);
    EXPECT_EQ(pixel[1], 20);
    EXPECT_EQ(pixel[2], 30);
    EXPECT_EQ(pixel[3], 40);
}

TEST(ViewPortTest, ToImage) {
    ViewPort vp(24, 24, PixelFormatIDs::RGBA8_Straight);

    uint8_t* pixel = vp.getPixelPtr<uint8_t>(5, 5);
    pixel[0] = 50;
    pixel[1] = 100;
    pixel[2] = 150;
    pixel[3] = 200;

    Image img = vp.toImage();

    EXPECT_EQ(img.width, 24);
    EXPECT_EQ(img.height, 24);

    // Access pixel (5, 5) in Image
    size_t offset = (5 * 24 + 5) * 4;
    EXPECT_EQ(img.data[offset + 0], 50);
    EXPECT_EQ(img.data[offset + 1], 100);
    EXPECT_EQ(img.data[offset + 2], 150);
    EXPECT_EQ(img.data[offset + 3], 200);
}

// ==============================================================================
// Origin Tests
// ==============================================================================

TEST(ViewPortTest, OriginDefault) {
    ViewPort vp(100, 100, PixelFormatIDs::RGBA8_Straight);

    // Default origin should be (0, 0)
    EXPECT_FLOAT_EQ(vp.srcOriginX, 0.0f);
    EXPECT_FLOAT_EQ(vp.srcOriginY, 0.0f);
}

TEST(ViewPortTest, OriginPreservation_Copy) {
    ViewPort original(100, 100, PixelFormatIDs::RGBA8_Straight);
    original.srcOriginX = 50.5f;
    original.srcOriginY = 25.25f;

    ViewPort copy(original);

    EXPECT_FLOAT_EQ(copy.srcOriginX, 50.5f);
    EXPECT_FLOAT_EQ(copy.srcOriginY, 25.25f);
}

TEST(ViewPortTest, OriginPreservation_Move) {
    ViewPort original(100, 100, PixelFormatIDs::RGBA8_Straight);
    original.srcOriginX = 33.3f;
    original.srcOriginY = 66.6f;

    ViewPort moved(std::move(original));

    EXPECT_FLOAT_EQ(moved.srcOriginX, 33.3f);
    EXPECT_FLOAT_EQ(moved.srcOriginY, 66.6f);
}

// Note: main() is provided by gtest_main library
