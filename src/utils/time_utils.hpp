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

}  // namespace utils
}  // namespace loramesher
