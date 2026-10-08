/**
 * @file nvs_state_store.hpp
 * @brief State store in ESP32 non-volatile storage (flash)
 */

#pragma once

#include "config/system_config.hpp"

#if defined(LORAMESHER_BUILD_ARDUINO) && defined(ARDUINO_ARCH_ESP32)

#include <string>

#include "types/storage/i_state_store.hpp"

namespace loramesher {
namespace storage {

/**
 * @brief IStateStore kept in the ESP32 NVS flash partition
 *
 * The blob survives OTA updates, resets and power loss. Every Save() and the
 * Clear() done at start-up write flash, so save before a planned reset only,
 * not periodically.
 */
class NvsStateStore : public IStateStore {
   public:
    /**
     * @param name_space NVS namespace (at most 15 characters)
     * @param key NVS key of the blob (at most 15 characters)
     */
    explicit NvsStateStore(std::string name_space = "loramesher",
                           std::string key = "state");

    Result Save(std::span<const uint8_t> data) override;
    std::optional<std::vector<uint8_t>> Load() override;
    void Clear() override;

   private:
    std::string name_space_;
    std::string key_;
};

}  // namespace storage
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO && ARDUINO_ARCH_ESP32
