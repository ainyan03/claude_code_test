// fleximg types.h Unit Tests
// 固定小数点型とPoint構造体のテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/types.h"

using namespace fleximg;

// =============================================================================
// int_fixed8 Tests
// =============================================================================

TEST_CASE("int_fixed8 conversion") {
    SUBCASE("to_fixed8 from integer") {
        CHECK(to_fixed8(0) == 0);
        CHECK(to_fixed8(1) == 256);
        CHECK(to_fixed8(-1) == -256);
        CHECK(to_fixed8(100) == 25600);
    }

    SUBCASE("from_fixed8 to integer") {
        CHECK(from_fixed8(0) == 0);
        CHECK(from_fixed8(256) == 1);
        CHECK(from_fixed8(-256) == -1);
        CHECK(from_fixed8(25600) == 100);
    }

    SUBCASE("round trip") {
        for (int i = -100; i <= 100; ++i) {
            CHECK(from_fixed8(to_fixed8(i)) == i);
        }
    }

    SUBCASE("fractional values") {
        // 0.5 = 128 in fixed8
        int_fixed8 half = 128;
        CHECK(from_fixed8(half) == 0);  // truncates to 0
        CHECK(from_fixed8(half + 128) == 1);  // 0.5 + 0.5 = 1
    }
}

// =============================================================================
// int_fixed16 Tests
// =============================================================================

TEST_CASE("int_fixed16 conversion") {
    SUBCASE("to_fixed16 from integer") {
        CHECK(to_fixed16(0) == 0);
        CHECK(to_fixed16(1) == 65536);
        CHECK(to_fixed16(-1) == -65536);
    }

    SUBCASE("from_fixed16 to integer") {
        CHECK(from_fixed16(0) == 0);
        CHECK(from_fixed16(65536) == 1);
        CHECK(from_fixed16(-65536) == -1);
    }
}

// =============================================================================
// Point Tests
// =============================================================================

TEST_CASE("Point structure") {
    SUBCASE("default construction") {
        Point p;
        CHECK(p.x == 0);
        CHECK(p.y == 0);
    }

    SUBCASE("parameterized construction") {
        Point p(to_fixed8(10), to_fixed8(20));
        CHECK(from_fixed8(p.x) == 10);
        CHECK(from_fixed8(p.y) == 20);
    }

    SUBCASE("addition operator") {
        Point a(to_fixed8(10), to_fixed8(20));
        Point b(to_fixed8(5), to_fixed8(15));
        Point c = a + b;
        CHECK(from_fixed8(c.x) == 15);
        CHECK(from_fixed8(c.y) == 35);
    }

    SUBCASE("subtraction operator") {
        Point a(to_fixed8(10), to_fixed8(20));
        Point b(to_fixed8(5), to_fixed8(15));
        Point c = a - b;
        CHECK(from_fixed8(c.x) == 5);
        CHECK(from_fixed8(c.y) == 5);
    }
}
