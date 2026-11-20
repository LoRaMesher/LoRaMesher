/**
 * @file frame_counter.hpp
 * @brief Transmit frame counters that never repeat a value across resets
 */

#pragma once

#include <cstdint>
#include <memory>

#include "security/i_counter_store.hpp"

namespace loramesher {
namespace security {

/**
 * @brief Outcome of a frame counter operation
 */
enum class CounterStatus : uint8_t {
    kOk = 0,
    kNotStarted,       ///< Start() or Restore() has not succeeded
    kStoreError,       ///< Record missing or corrupt, save failed, or the
                       ///< saved limit was reached before the refill
    kExhausted,        ///< The space reached its ceiling
    kSealLocked,       ///< The state was captured for deep sleep
    kInvalidArgument,  ///< Epoch or snapshot not acceptable
};

/**
 * @brief The four transmit counter spaces with reserve-ahead persistence
 *
 * A counter value feeds a nonce, so no value may be used twice under one
 * key, including after a crash. The counters therefore hand out only values
 * below a limit that is already durable in the counter store.
 *
 * Reservation rules:
 * - A boot reads the record and writes nothing. Each counter starts at its
 *   saved limit.
 * - A space gets its first reservation when the node first needs to transmit
 *   in it. The owner calls EnsureReserved() at that point.
 * - The k-th reservation of a space since boot covers min(2^(k-1), B)
 *   values, with B = 64 for beacons and 256 for control and data. A boot
 *   that seals n frames in a space consumes at most 3n + 1 values, and none
 *   when the space is never used.
 * - Next() never touches the store. After a value is taken, a refill is
 *   scheduled when a quarter of the last block or less is left. The owner
 *   runs it with RunPendingSave() at a time when no frame is due.
 * - The recovery space is reserved one value at a time and has no refill.
 * - An epoch switch saves the new epoch with the three traffic limits at 0
 *   and restarts the ramp. Ceilings and the recovery space are kept.
 *
 * The class is not thread safe. Its owner serializes the calls.
 */
class FrameCounters {
   public:
    /// Largest reservation of each space
    static constexpr uint32_t kMaxBlock[kCounterSpaceCount] = {64, 256, 256, 1};

    /// Marks a space without a reservation since boot in State
    static constexpr uint8_t kNoReservation = 0xFF;

    /**
     * @brief Exact position of the traffic counters, for the resume snapshot
     *
     * The recovery space is not part of it. Its block size is 1, so the
     * store always holds its exact value.
     */
    struct State {
        uint8_t tx_epoch = 0;
        uint32_t next[kTrafficSpaceCount] = {0, 0, 0};
        uint32_t limit[kTrafficSpaceCount] = {0, 0, 0};
        /// log2 of the size of the last reservation, or kNoReservation
        uint8_t reservation_log2[kTrafficSpaceCount] = {
            kNoReservation, kNoReservation, kNoReservation};

        bool operator==(const State& other) const;
    };

    /**
     * @param store Store of the counter record. May be null only when
     *        StartVolatile() is used
     */
    explicit FrameCounters(std::shared_ptr<ICounterStore> store);

    /**
     * @brief Continue from the stored record after a cold boot
     *
     * Writes nothing.
     *
     * @return kOk, or kStoreError when there is no store or no valid record.
     *         The counters then stay stopped and the node must stay silent
     */
    CounterStatus Start();

    /**
     * @brief Start every counter at 0 in epoch 0 without any persistence
     *
     * For tests and development builds only. A reset repeats counter values.
     */
    CounterStatus StartVolatile();

    /**
     * @brief Make sure the space has at least one value to hand out
     *
     * Call at the first need of a space: state entry for beacons and control
     * frames, first send for data, every key request for recovery. The save
     * runs inside the call. Does nothing when values are left.
     *
     * @return kOk, kStoreError when the save failed, kExhausted
     */
    CounterStatus EnsureReserved(CounterSpace space);

