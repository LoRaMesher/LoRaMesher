/**
 * @file memory_state_store.hpp
 * @brief State store kept in RAM
 */

#pragma once

#include <mutex>

#include "types/storage/i_state_store.hpp"

namespace loramesher {
namespace storage {

/**
 * @brief IStateStore kept in RAM
 *
 * Survives a protocol restart within the same process, which is what tests
 * and native builds use to emulate RTC memory or flash across a reboot. It does
 * not survive a real power loss or reset of the device.
 */
class MemoryStateStore : public IStateStore {
   public:
    Result Save(std::span<const uint8_t> data) override {
        std::lock_guard<std::mutex> lock(mutex_);
        blob_.emplace(data.begin(), data.end());
        ++save_count_;
        return Result::Success();
    }

    std::optional<std::vector<uint8_t>> Load() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return blob_;
    }

    void Clear() override {
        std::lock_guard<std::mutex> lock(mutex_);
        blob_.reset();
    }

    /**
     * @brief Number of successful Save() calls
     */
    uint32_t GetSaveCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return save_count_;
    }

   private:
    mutable std::mutex mutex_;
    std::optional<std::vector<uint8_t>> blob_;
    uint32_t save_count_ = 0;
};

}  // namespace storage
}  // namespace loramesher
