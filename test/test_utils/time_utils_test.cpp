/**
 * @file time_utils_test.cpp
 * @brief Tests of the wrapping-clock helpers
 */

#include <gtest/gtest.h>

#include "utils/time_utils.hpp"

namespace loramesher {
namespace utils {
namespace {

TEST(TimeUtilsTest, TimeReachedAcrossTheWrap) {
    EXPECT_TRUE(TimeReached(5, 0xFFFFFFF0u));
    EXPECT_FALSE(TimeReached(0xFFFFFFF0u, 5));
    EXPECT_TRUE(TimeReached(100, 100));
}

TEST(TimeUtilsTest, WrapToPeriodKeepsSmallOffsets) {
    EXPECT_EQ(WrapToPeriod(25, 1000), 25);
    EXPECT_EQ(WrapToPeriod(-25, 1000), -25);
    EXPECT_EQ(WrapToPeriod(500, 1000), 500);
    EXPECT_EQ(WrapToPeriod(-500, 1000), 500);
}

TEST(TimeUtilsTest, WrapToPeriodFoldsWholePeriods) {
    // A beacon 31 ms before the node's own superframe start, measured against
    // the previous superframe start
    EXPECT_EQ(WrapToPeriod(119600 - 31, 119600), -31);
    EXPECT_EQ(WrapToPeriod(3 * 1000 + 40, 1000), 40);
    EXPECT_EQ(WrapToPeriod(-2 * 1000 - 40, 1000), -40);
    EXPECT_EQ(WrapToPeriod(77, 0), 77);
}

}  // namespace
}  // namespace utils
}  // namespace loramesher
