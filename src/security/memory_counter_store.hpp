/**
 * @file memory_counter_store.hpp
 * @brief Counter store kept in RAM
 */

#pragma once

#include "security/i_counter_store.hpp"

namespace loramesher {
namespace security {

/**
 * @brief ICounterStore kept in RAM
 *
 * Survives a restart of the counter within the same process, which is what
 * tests and native builds use to emulate flash across a reboot. It can be
 * told to fail, to exercise the error paths of its user.
 */
class MemoryCounterStore : public ICounterStore {
   public:
    explicit MemoryCounterStore(uint32_t mark = 0) : mark_(mark) {}

    Result Load(uint32_t& mark) override {
        if (fail_load_) {
            return Result(LoraMesherErrorCode::kHardwareError,
                          "Counter store read failed");
        }
        mark = mark_;
        return Result::Success();
    }

    Result Store(uint32_t mark) override {
        if (fail_store_) {
            return Result(LoraMesherErrorCode::kHardwareError,
                          "Counter store write failed");
        }
        mark_ = mark;
        ++store_count_;
        return Result::Success();
    }

    /// Make every following Load() fail or work again
    void SetLoadFailure(bool fail) { fail_load_ = fail; }

    /// Make every following Store() fail or work again
    void SetStoreFailure(bool fail) { fail_store_ = fail; }

    /// Mark currently held
    uint32_t GetMark() const { return mark_; }

    /// Number of successful Store() calls
    uint32_t GetStoreCount() const { return store_count_; }

   private:
    uint32_t mark_;
    uint32_t store_count_ = 0;
    bool fail_load_ = false;
    bool fail_store_ = false;
};

}  // namespace security
}  // namespace loramesher
