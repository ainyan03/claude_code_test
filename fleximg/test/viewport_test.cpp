// fleximg ViewPort / ImageBuffer Unit Tests
// コンパイル: g++ -std=c++17 -I../src viewport_test.cpp ../src/fleximg/*.cpp -o viewport_test

#include <iostream>
#include <cmath>
#include "fleximg/common.h"
#include "fleximg/viewport.h"
#include "fleximg/image_buffer.h"
#include "fleximg/pixel_format.h"

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

// ==============================================================================
// ImageBuffer Construction Tests
// ==============================================================================

void testDefaultConstruction() {
    ImageBuffer buf;
    check("DefaultConstruction - data is null", buf.data() == nullptr);
    check("DefaultConstruction - width is 0", buf.width() == 0);
    check("DefaultConstruction - height is 0", buf.height() == 0);
    check("DefaultConstruction - not valid", !buf.isValid());
}

void testSizedConstruction_RGBA8() {
    ImageBuffer buf(100, 50, PixelFormatIDs::RGBA8_Straight);
    check("SizedConstruction_RGBA8 - data not null", buf.data() != nullptr);
    check("SizedConstruction_RGBA8 - width is 100", buf.width() == 100);
    check("SizedConstruction_RGBA8 - height is 50", buf.height() == 50);
    check("SizedConstruction_RGBA8 - format correct", buf.formatID() == PixelFormatIDs::RGBA8_Straight);
    check("SizedConstruction_RGBA8 - is valid", buf.isValid());
}

void testSizedConstruction_RGBA16() {
    ImageBuffer buf(64, 64, PixelFormatIDs::RGBA16_Premultiplied);
    check("SizedConstruction_RGBA16 - data not null", buf.data() != nullptr);
    check("SizedConstruction_RGBA16 - width is 64", buf.width() == 64);
    check("SizedConstruction_RGBA16 - height is 64", buf.height() == 64);
    check("SizedConstruction_RGBA16 - format correct", buf.formatID() == PixelFormatIDs::RGBA16_Premultiplied);
    check("SizedConstruction_RGBA16 - is valid", buf.isValid());
    check("SizedConstruction_RGBA16 - 8 bytes per pixel", buf.bytesPerPixel() == 8);
}

// ==============================================================================
// Memory Layout Tests
// ==============================================================================

void testBytesPerPixel() {
    ImageBuffer buf8(10, 10, PixelFormatIDs::RGBA8_Straight);
    check("BytesPerPixel_RGBA8 - 4 bytes", buf8.bytesPerPixel() == 4);

    ImageBuffer buf16(10, 10, PixelFormatIDs::RGBA16_Premultiplied);
    check("BytesPerPixel_RGBA16 - 8 bytes", buf16.bytesPerPixel() == 8);
}

void testStrideCalculation() {
    ImageBuffer buf(100, 50, PixelFormatIDs::RGBA8_Straight);
    check("StrideCalculation - stride >= width*4", buf.stride() >= static_cast<size_t>(100 * 4));
}

void testTotalBytes() {
    ImageBuffer buf(100, 50, PixelFormatIDs::RGBA8_Straight);
    check("TotalBytes - correct", buf.totalBytes() == buf.stride() * 50);
}

// ==============================================================================
// Pixel Access Tests
// ==============================================================================

void testPixelAccess_RGBA8() {
    ImageBuffer buf(10, 10, PixelFormatIDs::RGBA8_Straight);

    uint8_t* pixel = static_cast<uint8_t*>(buf.pixelAt(5, 3));
    pixel[0] = 255;  // R
    pixel[1] = 128;  // G
    pixel[2] = 64;   // B
    pixel[3] = 200;  // A

    const uint8_t* readPixel = static_cast<const uint8_t*>(buf.pixelAt(5, 3));
    check("PixelAccess_RGBA8 - R", readPixel[0] == 255);
    check("PixelAccess_RGBA8 - G", readPixel[1] == 128);
    check("PixelAccess_RGBA8 - B", readPixel[2] == 64);
    check("PixelAccess_RGBA8 - A", readPixel[3] == 200);
}

void testPixelAccess_RGBA16() {
    ImageBuffer buf(10, 10, PixelFormatIDs::RGBA16_Premultiplied);

    uint16_t* pixel = static_cast<uint16_t*>(buf.pixelAt(2, 7));
    pixel[0] = 65535;  // R
    pixel[1] = 32768;  // G
    pixel[2] = 16384;  // B
    pixel[3] = 49152;  // A

    const uint16_t* readPixel = static_cast<const uint16_t*>(buf.pixelAt(2, 7));
    check("PixelAccess_RGBA16 - R", readPixel[0] == 65535);
    check("PixelAccess_RGBA16 - G", readPixel[1] == 32768);
    check("PixelAccess_RGBA16 - B", readPixel[2] == 16384);
    check("PixelAccess_RGBA16 - A", readPixel[3] == 49152);
}

// ==============================================================================
// Move Semantics Tests
// ==============================================================================

void testMoveConstruction() {
    ImageBuffer original(30, 30, PixelFormatIDs::RGBA8_Straight);
    void* originalData = original.data();

    ImageBuffer moved(std::move(original));

    check("MoveConstruction - moved has data", moved.data() == originalData);
    check("MoveConstruction - moved width", moved.width() == 30);
    check("MoveConstruction - moved height", moved.height() == 30);
    check("MoveConstruction - moved valid", moved.isValid());
    check("MoveConstruction - original empty", original.data() == nullptr);
    check("MoveConstruction - original invalid", !original.isValid());
}

