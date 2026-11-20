/**
 * @file counter_record_codec.hpp
 * @brief Persisted record of the transmit frame counters and its encoding
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "utils/compat/span.hpp"

namespace loramesher {
namespace security {

/**
 * @brief Transmit counter spaces
 *
 * The values index CounterRecord::limit.
 */
enum class CounterSpace : uint8_t {
    kBeacon = 0,    ///< C_bcn, 24 bits, every beacon sent or forwarded
    kControl = 1,   ///< C_ctl, 32 bits, every control frame transmitted
    kData = 2,      ///< C_data, 30 bits, every data frame originated
    kRecovery = 3,  ///< C_rec, 32 bits, every key request, never restarts
};

/// Number of counter spaces
constexpr size_t kCounterSpaceCount = 4;
/// Number of spaces that restart at an epoch switch and have a ceiling field
constexpr size_t kTrafficSpaceCount = 3;

/// All-ones value of each space. It is never sent and means "nothing seen"
constexpr uint32_t kCounterSpaceNone[kCounterSpaceCount] = {
    0x00FFFFFFu, 0xFFFFFFFFu, 0x3FFFFFFFu, 0xFFFFFFFFu};

/// Highest key epoch a node can transmit under
constexpr uint8_t kMaxEpoch = 254;

/// Size of the encoded record, in bytes
constexpr size_t kCounterRecordSize = 40;

/**
 * @brief Content of the counter store
 *
 * limit is the first value that is not yet allowed. ceiling is the first
 * value the node may never use. The ceiling of the recovery space is fixed
 * at its all-ones value.
 */
struct CounterRecord {
    uint8_t tx_epoch = 0;
    uint32_t limit[kCounterSpaceCount] = {0, 0, 0, 0};
    uint32_t ceiling[kTrafficSpaceCount] = {0x00FFFFFFu, 0xFFFFFFFFu,
                                            0x3FFFFFFFu};

    bool operator==(const CounterRecord& other) const {
        for (size_t i = 0; i < kCounterSpaceCount; ++i) {
            if (limit[i] != other.limit[i]) {
                return false;
            }
        }
        for (size_t i = 0; i < kTrafficSpaceCount; ++i) {
            if (ceiling[i] != other.ceiling[i]) {
                return false;
            }
        }
        return tx_epoch == other.tx_epoch;
    }
};

/**
 * @brief True when every field of the record is inside its space
 *
 * A ceiling may not exceed the all-ones value of its space, a limit may not
 * exceed its ceiling, and the epoch may not exceed kMaxEpoch.
 */
bool IsValidCounterRecord(const CounterRecord& record);

/**
 * @brief Encode a record into its 40-byte stored form
 *
 * Layout: magic "LMC1", format version, transmit epoch, two zero bytes,
 * limit and ceiling of C_bcn, C_ctl and C_data, limit of C_rec, CRC-32.
 * Multi-byte fields are little-endian.
 */
std::array<uint8_t, kCounterRecordSize> EncodeCounterRecord(
    const CounterRecord& record);

/**
 * @brief Decode a stored record
 *
 * @return The record, or std::nullopt when the size, the magic, the version,
 *         the reserved bytes, the CRC or a field value is wrong
 */
std::optional<CounterRecord> DecodeCounterRecord(std::span<const uint8_t> data);

}  // namespace security
}  // namespace loramesher
