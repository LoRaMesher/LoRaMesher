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
 * Survives a restart of the counters within the same process, which is what
 * tests and native builds use to emulate flash across a reboot. It can be
 * told to fail, to exercise the error paths of its user.
 */
class MemoryCounterStore : public ICounterStore {
   public:
    /// Starts empty, as a device that was never provisioned
    MemoryCounterStore() = default;

    /// Starts with a record, as a provisioned device
    explicit MemoryCounterStore(const CounterRecord& record)
        : record_(record) {}

    std::optional<CounterRecord> Load() override {
        ++load_count_;
        return fail_load_ ? std::nullopt : record_;
    }

    Result Save(const CounterRecord& record) override {
        if (fail_save_) {
            return Result(LoraMesherErrorCode::kHardwareError,
                          "Counter store write failed");
        }
        record_ = record;
        ++save_count_;
        return Result::Success();
    }

    /// Make every following Load() return nothing, or work again
    void SetLoadFailure(bool fail) { fail_load_ = fail; }

    /// Make every following Save() fail, or work again
    void SetSaveFailure(bool fail) { fail_save_ = fail; }

    /// Remove the record
    void Erase() { record_.reset(); }

    /// Record currently held
    const std::optional<CounterRecord>& GetRecord() const { return record_; }

    /// Number of successful Save() calls
    uint32_t GetSaveCount() const { return save_count_; }

    /// Number of Load() calls
    uint32_t GetLoadCount() const { return load_count_; }

   private:
    std::optional<CounterRecord> record_;
    uint32_t save_count_ = 0;
    uint32_t load_count_ = 0;
    bool fail_load_ = false;
    bool fail_save_ = false;
};

}  // namespace security
}  // namespace loramesher
