#pragma once

#include <cstdint>

namespace loramesher {
namespace airtime {

/**
 * @brief Decide whether Low Data Rate Optimization should be enabled.
 *
 * LDRO is enabled when the LoRa symbol time is 16 ms or longer, matching the
 * Semtech reference and RadioLib's automatic behavior.
 *
 * @param spreading_factor Spreading factor (6-12)
 * @param bandwidth_khz Signal bandwidth in kHz
 * @return true if LDRO should be enabled for this configuration
 */
inline bool ShouldEnableLdro(uint8_t spreading_factor, float bandwidth_khz) {
    if (bandwidth_khz <= 0.0f) {
        return false;
    }
    const float symbol_time_ms =
        static_cast<float>(uint32_t(1) << spreading_factor) / bandwidth_khz;
    return symbol_time_ms >= 16.0f;
}

/**
 * @brief Compute the LoRa time-on-air for a payload using the Semtech formula.
 *
 * Implements the airtime equation from the SX127x/SX126x datasheets so the
 * result is independent of the radio driver's internal state.
 *
 * @param spreading_factor Spreading factor (6-12)
 * @param bandwidth_khz Signal bandwidth in kHz
 * @param coding_rate_denom Coding rate denominator (5-8 for 4/5 to 4/8)
 * @param payload_bytes Payload length in bytes
 * @param preamble_symbols Programmed preamble length in symbols
 * @param crc_enabled Whether the payload CRC is enabled
 * @param explicit_header Whether the explicit LoRa header is used
 * @param low_data_rate_optimize Whether LDRO is enabled
 * @return Time-on-air in milliseconds
 */
inline uint32_t CalculateTimeOnAirMs(
    uint8_t spreading_factor, float bandwidth_khz, uint8_t coding_rate_denom,
    uint16_t payload_bytes, uint16_t preamble_symbols, bool crc_enabled,
    bool explicit_header, bool low_data_rate_optimize) {
    if (bandwidth_khz <= 0.0f) {
        return 0;
    }

    const float bandwidth_hz = bandwidth_khz * 1000.0f;
    const float symbol_time_ms =
        (static_cast<float>(uint32_t(1) << spreading_factor) / bandwidth_hz) *
        1000.0f;

    const int sf = static_cast<int>(spreading_factor);
    const int de = low_data_rate_optimize ? 1 : 0;
    const int ih = explicit_header ? 0 : 1;
    const int crc = crc_enabled ? 1 : 0;
    const int cr = static_cast<int>(coding_rate_denom) - 4;  // 4/5..4/8 -> 1..4

    const float preamble_time_ms =
        (static_cast<float>(preamble_symbols) + 4.25f) * symbol_time_ms;

    const int numerator =
        8 * static_cast<int>(payload_bytes) - 4 * sf + 28 + 16 * crc - 20 * ih;
    const int denominator = 4 * (sf - 2 * de);

    int blocks = 0;
    if (numerator > 0 && denominator > 0) {
        blocks = (numerator + denominator - 1) / denominator;  // ceil
    }
    const int payload_symbols = 8 + blocks * (cr + 4);
    const float payload_time_ms =
        static_cast<float>(payload_symbols) * symbol_time_ms;

    return static_cast<uint32_t>(preamble_time_ms + payload_time_ms + 0.5f);
}

}  // namespace airtime
}  // namespace loramesher
