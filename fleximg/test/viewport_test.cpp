// ImageBuffer / ViewPort Unit Tests
// Tests for the image buffer and viewport structures

#include <gtest/gtest.h>
#include "fleximg/image_buffer.h"
#include "fleximg/viewport.h"
#include "fleximg/pixel_format.h"
#include "fleximg/image_types.h"

using namespace FLEXIMG_NAMESPACE;

// ==============================================================================
// ImageBuffer Construction Tests
// ==============================================================================

TEST(ImageBufferTest, DefaultConstruction) {
    ImageBuffer buf;
    EXPECT_EQ(buf.data, nullptr);
    EXPECT_EQ(buf.width, 0);
    EXPECT_EQ(buf.height, 0);
    EXPECT_FALSE(buf.isValid());
}

TEST(ImageBufferTest, SizedConstruction_RGBA8) {
    ImageBuffer buf(100, 50, PixelFormatIDs::RGBA8_Straight);

    EXPECT_NE(buf.data, nullptr);
    EXPECT_EQ(buf.width, 100);
    EXPECT_EQ(buf.height, 50);
    EXPECT_EQ(buf.formatID, PixelFormatIDs::RGBA8_Straight);
    EXPECT_TRUE(buf.isValid());
}

TEST(ImageBufferTest, SizedConstruction_RGBA16) {
    ImageBuffer buf(64, 64, PixelFormatIDs::RGBA16_Premultiplied);

    EXPECT_NE(buf.data, nullptr);
    EXPECT_EQ(buf.width, 64);
    EXPECT_EQ(buf.height, 64);
    EXPECT_EQ(buf.formatID, PixelFormatIDs::RGBA16_Premultiplied);
    EXPECT_TRUE(buf.isValid());

    // RGBA16 = 8 bytes per pixel
    EXPECT_EQ(buf.getBytesPerPixel(), 8);
}

// ==============================================================================
// Memory Layout Tests
// ==============================================================================

TEST(ImageBufferTest, BytesPerPixel_RGBA8) {
    ImageBuffer buf(10, 10, PixelFormatIDs::RGBA8_Straight);
    EXPECT_EQ(buf.getBytesPerPixel(), 4);
}

TEST(ImageBufferTest, BytesPerPixel_RGBA16) {
    ImageBuffer buf(10, 10, PixelFormatIDs::RGBA16_Premultiplied);
    EXPECT_EQ(buf.getBytesPerPixel(), 8);
}

TEST(ImageBufferTest, StrideCalculation) {
    ImageBuffer buf(100, 50, PixelFormatIDs::RGBA8_Straight);
    // stride should be at least width * bytesPerPixel
    EXPECT_GE(buf.stride, static_cast<size_t>(100 * 4));
}

TEST(ImageBufferTest, TotalBytes) {
    ImageBuffer buf(100, 50, PixelFormatIDs::RGBA8_Straight);
    // Total bytes = stride * height
    EXPECT_EQ(buf.getTotalBytes(), buf.stride * 50);
}

// ==============================================================================
// Pixel Access Tests
// ==============================================================================

TEST(ImageBufferTest, PixelAccess_RGBA8) {
    ImageBuffer buf(10, 10, PixelFormatIDs::RGBA8_Straight);

    // Write pixel at (5, 3)
    uint8_t* pixel = static_cast<uint8_t*>(buf.getPixelAddress(5, 3));
    pixel[0] = 255;  // R
    pixel[1] = 128;  // G
    pixel[2] = 64;   // B
    pixel[3] = 200;  // A

    // Read back
    const uint8_t* readPixel = static_cast<const uint8_t*>(buf.getPixelAddress(5, 3));
    EXPECT_EQ(readPixel[0], 255);
    EXPECT_EQ(readPixel[1], 128);
    EXPECT_EQ(readPixel[2], 64);
    EXPECT_EQ(readPixel[3], 200);
}