    /**
     * @brief Hand out the next value of a space
     *
     * Never touches the store.
     *
     * @param value Receives the value, valid only on kOk
     * @return kOk, kStoreError when the saved limit is reached (the frame
     *         stays queued until a save succeeded), kExhausted, kSealLocked
     *         or kNotStarted. A value is never handed out on an error
     */
    CounterStatus Next(CounterSpace space, uint32_t& value);

    /// True when a refill is scheduled
    bool HasPendingSave() const;

    /**
     * @brief Run the scheduled refills with one save
     *
     * A failed save leaves the old limits in force and stays scheduled.
     *
     * @return kOk, also when nothing was scheduled, or kStoreError
     */
    CounterStatus RunPendingSave();

    /**
     * @brief Move to a higher key epoch
     *
     * Saves the record for the new epoch before any frame is sent under it.
     *
     * @param new_epoch Epoch above the current one, at most kMaxEpoch
     * @return kOk, kInvalidArgument, or kStoreError with the state unchanged
     */
    CounterStatus SwitchEpoch(uint8_t new_epoch);

    /**
     * @brief Capture the state for the resume snapshot and lock sealing
     *
     * After the capture Next() returns kSealLocked, so no value is used
     * that the snapshot does not know of. Saves are refused in the same
     * way, so the stored record stays the one the snapshot matches.
     */
    State Capture();

    /**
     * @brief Unlock sealing after a captured snapshot was thrown away
     *
     * Only for a sleep that did not happen. The snapshot must not be used.
     */
    void DiscardCapture() { seal_locked_ = false; }

    /**
     * @brief Continue from a snapshot after a deep sleep
     *
     * Writes nothing, so a resume wastes no value. The snapshot is accepted
     * only when its epoch and limits equal the stored record. The snapshot
     * must be restored at most once.
     *
     * @return kOk, kStoreError when the record cannot be read, or
     *         kInvalidArgument when the snapshot does not match it. On an
     *         error nothing changes and the caller falls back to Start()
     */
    CounterStatus Restore(const State& state);

    bool IsStarted() const { return started_; }

    /// Transmit epoch. After Start() it is the epoch of the stored record
    uint8_t GetEpoch() const { return epoch_; }

    uint32_t GetNext(CounterSpace space) const { return next_[Index(space)]; }

    uint32_t GetLimit(CounterSpace space) const { return limit_[Index(space)]; }

    uint32_t GetCeiling(CounterSpace space) const {
        return ceiling_[Index(space)];
    }

    /// True when the space reached its ceiling
    bool IsExhausted(CounterSpace space) const {
        return started_ && next_[Index(space)] >= ceiling_[Index(space)];
    }

    /**
     * @brief True when few values are left before the ceiling
     *
     * Fewer than 2^18 for beacons and fewer than 2^24 for the other spaces.
     */
    bool IsLow(CounterSpace space) const;

    /// Number of successful saves since construction
    uint32_t GetStoreWrites() const { return store_writes_; }

   private:
    static size_t Index(CounterSpace space) {
        return static_cast<size_t>(space);
    }

    void Load(const CounterRecord& record);
    CounterRecord MakeRecord() const;
    Result Save(const CounterRecord& record);
    CounterStatus Reserve(const bool spaces[kCounterSpaceCount]);
    void ScheduleRefillIfDue(size_t index);

    std::shared_ptr<ICounterStore> store_;
    uint8_t epoch_ = 0;
    uint32_t next_[kCounterSpaceCount] = {0, 0, 0, 0};
    uint32_t limit_[kCounterSpaceCount] = {0, 0, 0, 0};
    uint32_t ceiling_[kCounterSpaceCount] = {0, 0, 0, 0};
    uint8_t reservation_log2_[kCounterSpaceCount] = {
        kNoReservation, kNoReservation, kNoReservation, kNoReservation};
    bool refill_pending_[kCounterSpaceCount] = {false, false, false, false};
    uint32_t store_writes_ = 0;
    bool started_ = false;
    bool volatile_ = false;
    bool seal_locked_ = false;
};

}  // namespace security
}  // namespace loramesher
