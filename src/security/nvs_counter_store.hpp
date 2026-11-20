/**
 * @file nvs_counter_store.hpp
 * @brief Counter store in ESP32 non-volatile storage (flash)
 */

#pragma once

#if defined(ESP_PLATFORM)

#include <string>

#include "security/i_counter_store.hpp"

namespace loramesher {
namespace security {

/**
 * @brief ICounterStore kept in the ESP32 NVS flash partition
 *
 * The record is one 40-byte blob under one NVS key. It survives resets,
 * power loss and OTA updates. NVS must be initialized, which the Arduino
 * core does at boot.
 */
class NvsCounterStore : public ICounterStore {
   public:
    /// Why the last Load() returned what it did
    enum class LoadStatus : uint8_t {
        kNotLoaded,   ///< Load() was not called yet
        kOk,          ///< A valid record was read
        kAbsent,      ///< NVS holds no record under the key
        kCorrupt,     ///< A blob exists and does not decode
        kReadFailed,  ///< NVS reported an error
    };

    /**
     * @param name_space NVS namespace (at most 15 characters)
     * @param key NVS key of the record (at most 15 characters)
     */
    explicit NvsCounterStore(std::string name_space = "loramesher",
                             std::string key = "counters");

    std::optional<CounterRecord> Load() override;
    Result Save(const CounterRecord& record) override;

    /// Tells an absent record from a corrupt one and from a read failure
    LoadStatus GetLastLoadStatus() const { return last_load_status_; }

   private:
    std::string name_space_;
    std::string key_;
    LoadStatus last_load_status_ = LoadStatus::kNotLoaded;
};

}  // namespace security
}  // namespace loramesher

#endif  // ESP_PLATFORM
