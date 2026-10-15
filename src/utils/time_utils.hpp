#pragma once

#include <cstdint>

namespace loramesher {
namespace utils {

/**
 * @brief Whether a millisecond deadline has been reached on a wrapping clock
 *
 * Compares through the signed difference, so it stays correct when the 32-bit
 * tick counter wraps, as long as @p now and @p deadline are less than 2^31 ms
 * apart.
 *
 * @param now Current time (ms)
 * @param deadline Deadline (ms)
 * @return true if @p now is at or past @p deadline
 */
constexpr bool TimeReached(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

/**
 * @brief Wrap an offset between two periodic schedules to the nearest period
 *
 * @param offset_ms Offset between the schedules (ms)
 * @param period_ms Period of the schedules (ms); 0 leaves the offset as is
 * @return The offset in (-period/2, period/2]
 */
constexpr int32_t WrapToPeriod(int32_t offset_ms, uint32_t period_ms) {
    if (period_ms == 0) {
        return offset_ms;
    }
    const int64_t period = period_ms;
    int64_t wrapped = static_cast<int64_t>(offset_ms) % period;
    if (wrapped > period / 2) {
        wrapped -= period;
    } else if (wrapped <= -period / 2) {
        wrapped += period;
    }
    return static_cast<int32_t>(wrapped);
}

}  // namespace utils
}  // namespace loramesher
