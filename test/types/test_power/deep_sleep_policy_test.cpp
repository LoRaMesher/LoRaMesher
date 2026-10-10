/**
 * @file deep_sleep_policy_test.cpp
 * @brief Tests of the wake margins derived from the sleep-clock error
 */

#include <gtest/gtest.h>

#include <limits>

#include "types/power/power_types.hpp"

namespace loramesher {
namespace power {
namespace {

TEST(DeepSleepPolicyTest, AllowanceScalesWithTheSleepAndRoundsUp) {
    const DeepSleepPolicy policy;
    EXPECT_EQ(policy.SleepClockAllowanceMs(4000, false), 40u);
    EXPECT_EQ(policy.SleepClockAllowanceMs(4000, true), 12u);
    EXPECT_EQ(policy.SleepClockAllowanceMs(4001, false), 41u);
    EXPECT_EQ(policy.SleepClockAllowanceMs(0, false), 0u);
}

TEST(DeepSleepPolicyTest, WakeMarginAddsBootGuardAndAllowance) {
    const DeepSleepPolicy policy;
    EXPECT_EQ(policy.WakeMarginMs(100000, 20, false),
              policy.boot_time_ms + 20 + 1000);
    EXPECT_EQ(policy.WakeMarginMs(100000, 20, true),
              policy.boot_time_ms + 20 + 300);
}

TEST(DeepSleepPolicyTest, MarginsDoNotOverflow) {
    DeepSleepPolicy policy;
    policy.clock_drift_ppm = 100000;
    policy.boot_time_ms = 60000;
    const uint32_t longest = std::numeric_limits<uint32_t>::max();
    EXPECT_EQ(policy.SleepClockAllowanceMs(longest, false), longest / 10 + 1);
    EXPECT_GT(policy.WakeMarginMs(longest, 20, false),
              policy.SleepClockAllowanceMs(longest, false));
}

}  // namespace
}  // namespace power
}  // namespace loramesher
