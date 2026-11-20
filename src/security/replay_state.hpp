/**
 * @file replay_state.hpp
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
    kAccept = 0,      ///< Counter was not seen before
    kDuplicate,       ///< Counter was already accepted
    kTooOld,          ///< Counter is behind what the check still tracks
    kInvalidCounter,  ///< Counter is the all-ones value or beyond the space
};

/// "Nothing seen yet" value of the beacon counter (24 bits)
constexpr uint32_t kBeaconCounterNone = 0x00FFFFFFu;
/// "Nothing seen yet" value of the control counter (32 bits)
constexpr uint32_t kControlCounterNone = 0xFFFFFFFFu;
/// "Nothing seen yet" value of the data counter (30 bits)
constexpr uint32_t kDataCounterNone = 0x3FFFFFFFu;

/**
 * @brief Strict check for beacon and control counters
 *
 * These frames travel one hop and arrive in order, so a counter is accepted
 * only when it is higher than every counter accepted before. The state is
 * the highest accepted counter. The all-ones value of the space means that
 * nothing was accepted yet. A received counter with that value is rejected,
 * since no sender ever uses it and accepting it would clear the state.
 *
 * Use Check() and Commit() only after the MIC of the frame verified.
 */
class StrictReplayCounter {
   public:
    /**
     * @param none All-ones value of the space, kBeaconCounterNone or
     *        kControlCounterNone
     */
    explicit StrictReplayCounter(uint32_t none) : none_(none), last_(none) {}

    /// Classify a received counter without changing the state
    ReplayVerdict Check(uint32_t counter) const {
        if (counter >= none_) {
            return ReplayVerdict::kInvalidCounter;
        }
        if (last_ == none_ || counter > last_) {
            return ReplayVerdict::kAccept;
        }
        return counter == last_ ? ReplayVerdict::kDuplicate
                                : ReplayVerdict::kTooOld;
    }

    /// Record an accepted counter. A rejected one changes nothing
    void Commit(uint32_t counter) {
        if (Check(counter) == ReplayVerdict::kAccept) {
            last_ = counter;
        }
    }

    /// Check() and, when the counter is acceptable, Commit() it
    ReplayVerdict CheckAndCommit(uint32_t counter) {
        const ReplayVerdict verdict = Check(counter);
        if (verdict == ReplayVerdict::kAccept) {
            last_ = counter;
        }
        return verdict;
    }

    /// True once a counter was accepted
    bool HasSeenAny() const { return last_ != none_; }

    /// Highest accepted counter, or the all-ones value
    uint32_t GetLast() const { return last_; }

    /// Continue from a saved value. A value beyond the space means none
    void Restore(uint32_t last) { last_ = last < none_ ? last : none_; }

    /// Forget everything
    void Reset() { last_ = none_; }

   private:
    uint32_t none_;
    uint32_t last_;
};

/**
 * @brief Sliding window of 32 for data counters
 *
 * Data frames can overtake each other, so the window accepts a counter up
 * to 31 behind the highest one, each value at most once. Bit b of the
 * bitmap stands for counter high - b. One window tracks one origin under
 * one key epoch.
 *
 * The first accepted counter sets every bit, so nothing older than it is
 * accepted afterwards. The same holds after a resume, where only the
 * highest counter is known.
 *
 * Use Check() and Commit() only after the MIC or tag of the frame verified.
 */
class ReplayWindow {
   public:
    /// Number of counter values the bitmap tracks, the highest included
    static constexpr uint32_t kWindowSize = 32;

    /// Exact content of the window
    struct State {
        uint32_t high = kDataCounterNone;
        uint32_t bitmap = 0;

        bool operator==(const State& other) const {
            return high == other.high && bitmap == other.bitmap;
        }
    };

    /// Classify a received counter without changing the window
    ReplayVerdict Check(uint32_t counter) const {
        if (counter >= kDataCounterNone) {
            return ReplayVerdict::kInvalidCounter;
        }
        if (state_.high == kDataCounterNone || counter > state_.high) {
            return ReplayVerdict::kAccept;
        }
        const uint32_t age = state_.high - counter;
        if (age >= kWindowSize) {
            return ReplayVerdict::kTooOld;
        }
        return (state_.bitmap & (uint32_t{1} << age)) != 0
                   ? ReplayVerdict::kDuplicate
                   : ReplayVerdict::kAccept;
    }

