/**
 * @file snapshot_codec.hpp
 * @brief Binary encoding of a NetworkSnapshot
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "types/storage/network_snapshot.hpp"
#include "utils/compat/span.hpp"

namespace loramesher {
namespace storage {

/**
 * @brief Encodes and decodes NetworkSnapshot blobs
 *
 * Little-endian layout:
 * | Offset | Size  | Field                                      |
 * |--------|-------|--------------------------------------------|
 * | 0      | 4     | Magic "LMS1"                               |
 * | 4      | 1     | Format version                             |
 * | 5      | 1     | Flags (bit 0: was network manager)         |
 * | 6      | 2     | Node address                               |
 * | 8      | 2     | Network id                                 |
 * | 10     | 1     | Last sequence number                       |
 * | 11     | 1     | Network depth                              |
 * | 12     | 4     | Superframe duration (ms)                   |
 * | 16     | 1     | Reservation count N                        |
 * | 17     | 3 * N | Reservations: address (2), control slot (1)|
 * | 17+3N  | 4     | CRC-32 of all previous bytes               |
 *
 * Decode() rejects any blob that is truncated, corrupted, of another format
 * version, or whose content is inconsistent, so a stale or foreign blob always
 * leads to a cold start.
 */
class SnapshotCodec {
   public:
    /// Magic "LMS1" read as a little-endian 32-bit value
    static constexpr uint32_t kMagic = 0x31534D4C;
    static constexpr uint8_t kFormatVersion = 1;
    static constexpr size_t kHeaderSize = 17;
    static constexpr size_t kReservationSize = 3;
    static constexpr size_t kCrcSize = 4;
    /// Largest reservation count (control slot indices 1..254)
    static constexpr size_t kMaxReservations = 254;

    /**
     * @brief Size in bytes of the encoded @p snapshot
     */
    static size_t EncodedSize(const NetworkSnapshot& snapshot);

    /**
     * @brief Encode a snapshot
     *
     * @param snapshot Snapshot to encode
     * @return The blob, or std::nullopt if the snapshot is not valid
     */
    static std::optional<std::vector<uint8_t>> Encode(
        const NetworkSnapshot& snapshot);

    /**
     * @brief Decode a blob produced by Encode()
     *
     * @param data Blob to decode
     * @return The snapshot, or std::nullopt if the blob is rejected
     */
    static std::optional<NetworkSnapshot> Decode(std::span<const uint8_t> data);

    /**
     * @brief Check the content rules every encoded snapshot satisfies
     *
     * The node address is a unicast address; reservations exist only for a
     * network manager and name distinct unicast members other than the node,
     * each with a distinct control slot index in 1..254.
     */
    static bool IsValid(const NetworkSnapshot& snapshot);

    /**
     * @brief CRC-32 (IEEE 802.3, reflected) of @p data
     */
    static uint32_t Crc32(std::span<const uint8_t> data);
};

}  // namespace storage
}  // namespace loramesher
