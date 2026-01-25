// fleximg types.h Unit Tests
// 固定小数点型とPoint構造体のテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/core/types.h"

using namespace fleximg;

// =============================================================================
// int_fixed Tests
// =============================================================================

TEST_CASE("int_fixed conversion") {
    SUBCASE("to_fixed from integer") {
        CHECK(to_fixed(0) == 0);
        CHECK(to_fixed(1) == 65536);
        CHECK(to_fixed(-1) == -65536);
    }

    SUBCASE("from_fixed to integer") {
        CHECK(from_fixed(0) == 0);
        CHECK(from_fixed(65536) == 1);
        CHECK(from_fixed(-65536) == -1);
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
        Point p(to_fixed(10), to_fixed(20));
        CHECK(from_fixed(p.x) == 10);
        CHECK(from_fixed(p.y) == 20);
    }

    SUBCASE("addition operator") {
        Point a(to_fixed(10), to_fixed(20));
        Point b(to_fixed(5), to_fixed(15));
        Point c = a + b;
        CHECK(from_fixed(c.x) == 15);
        CHECK(from_fixed(c.y) == 35);
    }

    SUBCASE("subtraction operator") {
        Point a(to_fixed(10), to_fixed(20));
        Point b(to_fixed(5), to_fixed(15));
        Point c = a - b;
        CHECK(from_fixed(c.x) == 5);
        CHECK(from_fixed(c.y) == 5);
    }
}