TEST(ImageBufferTest, PixelAccess_RGBA16) {
    ImageBuffer buf(10, 10, PixelFormatIDs::RGBA16_Premultiplied);

    // Write pixel at (2, 7)
    uint16_t* pixel = static_cast<uint16_t*>(buf.getPixelAddress(2, 7));
    pixel[0] = 65535;  // R
    pixel[1] = 32768;  // G
    pixel[2] = 16384;  // B
    pixel[3] = 49152;  // A

    // Read back
    const uint16_t* readPixel = static_cast<const uint16_t*>(buf.getPixelAddress(2, 7));
    EXPECT_EQ(readPixel[0], 65535);
    EXPECT_EQ(readPixel[1], 32768);
    EXPECT_EQ(readPixel[2], 16384);
    EXPECT_EQ(readPixel[3], 49152);
}

// ==============================================================================
// Move Semantics Tests
// ==============================================================================

TEST(ImageBufferTest, MoveConstruction) {
    ImageBuffer original(30, 30, PixelFormatIDs::RGBA8_Straight);
    void* originalData = original.data;

    ImageBuffer moved(std::move(original));

    // Moved-to object has the data
    EXPECT_EQ(moved.data, originalData);
    EXPECT_EQ(moved.width, 30);
    EXPECT_EQ(moved.height, 30);
    EXPECT_TRUE(moved.isValid());

    // Moved-from object is empty
    EXPECT_EQ(original.data, nullptr);
    EXPECT_FALSE(original.isValid());
}