    /// Record an accepted counter. A rejected one changes nothing
    void Commit(uint32_t counter) {
        if (Check(counter) != ReplayVerdict::kAccept) {
            return;
        }
        if (state_.high == kDataCounterNone) {
            state_.high = counter;
            state_.bitmap = 0xFFFFFFFFu;
        } else if (counter > state_.high) {
            const uint32_t advance = counter - state_.high;
            state_.bitmap =
                advance >= kWindowSize ? 1 : (state_.bitmap << advance) | 1;
            state_.high = counter;
        } else {
            state_.bitmap |= uint32_t{1} << (state_.high - counter);
        }
    }

    /// Check() and, when the counter is acceptable, Commit() it
    ReplayVerdict CheckAndCommit(uint32_t counter) {
        const ReplayVerdict verdict = Check(counter);
        if (verdict == ReplayVerdict::kAccept) {
            Commit(counter);
        }
        return verdict;
    }

    /// True once a counter was accepted
    bool HasSeenAny() const { return state_.high != kDataCounterNone; }

    /// Highest accepted counter, or kDataCounterNone
    uint32_t GetHigh() const { return state_.high; }

    const State& GetState() const { return state_; }

    /**
     * @brief Continue after a resume, where only the highest counter is kept
     *
     * Every bit is set, so only counters above high are accepted.
     */
    void RestoreHigh(uint32_t high) {
        state_.high = high < kDataCounterNone ? high : kDataCounterNone;
        state_.bitmap = state_.high == kDataCounterNone ? 0 : 0xFFFFFFFFu;
    }

    /// Forget everything
    void Reset() { state_ = State{}; }

   private:
    State state_;
};

/**
 * @brief Outcome of comparing a frame epoch with the recorded peer epoch
 */
enum class EpochVerdict : uint8_t {
    kCurrent = 0,  ///< Same epoch, the replay state applies
    kNewer,        ///< Newer epoch, the replay state was cleared
    kOlder,        ///< Older epoch, the frame is dropped
};

/**
 * @brief Hop layer replay state of one peer
 *
 * Holds the epoch of the last accepted frame, the strict beacon and control
 * counters, hop window G for data frames without the end-to-end flag, and
 * hop window R for end-to-end data frames that this node relayed.
 */
struct HopReplayState {
    explicit HopReplayState(uint8_t peer_epoch) : epoch(peer_epoch) {}

    /**
     * @brief Apply the epoch rule for a frame whose MIC verified
     *
     * A frame under a newer epoch clears every field and records the epoch.
     */
    EpochVerdict AcceptEpoch(uint8_t frame_epoch) {
        if (frame_epoch < epoch) {
            return EpochVerdict::kOlder;
        }
        if (frame_epoch == epoch) {
            return EpochVerdict::kCurrent;
        }
        epoch = frame_epoch;
        beacon.Reset();
        control.Reset();
        group.Reset();
        relayed.Reset();
        return EpochVerdict::kNewer;
    }

    uint8_t epoch;
    StrictReplayCounter beacon{kBeaconCounterNone};
    StrictReplayCounter control{kControlCounterNone};
    ReplayWindow group;    ///< Hop window G
    ReplayWindow relayed;  ///< Hop window R, used by relays only
};

/**
 * @brief End-to-end replay state of one peer with a node key
 *
 * Used at the destination only, after the body tag verified.
 */
struct EndToEndReplayState {
    explicit EndToEndReplayState(uint8_t peer_epoch) : epoch(peer_epoch) {}

    /// Apply the epoch rule for a frame whose tag verified
    EpochVerdict AcceptEpoch(uint8_t frame_epoch) {
        if (frame_epoch < epoch) {
            return EpochVerdict::kOlder;
        }
        if (frame_epoch == epoch) {
            return EpochVerdict::kCurrent;
        }
        epoch = frame_epoch;
        window.Reset();
        return EpochVerdict::kNewer;
    }

    uint8_t epoch;
    ReplayWindow window;
};

}  // namespace security
}  // namespace loramesher
