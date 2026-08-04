// Smoke test — validates the GoogleTest harness itself.
//
// Pure stdlib + GoogleTest only (no project headers): a harness failure must
// be diagnosable without implicating engine code. The three cases prove
// (1) assertions execute, (2) std::string links against the test binary, and
// (3) the compiler supports C++20 designated initializers, which the ROM data
// tables rely on for every row literal.

#include <gtest/gtest.h>

#include <string>

TEST(SmokeTest, BasicArithmetic) {
    EXPECT_EQ(2 + 2, 4);
    EXPECT_NE(2 + 2, 5);
}

TEST(SmokeTest, StringCompare) {
    const std::string a = "kirpich";
    const std::string b = "kirpich";
    EXPECT_EQ(a, b);
    EXPECT_NE(a, std::string("snake"));
}

TEST(SmokeTest, Cpp20DesignatedInit) {
    struct Probe { int x; int y; const char* label; };
    constexpr Probe p { .x = 7, .y = 11, .label = "designated" };
    EXPECT_EQ(p.x, 7);
    EXPECT_EQ(p.y, 11);
    EXPECT_STREQ(p.label, "designated");
}
