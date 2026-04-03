#pragma once

#include <cstdint>

namespace loramesher {

/**
 * @brief Sliding window Packet Delivery Ratio tracker
 *
 * Tracks reception over the last N superframes using a circular bitset.
 * Provides a stable PDR metric that avoids the sawtooth oscillation
 * of EWMA on lossy links.
 *
 * @tparam WindowSize Number of superframes to track (default 8, max 32)
 */
template <uint8_t WindowSize = 8>
class SlidingWindowPDR {
   public:
    static_assert(WindowSize > 0 && WindowSize <= 32,
                  "WindowSize must be 1-32");

    /**
     * @brief Advance to next slot, assume miss
     *
     * Call once per superframe. The new slot starts as 0 (miss).
     * If a message arrives before the next Expect(), call Received().
     */
    void Expect() {
        position_ = (position_ + 1) % WindowSize;
        window_ &= ~(1u << position_);
        if (total_expected_ < WindowSize) {
            total_expected_++;
        }
    }

    /**
     * @brief Mark current slot as received
     *
     * Call when a routing table message is received from this node.
     */
    void Received() { window_ |= (1u << position_); }

    /**
     * @brief Get PDR as 0-255
     *
     * During warmup (fewer than WindowSize samples), scales over the
     * number of slots filled so far.
     *
     * @return uint8_t PDR scaled to 0-255
     */
    uint8_t GetPDR() const {
        if (total_expected_ == 0) {
            return 0;
        }
        uint8_t count = PopCount(window_);
        uint8_t effective =
            (total_expected_ < WindowSize) ? total_expected_ : WindowSize;
        return static_cast<uint8_t>(static_cast<uint16_t>(count) * 255 /
                                    effective);
    }

    /**
     * @brief True when the window has been fully populated at least once
     */
    bool IsReady() const { return total_expected_ >= WindowSize; }

    /**
     * @brief Reset to initial state
     */
    void Reset() {
        window_ = 0;
        position_ = 0;
        total_expected_ = 0;
    }

   private:
    static uint8_t PopCount(uint32_t v) {
#if defined(__GNUC__) || defined(__clang__)
        return static_cast<uint8_t>(__builtin_popcount(v));
#else
        // Portable fallback
        uint8_t count = 0;
        while (v) {
            count += v & 1;
            v >>= 1;
        }
        return count;
#endif
    }

    uint32_t window_ = 0;         ///< Bitset of reception results
    uint8_t position_ = 0;        ///< Current slot in circular buffer
    uint8_t total_expected_ = 0;  ///< Slots filled so far (up to WindowSize)
};

}  // namespace loramesher
