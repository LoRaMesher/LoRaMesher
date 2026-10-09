/**
 * @file sleep_clock_calibration_test.cpp
 * @brief Tests of the deep-sleep clock calibration
 */

#include <gtest/gtest.h>

#include <cstdlib>

#include "types/power/sleep_clock_calibration.hpp"

namespace loramesher {
namespace power {
namespace {

/// Sleep-clock time a clock @p true_ppm fast counts while @p real_ms pass
uint64_t ClockCounts(uint64_t real_ms, int32_t true_ppm) {
    return static_cast<uint64_t>(static_cast<int64_t>(real_ms) +
                                 static_cast<int64_t>(real_ms) * true_ppm /
                                     1000000);
}

/**
 * @brief Run @p sleeps sleeps of @p real_ms with a clock @p true_ppm off
 *
 * @return The drift seen after the last sleep (ms, positive: node ahead)
 */
int64_t Sleep(SleepClockCalibration& calibration, int32_t true_ppm,
              uint32_t real_ms, int sleeps) {
    int64_t drift_ms = 0;
    for (int i = 0; i < sleeps; ++i) {
        // The node asks the sleep clock for what it believes is real_ms...
        const uint64_t requested = calibration.ToClockMs(real_ms);
        // ...the clock really lets this much time pass...
        const uint64_t real =
            requested * 1000000 / static_cast<uint64_t>(1000000 + true_ppm);
        // ...and on waking the node measures the clock and corrects it
        const uint64_t believed =
            calibration.ToRealMs(ClockCounts(real, true_ppm));
        drift_ms = static_cast<int64_t>(believed) - static_cast<int64_t>(real);
        calibration.AddSample(static_cast<int32_t>(drift_ms),
                              static_cast<uint32_t>(believed));
    }
    return drift_ms;
}

TEST(SleepClockCalibrationTest, UncalibratedClockIsTakenAsIs) {
    SleepClockCalibration calibration;
    EXPECT_EQ(calibration.GetPpm(), 0);
    EXPECT_EQ(calibration.GetSamples(), 0u);
    EXPECT_FALSE(calibration.IsCalibrated());
    EXPECT_EQ(calibration.ToRealMs(110000), 110000u);
    EXPECT_EQ(calibration.ToClockMs(110000), 110000u);
}

TEST(SleepClockCalibrationTest, FirstSampleSetsTheError) {
    SleepClockCalibration calibration;
    // 80 ms ahead after 10 s: the clock runs 8000 ppm fast
    calibration.AddSample(80, 10000);
    EXPECT_EQ(calibration.GetPpm(), 8000);
    EXPECT_EQ(calibration.GetSamples(), 1u);
    EXPECT_FALSE(calibration.IsCalibrated());
    EXPECT_EQ(calibration.ToClockMs(10000), 10080u);
    EXPECT_EQ(calibration.ToRealMs(10080), 10000u);
}

TEST(SleepClockCalibrationTest, LaterSamplesCorrectHalfTheResidual) {
    SleepClockCalibration calibration;
    calibration.AddSample(80, 10000);
    // Still 10 ms ahead after a corrected sleep: 1000 ppm left
    calibration.AddSample(10, 10000);
    EXPECT_EQ(calibration.GetPpm(), 8500);
    EXPECT_EQ(calibration.GetSamples(), 2u);
    EXPECT_TRUE(calibration.IsCalibrated());
}

TEST(SleepClockCalibrationTest, ConvergesOnAFastClock) {
    SleepClockCalibration calibration;
    const int64_t first = Sleep(calibration, 7000, 110000, 1);
    EXPECT_NEAR(first, 765, 2);
    const int64_t later = Sleep(calibration, 7000, 110000, 8);
    EXPECT_LE(std::llabs(later), 5);
    EXPECT_NEAR(calibration.GetPpm(), 7000, 50);
}

TEST(SleepClockCalibrationTest, ConvergesOnASlowClock) {
    SleepClockCalibration calibration;
    Sleep(calibration, -5000, 31000, 1);
    const int64_t later = Sleep(calibration, -5000, 31000, 8);
    EXPECT_LE(std::llabs(later), 3);
    EXPECT_NEAR(calibration.GetPpm(), -5000, 100);
}

TEST(SleepClockCalibrationTest, ImplausibleErrorIsClamped) {
    SleepClockCalibration calibration;
    calibration.AddSample(5000, 10000);
    EXPECT_EQ(calibration.GetPpm(), SleepClockCalibration::kMaxPpm);
    calibration.AddSample(-30000, 10000);
    calibration.AddSample(-30000, 10000);
    EXPECT_EQ(calibration.GetPpm(), -SleepClockCalibration::kMaxPpm);
}

TEST(SleepClockCalibrationTest, ShortSleepsAreIgnored) {
    SleepClockCalibration calibration;
    calibration.AddSample(10, SleepClockCalibration::kMinSampleSleepMs - 1);
    EXPECT_EQ(calibration.GetSamples(), 0u);
    EXPECT_EQ(calibration.GetPpm(), 0);
}

TEST(SleepClockCalibrationTest, SampleCountSaturates) {
    SleepClockCalibration calibration(1000, 255);
    calibration.AddSample(0, 10000);
    EXPECT_EQ(calibration.GetSamples(), 255u);
    EXPECT_EQ(calibration.GetPpm(), 1000);
}

}  // namespace
}  // namespace power
}  // namespace loramesher
