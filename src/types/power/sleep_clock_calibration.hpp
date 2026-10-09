/**
 * @file sleep_clock_calibration.hpp
 * @brief Correction of the clock that times a deep sleep
 */

#pragma once

#include <algorithm>
#include <cstdint>

namespace loramesher {
namespace power {

/**
 * @brief Learned error of the deep-sleep clock
 *
 * The clock that runs through a deep sleep (the ESP32 RTC on its RC
 * oscillator) can be off by about 1 %. After each resume, the drift between
 * the node's schedule and the next sync beacon shows how far the clock was
 * off over the sleep; the calibration folds that into an error estimate and
 * corrects later sleeps with it: the time slept is converted to real time on
 * resume, and a planned sleep to sleep-clock time before sleeping.
 */
class SleepClockCalibration {
   public:
    /// Largest error accepted (5 %)
    static constexpr int32_t kMaxPpm = 50000;
    /// Shortest sleep whose drift is used as a sample
    static constexpr uint32_t kMinSampleSleepMs = 1000;
    /// Samples after which the estimate counts as calibrated
    static constexpr uint8_t kCalibratedSamples = 2;

    SleepClockCalibration() = default;

    /**
     * @param ppm Clock error in parts per million (positive: runs fast)
     * @param samples Samples the estimate is based on
     */
    SleepClockCalibration(int32_t ppm, uint8_t samples)
        : ppm_(std::clamp(ppm, -kMaxPpm, kMaxPpm)), samples_(samples) {}

    /// Estimated error in parts per million (positive: the clock runs fast)
    int32_t GetPpm() const { return ppm_; }

    /// Samples the estimate is based on (saturates at 255)
    uint8_t GetSamples() const { return samples_; }

    /// True once enough samples back the estimate
    bool IsCalibrated() const { return samples_ >= kCalibratedSamples; }

    /**
     * @brief Real time that passed while the sleep clock counted @p clock_ms
     */
    uint64_t ToRealMs(uint64_t clock_ms) const {
        return RoundedDivide(clock_ms * kMillion, ClockRate());
    }

    /**
     * @brief Sleep-clock time to request so that @p real_ms pass
     */
    uint64_t ToClockMs(uint64_t real_ms) const {
        return RoundedDivide(real_ms * ClockRate(), kMillion);
    }

    /**
     * @brief Add the drift seen after a corrected sleep
     *
     * @param drift_ms How far the node's schedule was ahead of the network's
     *        at the first beacon after the resume (negative: behind)
     * @param slept_ms Corrected length of the sleep; shorter sleeps than
     *        kMinSampleSleepMs are ignored
     */
    void AddSample(int32_t drift_ms, uint32_t slept_ms) {
        if (slept_ms < kMinSampleSleepMs) {
            return;
        }
        const int64_t residual_ppm = static_cast<int64_t>(drift_ms) *
                                     static_cast<int64_t>(kMillion) / slept_ms;
        // The first sample is the whole error; later ones correct half of
        // what is left, which smooths the beacon timing noise
        const int64_t ppm =
            samples_ == 0 ? residual_ppm : ppm_ + residual_ppm / 2;
        ppm_ =
            static_cast<int32_t>(std::clamp<int64_t>(ppm, -kMaxPpm, kMaxPpm));
        if (samples_ < UINT8_MAX) {
            ++samples_;
        }
    }

    bool operator==(const SleepClockCalibration& other) const {
        return ppm_ == other.ppm_ && samples_ == other.samples_;
    }

   private:
    static constexpr uint64_t kMillion = 1000000;

    /// Sleep-clock ticks per million real ones
    uint64_t ClockRate() const {
        return static_cast<uint64_t>(static_cast<int64_t>(kMillion) + ppm_);
    }

    static uint64_t RoundedDivide(uint64_t numerator, uint64_t denominator) {
        return (numerator + denominator / 2) / denominator;
    }

    int32_t ppm_ = 0;
    uint8_t samples_ = 0;
};

}  // namespace power
}  // namespace loramesher
