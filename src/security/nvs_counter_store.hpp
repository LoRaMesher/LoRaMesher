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
 * The mark survives resets, power loss and OTA updates. FrameCounter writes
 * it once per reserved block, so flash wear stays low. Each counter needs
 * its own key. NVS must be initialized, which the Arduino core does at boot.
 */
class NvsCounterStore : public ICounterStore {
   public:
    /**
     * @param name_space NVS namespace (at most 15 characters)
     * @param key NVS key of the mark (at most 15 characters)
     */
    explicit NvsCounterStore(std::string name_space = "loramesher",
                             std::string key = "tx_counter");

    Result Load(uint32_t& mark) override;
    Result Store(uint32_t mark) override;

   private:
    std::string name_space_;
    std::string key_;
};

}  // namespace security
}  // namespace loramesher

#endif  // ESP_PLATFORM
