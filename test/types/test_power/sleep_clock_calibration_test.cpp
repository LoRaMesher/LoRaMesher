/**
 * @file sleep_clock_calibration_test.cpp
 * @brief Tests of the deep-sleep clock calibration
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <random>

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

TEST(SleepClockCalibrationTest, FirstSampleIsOnlyCounted) {
    // The sleep right after power-on runs on a poorly calibrated clock
    SleepClockCalibration calibration;
    calibration.AddSample(160, 10000);
    EXPECT_EQ(calibration.GetPpm(), 0);
    EXPECT_EQ(calibration.GetSamples(), 1u);
    EXPECT_FALSE(calibration.IsCalibrated());
}

TEST(SleepClockCalibrationTest, SecondSampleSetsTheError) {
    SleepClockCalibration calibration;
    calibration.AddSample(160, 10000);
    // 80 ms ahead after 10 s: the clock runs 8000 ppm fast
    calibration.AddSample(80, 10000);
    EXPECT_EQ(calibration.GetPpm(), 8000);
    EXPECT_EQ(calibration.GetSamples(), 2u);
    EXPECT_FALSE(calibration.IsCalibrated());
    EXPECT_EQ(calibration.ToClockMs(10000), 10080u);
    EXPECT_EQ(calibration.ToRealMs(10080), 10000u);
}

TEST(SleepClockCalibrationTest, LaterSamplesCorrectPartOfTheResidual) {
    SleepClockCalibration calibration;
    calibration.AddSample(0, 10000);
    calibration.AddSample(80, 10000);
    // Still 10 ms ahead after a corrected sleep: 1000 ppm left, half of it
    // corrected while the estimate is young
    calibration.AddSample(10, 10000);
    EXPECT_EQ(calibration.GetPpm(), 8500);
    EXPECT_TRUE(calibration.IsCalibrated());
    calibration.AddSample(10, 10000);
    EXPECT_EQ(calibration.GetPpm(), 9000);
    // Then a quarter, which smooths the beacon timing noise
    calibration.AddSample(-40, 10000);
    EXPECT_EQ(calibration.GetPpm(), 8000);
}

TEST(SleepClockCalibrationTest, ConvergesOnAFastClock) {
    SleepClockCalibration calibration;
    Sleep(calibration, 7000, 110000, 2);
    const int64_t later = Sleep(calibration, 7000, 110000, 12);
    EXPECT_LE(std::llabs(later), 5);
    EXPECT_NEAR(calibration.GetPpm(), 7000, 50);
}

TEST(SleepClockCalibrationTest, ConvergesOnASlowClock) {
    SleepClockCalibration calibration;
    Sleep(calibration, -5000, 31000, 2);
    const int64_t later = Sleep(calibration, -5000, 31000, 12);
    EXPECT_LE(std::llabs(later), 3);
    EXPECT_NEAR(calibration.GetPpm(), -5000, 100);
}

TEST(SleepClockCalibrationTest, BeaconNoiseIsSmoothed) {
    // Beacon timing adds about +-900 ppm of noise to every sample of a
    // 110 s sleep on a clock 5000 ppm fast
    SleepClockCalibration calibration;
    std::minstd_rand rng(7);
    std::uniform_int_distribution<int32_t> noise_ppm(-900, 900);
    int32_t worst = 0;
    for (int i = 0; i < 40; ++i) {
        const int32_t residual = 5000 - calibration.GetPpm() + noise_ppm(rng);
        calibration.AddSample(
            static_cast<int32_t>(static_cast<int64_t>(residual) * 110 / 1000),
            110000);
        if (i >= 10) {
            worst = std::max(worst, std::abs(calibration.GetPpm() - 5000));
        }
    }
    // Without smoothing the estimate would follow the noise (+-900 ppm); 500
    // ppm over a 110 s sleep is 55 ms, well inside the calibrated margin
    EXPECT_LE(worst, 500);
}

TEST(SleepClockCalibrationTest, ImplausibleErrorIsClamped) {
    SleepClockCalibration calibration;
    calibration.AddSample(0, 10000);
    calibration.AddSample(5000, 10000);
    EXPECT_EQ(calibration.GetPpm(), SleepClockCalibration::kMaxPpm);
    for (int i = 0; i < 6; ++i) {
        calibration.AddSample(-30000, 10000);
    }
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