TEST(ImageBufferTest, MoveAssignment) {
    ImageBuffer original(30, 30, PixelFormatIDs::RGBA8_Straight);
    void* originalData = original.data;

    ImageBuffer moved;
    moved = std::move(original);

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
// ViewPort Tests
// ==============================================================================

TEST(ViewPortTest, ViewCreation) {
    ImageBuffer buf(100, 100, PixelFormatIDs::RGBA8_Straight);

    ViewPort view = buf.view();

    EXPECT_TRUE(view.isValid());
    EXPECT_EQ(view.width, 100);
    EXPECT_EQ(view.height, 100);
    EXPECT_EQ(view.formatID, PixelFormatIDs::RGBA8_Straight);
    EXPECT_EQ(view.data, buf.data);
    EXPECT_EQ(view.stride, buf.stride);
}

TEST(ViewPortTest, SubView_Creation) {
    ImageBuffer buf(100, 100, PixelFormatIDs::RGBA8_Straight);
    ViewPort root = buf.view();

    ViewPort sub = root.subView(10, 20, 30, 40);

    EXPECT_TRUE(sub.isValid());
    EXPECT_EQ(sub.width, 30);
    EXPECT_EQ(sub.height, 40);
    EXPECT_EQ(sub.formatID, PixelFormatIDs::RGBA8_Straight);
    // sub.data should point into the root buffer
    EXPECT_NE(sub.data, root.data);
}

TEST(ViewPortTest, SubView_PixelAccess) {
    ImageBuffer buf(100, 100, PixelFormatIDs::RGBA8_Straight);
    ViewPort root = buf.view();

    // Write to root at (15, 25)
    uint8_t* rootPixel = root.getPixelPtr<uint8_t>(15, 25);
    rootPixel[0] = 111;
    rootPixel[1] = 222;

    // Create subview starting at (10, 20)
    ViewPort sub = root.subView(10, 20, 30, 40);

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

TEST(ViewPortTest, ToImageBuffer) {
    ImageBuffer buf(20, 20, PixelFormatIDs::RGBA8_Straight);

    // Set test data
    uint8_t* pixel = static_cast<uint8_t*>(buf.getPixelAddress(5, 5));
    pixel[0] = 100;
    pixel[1] = 150;
    pixel[2] = 200;
    pixel[3] = 255;

    ViewPort view = buf.view();
    ImageBuffer copy = view.toImageBuffer();

    EXPECT_EQ(copy.formatID, PixelFormatIDs::RGBA8_Straight);
    EXPECT_NE(copy.data, buf.data);  // Should be a copy

    const uint8_t* copyPixel = static_cast<const uint8_t*>(copy.getPixelAddress(5, 5));
    EXPECT_EQ(copyPixel[0], 100);
    EXPECT_EQ(copyPixel[1], 150);
    EXPECT_EQ(copyPixel[2], 200);
    EXPECT_EQ(copyPixel[3], 255);
}

// ==============================================================================
// Format Conversion Tests
// ==============================================================================

TEST(ImageBufferTest, ConvertTo_SameFormat) {
    ImageBuffer buf(20, 20, PixelFormatIDs::RGBA8_Straight);

    // Set test data
    uint8_t* pixel = static_cast<uint8_t*>(buf.getPixelAddress(5, 5));
    pixel[0] = 100;
    pixel[1] = 150;
    pixel[2] = 200;
    pixel[3] = 255;

    // Convert to same format (should copy)
    ImageBuffer converted = buf.convertTo(PixelFormatIDs::RGBA8_Straight);

    EXPECT_EQ(converted.formatID, PixelFormatIDs::RGBA8_Straight);
    EXPECT_NE(converted.data, buf.data);  // Should be a copy

    const uint8_t* convPixel = static_cast<const uint8_t*>(converted.getPixelAddress(5, 5));
    EXPECT_EQ(convPixel[0], 100);
    EXPECT_EQ(convPixel[1], 150);
    EXPECT_EQ(convPixel[2], 200);
    EXPECT_EQ(convPixel[3], 255);
}

TEST(ImageBufferTest, ConvertTo_RGBA8_to_RGBA16Premul) {
    ImageBuffer buf(10, 10, PixelFormatIDs::RGBA8_Straight);

    // Set opaque red pixel
    uint8_t* pixel = static_cast<uint8_t*>(buf.getPixelAddress(3, 3));
    pixel[0] = 255;  // R
    pixel[1] = 0;    // G
    pixel[2] = 0;    // B
    pixel[3] = 255;  // A (opaque)

    ImageBuffer converted = buf.convertTo(PixelFormatIDs::RGBA16_Premultiplied);

    EXPECT_EQ(converted.formatID, PixelFormatIDs::RGBA16_Premultiplied);
    EXPECT_EQ(converted.width, 10);
    EXPECT_EQ(converted.height, 10);

    // A_tmp = A8 + 1 = 256
    // R16 = R8 * A_tmp = 255 * 256 = 65280
    // A16 = 255 * A_tmp = 255 * 256 = 65280
    const uint16_t* convPixel = static_cast<const uint16_t*>(converted.getPixelAddress(3, 3));
    EXPECT_EQ(convPixel[0], 65280);  // R = 255 * 256
    EXPECT_EQ(convPixel[1], 0);      // G = 0 * 256
    EXPECT_EQ(convPixel[2], 0);      // B = 0 * 256
    EXPECT_EQ(convPixel[3], 65280);  // A = 255 * 256 (ALPHA_OPAQUE_MIN)
}

TEST(ImageBufferTest, ConvertTo_RGBA16Premul_to_RGBA8) {
    ImageBuffer buf(10, 10, PixelFormatIDs::RGBA16_Premultiplied);

    // Opaque green pixel (A8=255 -> A16=65280)
    uint16_t* pixel = static_cast<uint16_t*>(buf.getPixelAddress(4, 4));
    pixel[0] = 0;      // R = 0 * 256 = 0
    pixel[1] = 65280;  // G = 255 * 256 = 65280
    pixel[2] = 0;      // B = 0 * 256 = 0
    pixel[3] = 65280;  // A = 255 * 256 = 65280 (ALPHA_OPAQUE_MIN)

    ImageBuffer converted = buf.convertTo(PixelFormatIDs::RGBA8_Straight);

    EXPECT_EQ(converted.formatID, PixelFormatIDs::RGBA8_Straight);

    // Reverse conversion: A8 = 65280 >> 8 = 255, A_tmp = 256
    // G8 = 65280 / 256 = 255
    const uint8_t* convPixel = static_cast<const uint8_t*>(converted.getPixelAddress(4, 4));
    EXPECT_EQ(convPixel[0], 0);    // R = 0 / 256 = 0
    EXPECT_EQ(convPixel[1], 255);  // G = 65280 / 256 = 255
    EXPECT_EQ(convPixel[2], 0);    // B = 0 / 256 = 0
    EXPECT_EQ(convPixel[3], 255);  // A = 65280 >> 8 = 255
}

// ==============================================================================
// Alpha Conversion Tests
// ==============================================================================

TEST(ImageBufferTest, AlphaConversion_TransparentPreservesRGB) {
    // Transparent pixel (A8=0) should preserve RGB info
    ImageBuffer buf(10, 10, PixelFormatIDs::RGBA8_Straight);

    uint8_t* pixel = static_cast<uint8_t*>(buf.getPixelAddress(5, 5));
    pixel[0] = 255;  // R
    pixel[1] = 128;  // G
    pixel[2] = 64;   // B
    pixel[3] = 0;    // A (transparent)

    ImageBuffer converted = buf.convertTo(PixelFormatIDs::RGBA16_Premultiplied);

    // A_tmp = 0 + 1 = 1
    // R16 = 255 * 1 = 255, G16 = 128 * 1 = 128, B16 = 64 * 1 = 64
    // A16 = 255 * 1 = 255 (ALPHA_TRANSPARENT_MAX)
    const uint16_t* convPixel = static_cast<const uint16_t*>(converted.getPixelAddress(5, 5));
    EXPECT_EQ(convPixel[0], 255);   // R = 255 * 1
    EXPECT_EQ(convPixel[1], 128);   // G = 128 * 1
    EXPECT_EQ(convPixel[2], 64);    // B = 64 * 1
    EXPECT_EQ(convPixel[3], 255);   // A = 255 * 1 (ALPHA_TRANSPARENT_MAX)

    // Roundtrip should restore RGB info
    ImageBuffer roundtrip = converted.convertTo(PixelFormatIDs::RGBA8_Straight);
    const uint8_t* rtPixel = static_cast<const uint8_t*>(roundtrip.getPixelAddress(5, 5));
    EXPECT_EQ(rtPixel[0], 255);  // R preserved
    EXPECT_EQ(rtPixel[1], 128);  // G preserved
    EXPECT_EQ(rtPixel[2], 64);   // B preserved
    EXPECT_EQ(rtPixel[3], 0);    // A = 255 >> 8 = 0
}

TEST(ImageBufferTest, AlphaConversion_Roundtrip) {
    // Roundtrip conversion (8bit->16bit->8bit) should preserve values
    ImageBuffer original(10, 10, PixelFormatIDs::RGBA8_Straight);

    // Semi-transparent pixel
    uint8_t* pixel = static_cast<uint8_t*>(original.getPixelAddress(3, 3));
    pixel[0] = 200;  // R
    pixel[1] = 100;  // G
    pixel[2] = 50;   // B
    pixel[3] = 128;  // A (semi-transparent)

    ImageBuffer converted = original.convertTo(PixelFormatIDs::RGBA16_Premultiplied);
    ImageBuffer roundtrip = converted.convertTo(PixelFormatIDs::RGBA8_Straight);

    const uint8_t* rtPixel = static_cast<const uint8_t*>(roundtrip.getPixelAddress(3, 3));
    EXPECT_EQ(rtPixel[0], 200);  // R preserved
    EXPECT_EQ(rtPixel[1], 100);  // G preserved
    EXPECT_EQ(rtPixel[2], 50);   // B preserved
    EXPECT_EQ(rtPixel[3], 128);  // A preserved
}

TEST(ImageBufferTest, AlphaConversion_ThresholdConstants) {
    // Verify threshold constants
    using namespace PixelFormatIDs::RGBA16Premul;

    EXPECT_EQ(ALPHA_TRANSPARENT_MAX, 255);
    EXPECT_EQ(ALPHA_OPAQUE_MIN, 65280);

    // Helper function tests
    EXPECT_TRUE(isTransparent(0));
    EXPECT_TRUE(isTransparent(255));
    EXPECT_FALSE(isTransparent(256));
    EXPECT_FALSE(isTransparent(65280));

    EXPECT_FALSE(isOpaque(255));
    EXPECT_FALSE(isOpaque(65279));
    EXPECT_TRUE(isOpaque(65280));
    EXPECT_TRUE(isOpaque(65535));
}

// Note: main() is provided by gtest_main library
