/**
 * @file frame_counter.hpp
 * @brief Transmit frame counter that never repeats a value across resets
 */

#pragma once

#include <cstdint>
#include <memory>

#include "security/i_counter_store.hpp"
#include "types/error_codes/result.hpp"

namespace loramesher {
namespace security {

/**
 * @brief Monotonic 32-bit transmit counter with reserve-ahead persistence
 *
 * A frame counter feeds the nonce, so a value must never be used twice with
 * the same key, including after a crash. Writing flash for every frame would
 * wear it out, so the counter reserves a block of values at a time. It
 * persists a high-water mark first and then hands out the values below it.
 * After a reset it continues from the persisted mark and skips whatever was
 * left of the interrupted block.
 *
 * Invariant: every value handed out is lower than the persisted mark.
 *
 * The value 0xFFFFFFFF is never handed out. When the counter reaches it the
 * key is used up and Next() reports an error until the key is replaced.
 *
 * The class is not thread safe. Its owner serializes the calls.
 */
class FrameCounter {
   public:
    /// Values reserved by one write of the store
    static constexpr uint32_t kDefaultBlockSize = 1024;

    /// First value that is never handed out
    static constexpr uint32_t kCounterLimit = 0xFFFFFFFFu;

    /**
     * @brief Exact position of the counter, for the deep sleep snapshot
     */
    struct State {
        uint32_t next = 0;          ///< Value the next call hands out
        uint32_t reserved_end = 0;  ///< Persisted mark that covers next

        bool operator==(const State& other) const {
            return next == other.next && reserved_end == other.reserved_end;
        }
    };

    /**
     * @param store Store of the high-water mark
     * @param block_size Values reserved per store write, at least 1
     */
    explicit FrameCounter(std::shared_ptr<ICounterStore> store,
                          uint32_t block_size = kDefaultBlockSize);

    /**
     * @brief Continue from the persisted mark
     *
     * Call once after a cold start. Reading the mark writes nothing. The
     * first Next() reserves a block.
     *
     * @return Success, or the error of the store. The counter stays stopped
     *         on an error
     */
    Result Start();

    /**
     * @brief Hand out the next counter value
     *
     * When the reserved block is used up, the new mark is stored before the
     * value is returned.
     *
     * @param counter Receives the value, valid only on success
     * @return Success, or an error when the counter is not started, the
     *         store write failed, or the counter space is exhausted. A value
     *         is never handed out on an error. A failed store write is
     *         tried again by the next call
     */
    Result Next(uint32_t& counter);

    /**
     * @brief Snapshot of the exact position
     *
     * Save it with the resume snapshot before deep sleep.
     */
    State GetState() const { return {next_, reserved_end_}; }

    /**
     * @brief Continue from a snapshot taken before deep sleep
     *
     * Writes nothing, so a resume does not burn a block. The snapshot is
     * accepted only when its mark equals the persisted mark, which shows
     * that no block was reserved after the snapshot was taken.
     *
     * A snapshot must be restored at most once. The resume snapshot store
     * erases the snapshot when it is read, which gives that guarantee.
     *
     * @param state Snapshot from GetState()
     * @return Success, or an error when the store cannot be read or the
     *         snapshot does not match it. On an error the counter is left
     *         as it was and the caller falls back to Start()
     */
    Result Restore(const State& state);

    /// True once Start() or Restore() succeeded
    bool IsStarted() const { return started_; }

    /// True when every value has been handed out
    bool IsExhausted() const { return started_ && next_ == kCounterLimit; }

    /// Values left before the counter space is exhausted
    uint32_t GetRemaining() const { return kCounterLimit - next_; }

   private:
    std::shared_ptr<ICounterStore> store_;
    uint32_t block_size_;
    uint32_t next_ = 0;
    uint32_t reserved_end_ = 0;
    bool started_ = false;
};

}  // namespace security
}  // namespace loramesher
