/**
 * @file ack_payload.hpp
 * @brief Acknowledgement payload carried inside an ACK data-family message.
 *
 * An ACK is transmitted as a DataMessage of type MessageType::ACK so it reuses
 * the unicast next-hop forwarding path back to the original source. This struct
 * encodes the ACK-specific payload that rides inside that message.
 *
 * Wire format (6 bytes, within the DataMessage payload):
 * - Byte 0:    acked_seq (sequence number being acknowledged)
 * - Byte 1:    flags (bit0 = was_group)
 * - Bytes 2-5: echo_timestamp (send timestamp echoed for RTT, little-endian)
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "utils/byte_operations.h"
#include "utils/compat/span.hpp"

namespace loramesher {

/**
 * @brief Acknowledgement payload encoder / decoder.
 */
struct AckPayload {
    /// Size of the serialized acknowledgement payload in bytes.
    static constexpr size_t kSize =
        sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint32_t);

    /// Flag bit indicating the acknowledged message was a group send.
    static constexpr uint8_t kFlagWasGroup = 0x01;

    uint8_t acked_seq = 0;        ///< Sequence number being acknowledged
    uint8_t flags = 0;            ///< Acknowledgement flags
    uint32_t echo_timestamp = 0;  ///< Echoed send timestamp for RTT

    /// Whether this acknowledgement is for a group send.
    bool WasGroup() const { return (flags & kFlagWasGroup) != 0; }

    /**
     * @brief Serialize the payload into a fixed-size byte array.
     *
     * @return std::array<uint8_t, kSize> Serialized bytes
     */
    std::array<uint8_t, kSize> Serialize() const {
        std::array<uint8_t, kSize> buffer{};
        utils::ByteSerializer serializer(buffer.data(), buffer.size());
        serializer.WriteUint8(acked_seq);
        serializer.WriteUint8(flags);
        serializer.WriteUint32(echo_timestamp);
        return buffer;
    }

    /**
     * @brief Deserialize a payload from raw bytes.
     *
     * @param data Bytes carrying the acknowledgement payload
     * @return std::optional<AckPayload> Parsed payload, or nullopt if too short
     */
    static std::optional<AckPayload> Deserialize(
        std::span<const uint8_t> data) {
        if (data.size() < kSize) {
            return std::nullopt;
        }
        utils::ByteDeserializer deserializer(data);
        auto acked_seq = deserializer.ReadUint8();
        auto flags = deserializer.ReadUint8();
        auto echo_timestamp = deserializer.ReadUint32();
        if (!acked_seq || !flags || !echo_timestamp) {
            return std::nullopt;
        }
        return AckPayload{*acked_seq, *flags, *echo_timestamp};
    }
};

}  // namespace loramesher
