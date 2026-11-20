/**
 * @file replay_window.hpp
 * @brief Receive side checks that reject replayed frame counters
 */

#pragma once

#include <cstdint>

namespace loramesher {
namespace security {

/**
 * @brief Outcome of a replay check
 */
enum class ReplayVerdict : uint8_t {
    kAccept = 0,  ///< Counter was not seen before
    kDuplicate,   ///< Counter was already accepted
    kTooOld,      ///< Counter is behind what the check still tracks
};

/**
 * @brief Sliding replay window: highest counter seen plus a 32-bit bitmap
 *
 * Tolerates frames that arrive out of order by up to 31 counter values, as
 * retransmissions and multi-path forwarding produce, and still accepts every
 * counter at most once. One window tracks one sender under one key.
 *
 * Use Check() before the tag is verified and Commit() only after it
 * verified. A forged frame then cannot move the window.
 *
 * The state is 8 bytes and can be copied into the resume snapshot.
 */
class ReplayWindow {
   public:
    /// Number of counter values the bitmap tracks, the highest included
    static constexpr uint32_t kWindowSize = 32;

    /**
     * @brief Exact content of the window
     *
     * Bit i of bitmap stands for counter highest - i. A bitmap of 0 means
     * that no counter was accepted yet.
     */
    struct State {
        uint32_t highest = 0;
        uint32_t bitmap = 0;

        bool operator==(const State& other) const {
            return highest == other.highest && bitmap == other.bitmap;
        }
    };

    /**
     * @brief Classify a received counter without changing the window
     */
    ReplayVerdict Check(uint32_t counter) const {
        if (state_.bitmap == 0 || counter > state_.highest) {
            return ReplayVerdict::kAccept;
        }
        const uint32_t age = state_.highest - counter;
        if (age >= kWindowSize) {
            return ReplayVerdict::kTooOld;
        }
        return (state_.bitmap & (uint32_t{1} << age)) != 0
                   ? ReplayVerdict::kDuplicate
                   : ReplayVerdict::kAccept;
    }

    /**
     * @brief Record a counter whose frame was authenticated
     *
     * A counter that Check() would reject leaves the window unchanged.
     */
    void Commit(uint32_t counter) {
        if (state_.bitmap == 0) {
            state_.highest = counter;
            state_.bitmap = 1;
        } else if (counter > state_.highest) {
            const uint32_t advance = counter - state_.highest;
            state_.bitmap =
                advance >= kWindowSize ? 0 : state_.bitmap << advance;
            state_.bitmap |= 1;
            state_.highest = counter;
        } else if (state_.highest - counter < kWindowSize) {
            state_.bitmap |= uint32_t{1} << (state_.highest - counter);
        }
    }

    /**
     * @brief Check() and, when the counter is acceptable, Commit() it
     *
     * Only for counters that are already authenticated.
     */
    ReplayVerdict CheckAndCommit(uint32_t counter) {
        const ReplayVerdict verdict = Check(counter);
        if (verdict == ReplayVerdict::kAccept) {
            Commit(counter);
        }
        return verdict;
    }

    /// True once a counter was committed
    bool HasSeenAny() const { return state_.bitmap != 0; }

    const State& GetState() const { return state_; }

    void Restore(const State& state) { state_ = state; }

    /// Forget everything, for example after a key change
    void Reset() { state_ = State{}; }

   private:
    State state_;
};

/**
 * @brief Replay check that keeps only the highest counter seen
 *
 * For relays, which keep state for many senders and need it small. It
 * accepts a counter only when it is higher than every counter accepted
 * before, so a frame that arrives out of order is dropped.
 */
class HighestCounterWindow {
   public:
    /**
     * @brief Exact content of the check
     */
    struct State {
        uint32_t highest = 0;
        bool has_seen = false;

        bool operator==(const State& other) const {
            return highest == other.highest && has_seen == other.has_seen;
        }
    };

    /**
     * @brief Classify a received counter without changing the state
     */
    ReplayVerdict Check(uint32_t counter) const {
        if (!state_.has_seen || counter > state_.highest) {
            return ReplayVerdict::kAccept;
        }
        return counter == state_.highest ? ReplayVerdict::kDuplicate
                                         : ReplayVerdict::kTooOld;
    }

    /**
     * @brief Record a counter whose frame was authenticated
     *
     * A counter that Check() would reject leaves the state unchanged.
     */
    void Commit(uint32_t counter) {
        if (!state_.has_seen || counter > state_.highest) {
            state_.highest = counter;
            state_.has_seen = true;
        }
    }

    /**
     * @brief Check() and, when the counter is acceptable, Commit() it
     *
     * Only for counters that are already authenticated.
     */
    ReplayVerdict CheckAndCommit(uint32_t counter) {
        const ReplayVerdict verdict = Check(counter);
        if (verdict == ReplayVerdict::kAccept) {
            Commit(counter);
        }
        return verdict;
    }

    /// True once a counter was committed
    bool HasSeenAny() const { return state_.has_seen; }

    const State& GetState() const { return state_; }

    void Restore(const State& state) { state_ = state; }

    /// Forget everything, for example after a key change
    void Reset() { state_ = State{}; }

   private:
    State state_;
};

}  // namespace security
}  // namespace loramesher
