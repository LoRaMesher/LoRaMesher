/**
 * @file rtc_state_store.hpp
 * @brief State store in ESP32 RTC memory
 */

#pragma once

#include "config/system_config.hpp"

#if defined(LORAMESHER_BUILD_ARDUINO) && defined(ARDUINO_ARCH_ESP32)

#include <cstddef>

#include "types/storage/i_state_store.hpp"

namespace loramesher {
namespace storage {

/**
 * @brief IStateStore kept in ESP32 RTC slow memory
 *
 * The blob survives deep sleep and software resets (esp_restart()) without
 * wearing the flash, but not power loss or brownout. Load() only returns a
 * blob after a deep sleep wake-up or a software reset, so memory left over
 * from an unrelated reset is never used.
 *
 * A new firmware image may place the buffer elsewhere in RTC memory: the
 * snapshot's magic and CRC then reject it and the node starts cold. Use
 * NvsStateStore to keep the state across an OTA update.
 */
class RtcStateStore : public IStateStore {
   public:
    /// Largest blob the store holds: a manager snapshot with every member, or
    /// a member's resume snapshot with about 60 direct neighbours (half of
    /// the 8 KB RTC slow memory)
    static constexpr size_t kCapacity = 4096;

    Result Save(std::span<const uint8_t> data) override;
    std::optional<std::vector<uint8_t>> Load() override;
    void Clear() override;
};

}  // namespace storage
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO && ARDUINO_ARCH_ESP32