void testMoveAssignment() {
    ImageBuffer original(30, 30, PixelFormatIDs::RGBA8_Straight);
    void* originalData = original.data();

    ImageBuffer moved;
    moved = std::move(original);

    check("MoveAssignment - moved has data", moved.data() == originalData);
    check("MoveAssignment - moved width", moved.width() == 30);
    check("MoveAssignment - moved height", moved.height() == 30);
    check("MoveAssignment - moved valid", moved.isValid());
    check("MoveAssignment - original empty", original.data() == nullptr);
    check("MoveAssignment - original invalid", !original.isValid());
}

// ==============================================================================
// ViewPort Tests
// ==============================================================================

void testViewCreation() {
    ImageBuffer buf(100, 100, PixelFormatIDs::RGBA8_Straight);
    ViewPort view = buf.view();

    check("ViewCreation - valid", view.isValid());
    check("ViewCreation - width", view.width == 100);
    check("ViewCreation - height", view.height == 100);
    check("ViewCreation - format", view.formatID == PixelFormatIDs::RGBA8_Straight);
    check("ViewCreation - data matches", view.data == buf.data());
    check("ViewCreation - stride matches", view.stride == buf.stride());
}

void testSubViewCreation() {
    ImageBuffer buf(100, 100, PixelFormatIDs::RGBA8_Straight);
    ViewPort root = buf.view();

    ViewPort sub = view_ops::subView(root, 10, 20, 30, 40);

    check("SubView - valid", sub.isValid());
    check("SubView - width", sub.width == 30);
    check("SubView - height", sub.height == 40);
    check("SubView - format", sub.formatID == PixelFormatIDs::RGBA8_Straight);
    check("SubView - different data ptr", sub.data != root.data);
}

void testSubViewPixelAccess() {
    ImageBuffer buf(100, 100, PixelFormatIDs::RGBA8_Straight);
    ViewPort root = buf.view();

    // Write to root at (15, 25)
    uint8_t* rootPixel = static_cast<uint8_t*>(root.pixelAt(15, 25));
    rootPixel[0] = 111;
    rootPixel[1] = 222;

    // Create subview starting at (10, 20)
    ViewPort sub = view_ops::subView(root, 10, 20, 30, 40);

    // SubView (5, 5) should map to root (15, 25)
    const uint8_t* subPixel = static_cast<const uint8_t*>(sub.pixelAt(5, 5));
    check("SubViewPixelAccess - read through subview R", subPixel[0] == 111);
    check("SubViewPixelAccess - read through subview G", subPixel[1] == 222);

    // Write through subview
    uint8_t* subWritePixel = static_cast<uint8_t*>(sub.pixelAt(5, 5));
    subWritePixel[2] = 99;

    check("SubViewPixelAccess - write through subview", rootPixel[2] == 99);
}

// ==============================================================================
// view_ops Tests
// ==============================================================================

void testViewOpsCopy() {
    ImageBuffer src(20, 20, PixelFormatIDs::RGBA8_Straight);
    ImageBuffer dst(20, 20, PixelFormatIDs::RGBA8_Straight);

    // Set test data in src
    uint8_t* pixel = static_cast<uint8_t*>(src.pixelAt(5, 5));
    pixel[0] = 100;
    pixel[1] = 150;
    pixel[2] = 200;
    pixel[3] = 255;

    ViewPort srcView = src.view();
    ViewPort dstView = dst.view();

    view_ops::copy(dstView, 0, 0, srcView, 0, 0, 20, 20);

    const uint8_t* dstPixel = static_cast<const uint8_t*>(dst.pixelAt(5, 5));
    check("ViewOpsCopy - R copied", dstPixel[0] == 100);
    check("ViewOpsCopy - G copied", dstPixel[1] == 150);
    check("ViewOpsCopy - B copied", dstPixel[2] == 200);
    check("ViewOpsCopy - A copied", dstPixel[3] == 255);
}

void testViewOpsClear() {
    ImageBuffer buf(20, 20, PixelFormatIDs::RGBA8_Straight);

    // Set non-zero data
    uint8_t* pixel = static_cast<uint8_t*>(buf.pixelAt(5, 5));
    pixel[0] = 100;
    pixel[1] = 150;
    pixel[2] = 200;
    pixel[3] = 255;

    ViewPort view = buf.view();
    view_ops::clear(view, 0, 0, 20, 20);

    const uint8_t* cleared = static_cast<const uint8_t*>(buf.pixelAt(5, 5));
    check("ViewOpsClear - R cleared", cleared[0] == 0);
    check("ViewOpsClear - G cleared", cleared[1] == 0);
    check("ViewOpsClear - B cleared", cleared[2] == 0);
    check("ViewOpsClear - A cleared", cleared[3] == 0);
}

// ==============================================================================
// Main
// ==============================================================================

int main() {
    std::cout << "=== fleximg ViewPort/ImageBuffer Tests ===" << std::endl;
    std::cout << std::endl;

    // Construction
    testDefaultConstruction();
    testSizedConstruction_RGBA8();
    testSizedConstruction_RGBA16();

    // Memory Layout
    testBytesPerPixel();
    testStrideCalculation();
    testTotalBytes();

    // Pixel Access
    testPixelAccess_RGBA8();
    testPixelAccess_RGBA16();

    // Move Semantics
    testMoveConstruction();
    testMoveAssignment();

    // ViewPort
    testViewCreation();
    testSubViewCreation();
    testSubViewPixelAccess();

    // view_ops
    testViewOpsCopy();
    testViewOpsClear();

    std::cout << std::endl;
    std::cout << "=== Results ===" << std::endl;
    std::cout << "Passed: " << testsPassed << std::endl;
    std::cout << "Failed: " << testsFailed << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
