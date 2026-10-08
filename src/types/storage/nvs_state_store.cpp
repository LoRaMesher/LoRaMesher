/**
 * @file nvs_state_store.cpp
 * @brief State store in ESP32 non-volatile storage (flash)
 */

#include "types/storage/nvs_state_store.hpp"

#if defined(LORAMESHER_BUILD_ARDUINO) && defined(ARDUINO_ARCH_ESP32)

#include <Preferences.h>

#include <utility>

namespace loramesher {
namespace storage {

NvsStateStore::NvsStateStore(std::string name_space, std::string key)
    : name_space_(std::move(name_space)), key_(std::move(key)) {}

Result NvsStateStore::Save(std::span<const uint8_t> data) {
    Preferences preferences;
    if (!preferences.begin(name_space_.c_str(), false)) {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "Cannot open NVS namespace");
    }
    const size_t written =
        preferences.putBytes(key_.c_str(), data.data(), data.size());
    preferences.end();
    if (written != data.size()) {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "Cannot write state to NVS");
    }
    return Result::Success();
}

std::optional<std::vector<uint8_t>> NvsStateStore::Load() {
    Preferences preferences;
    // Read-only open fails when nothing was ever saved
    if (!preferences.begin(name_space_.c_str(), true)) {
        return std::nullopt;
    }
    std::optional<std::vector<uint8_t>> blob;
    const size_t length = preferences.getBytesLength(key_.c_str());
    if (length > 0) {
        std::vector<uint8_t> data(length);
        if (preferences.getBytes(key_.c_str(), data.data(), length) ==
            length) {
            blob = std::move(data);
        }
    }
    preferences.end();
    return blob;
}

void NvsStateStore::Clear() {
    Preferences preferences;
    if (!preferences.begin(name_space_.c_str(), false)) {
        return;
    }
    if (preferences.isKey(key_.c_str())) {
        preferences.remove(key_.c_str());
    }
    preferences.end();
}

}  // namespace storage
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO && ARDUINO_ARCH_ESP32
