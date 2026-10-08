/**
 * @file rtc_state_store.cpp
 * @brief State store in ESP32 RTC memory
 */

#include "types/storage/rtc_state_store.hpp"

#if defined(LORAMESHER_BUILD_ARDUINO) && defined(ARDUINO_ARCH_ESP32)

#include <esp_attr.h>
#include <esp_system.h>

#include <cstring>

namespace loramesher {
namespace storage {

namespace {

/// Marks a buffer written by Save() ("LMRT")
constexpr uint32_t kRtcBufferMagic = 0x54524D4C;

struct RtcBuffer {
    uint32_t magic;
    uint16_t length;
    uint16_t length_complement;  ///< ~length, guards the length field
    uint8_t data[RtcStateStore::kCapacity];
};

/// Not initialized at boot, so it keeps its content across resets that do
/// not cut the RTC power domain
RTC_NOINIT_ATTR RtcBuffer rtc_buffer;

bool ResetKeepsRtcMemory() {
    const esp_reset_reason_t reason = esp_reset_reason();
    return reason == ESP_RST_DEEPSLEEP || reason == ESP_RST_SW;
}

}  // namespace

Result RtcStateStore::Save(std::span<const uint8_t> data) {
    if (data.size() > kCapacity) {
        return Result(LoraMesherErrorCode::kBufferOverflow,
                      "State does not fit in RTC memory");
    }
    std::memcpy(rtc_buffer.data, data.data(), data.size());
    rtc_buffer.length = static_cast<uint16_t>(data.size());
    rtc_buffer.length_complement = static_cast<uint16_t>(~rtc_buffer.length);
    rtc_buffer.magic = kRtcBufferMagic;
    return Result::Success();
}

std::optional<std::vector<uint8_t>> RtcStateStore::Load() {
    if (!ResetKeepsRtcMemory() || rtc_buffer.magic != kRtcBufferMagic ||
        rtc_buffer.length != static_cast<uint16_t>(
                                 ~rtc_buffer.length_complement) ||
        rtc_buffer.length > kCapacity) {
        return std::nullopt;
    }
    return std::vector<uint8_t>(rtc_buffer.data,
                                rtc_buffer.data + rtc_buffer.length);
}

void RtcStateStore::Clear() {
    rtc_buffer.magic = 0;
    rtc_buffer.length = 0;
    rtc_buffer.length_complement = 0;
}

}  // namespace storage
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO && ARDUINO_ARCH_ESP32
