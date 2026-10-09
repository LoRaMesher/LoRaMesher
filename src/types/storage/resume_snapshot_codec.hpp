/**
 * @file resume_snapshot_codec.hpp
 * @brief Binary encoding of a ResumeSnapshot
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "types/storage/resume_snapshot.hpp"
#include "utils/compat/span.hpp"

namespace loramesher {
namespace storage {

/**
 * @brief Encodes and decodes ResumeSnapshot blobs
 *
 * Little-endian layout:
 * | Size    | Field                                                  |
 * |---------|--------------------------------------------------------|
 * | 4       | Magic "LMR1"                                           |
 * | 1       | Format version                                         |
 * | 1       | Length L of the embedded NetworkSnapshot blob          |
 * | L       | NetworkSnapshot blob (SnapshotCodec format)            |
 * | 30      | Timing: tick (4), persistent time (8), wake deadline   |
 * |         | (4), superframe start (4), superframes completed (4),  |
 * |         | slot duration (4), total slots (2)                     |
 * | 26      | Member: manager (2), slots per superframe (1), beacon  |
 * |         | node count (1), control slot (1), data slots (1),      |
 * |         | table version (1), last sync (4), last sync beacon (4),|
 * |         | last route cleanup (4), sleep clock error (4, signed   |
 * |         | ppm), sleep clock samples (1), schedule offset at the  |
 * |         | last beacon (2, signed ms)                             |
 * | 2       | Group stream: started (1), next sequence (1)           |
 * | 2       | Slot count N                                           |
 * | 5 x N   | Slots: number (2), type (1), target (2)                |
 * | 4       | Control slots (1), discovery slots (2), flags (1,      |
 * |         | bit 0: slot table rebuild pending)                     |
 * | 1       | Route count R                                          |
 * | R x var | Routes, see below                                      |
 * | 1       | Sequence stream count S                                |
 * | 3 x S   | Streams: destination (2), next sequence (1)            |
 * | 1       | Delivery stream count D                                |
 * | 12 x D  | Delivery streams: source (2), group (1), highest (1),  |
 * |         | bitmap (4), last send timestamp (4)                    |
 * | 1       | Seen message count M                                   |
 * | 3 x M   | Seen messages: source (2), sequence (1)                |
 * | 4       | CRC-32 of all previous bytes                           |
 *
 * Each route is a 22-byte record: destination (2), next hop (2), hop count,
 * link quality, data slots, capabilities, advertised control slot, reception
 * quality (1 each), advertised next hop (2), local control slot (1), flags
 * (1), last seen (4), last updated (4). Flag bits 0-1 hold the network
 * manager and active flags. Bit 2 appends the link statistics (33 bytes) and
 * bit 3 the path round-trip time (8 bytes); both are omitted while they hold
 * their initial values.
 */
class ResumeSnapshotCodec {
   public:
    /// Magic "LMR1" read as a little-endian 32-bit value
    static constexpr uint32_t kMagic = 0x31524D4C;
    static constexpr uint8_t kFormatVersion = 2;
    static constexpr size_t kRouteSize = 22;
    static constexpr size_t kLinkStatsSize = 33;
    static constexpr size_t kPathRttSize = 8;
    static constexpr size_t kSequenceStreamSize = 3;
    static constexpr size_t kMaxRoutes = 255;
    static constexpr size_t kMaxSequenceStreams = 254;
    static constexpr size_t kDeliveryStreamSize = 12;
    static constexpr size_t kMaxDeliveryStreams = 32;
    static constexpr size_t kSeenMessageSize = 3;
    static constexpr size_t kMaxSeenMessages = 32;
    static constexpr size_t kSlotSize = 5;
    static constexpr size_t kMaxSlots = 256;

    /**
     * @brief Size in bytes of the encoded @p snapshot
     */
    static size_t EncodedSize(const ResumeSnapshot& snapshot);

    /**
     * @brief Encode a snapshot
     *
     * @param snapshot Snapshot to encode
     * @return The blob, or std::nullopt if the snapshot is not valid
     */
    static std::optional<std::vector<uint8_t>> Encode(
        const ResumeSnapshot& snapshot);

    /**
     * @brief Decode a blob produced by Encode()
     *
     * @param data Blob to decode
     * @return The snapshot, or std::nullopt if the blob is rejected
     */
    static std::optional<ResumeSnapshot> Decode(std::span<const uint8_t> data);

    /**
     * @brief True if @p data starts like a blob produced by Encode()
     */
    static bool HasMagic(std::span<const uint8_t> data);

    /**
     * @brief Check the content rules every encoded snapshot satisfies
     *
     * The embedded NetworkSnapshot is valid and belongs to a member; the
     * network manager is another unicast node; the superframe has slots of
     * non-zero duration matching the snapshot's superframe duration; routes,
     * sequence streams and delivery streams name distinct unicast nodes
     * (delivery streams: per kind) and fit their counts; every delivery
     * stream has delivered its highest sequence; seen messages come from
     * unicast nodes and fit their count; the slot table fits kMaxSlots and
     * holds known slot types.
     */
    static bool IsValid(const ResumeSnapshot& snapshot);
};

}  // namespace storage
}  // namespace loramesher
