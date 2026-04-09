/**
 * @file broadcast_message.hpp
 * @brief Definition of broadcast data message for mesh-wide flooding
 */

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "types/messages/base_header.hpp"
#include "types/messages/base_message.hpp"
#include "utils/byte_operations.h"
#include "utils/logger.hpp"

namespace loramesher {

/**
 * @brief Message for broadcasting data to all nodes in the mesh network
 *
 * Uses TTL-based controlled flooding: each node that receives the message
 * delivers it to the application layer and re-broadcasts with decremented TTL.
 * A per-source sequence number enables de-duplication at each node.
 *
 * Wire format (within BaseMessage payload, 4 bytes overhead):
 * - Bytes 0-1: next_hop (always kBroadcastAddress)
 * - Byte 2:    ttl (time-to-live, decremented at each hop)
 * - Byte 3:    seq_num (per-source sequence number for de-duplication)
 * - Bytes 4+:  user data payload
 */
class BroadcastMessage : public IConvertibleToBaseMessage {
   public:
    /**
     * @brief Size of broadcast-specific fields within the BaseMessage payload
     *
     * next_hop (2) + ttl (1) + seq_num (1) = 4 bytes
     */
    static constexpr size_t kBroadcastFieldsSize =
        sizeof(AddressType) + sizeof(uint8_t) + sizeof(uint8_t);

    /**
     * @brief Creates a new broadcast message
     *
     * @param src Source address (original sender)
     * @param ttl Time-to-live (decremented at each hop)
     * @param seq_num Per-source sequence number for de-duplication
     * @param payload User data payload
     * @return std::optional<BroadcastMessage> Valid message if creation succeeded
     */
    static std::optional<BroadcastMessage> Create(
        AddressType src, uint8_t ttl, uint8_t seq_num,
        std::span<const uint8_t> payload);

    /**
     * @brief Creates a broadcast message from serialized data
     *
     * @param data Serialized message data (full wire format including BaseHeader)
     * @return std::optional<BroadcastMessage> Deserialized message if successful
     */
    static std::optional<BroadcastMessage> CreateFromSerialized(
        const std::vector<uint8_t>& data);

    /**
     * @brief Creates a broadcast message directly from a BaseMessage
     *
     * @param message The base message to convert
     * @return std::optional<BroadcastMessage> Deserialized message if successful
     */
    static std::optional<BroadcastMessage> CreateFromBaseMessage(
        const BaseMessage& message);

    /**
     * @brief Creates a forwarded copy with TTL decremented by 1
     *
     * @param original The original broadcast message to forward
     * @return std::optional<BroadcastMessage> Forwarded message if TTL > 0
     */
    static std::optional<BroadcastMessage> CreateForwarded(
        const BroadcastMessage& original);

    AddressType GetSource() const { return source_; }

    AddressType GetDestination() const { return kBroadcastAddress; }

    uint8_t GetTTL() const { return ttl_; }

    uint8_t GetSeqNum() const { return seq_num_; }

    const std::vector<uint8_t>& GetPayload() const { return payload_; }

    /**
     * @brief Gets the total size of the serialized message
     */
    size_t GetTotalSize() const {
        return BaseHeader::Size() + kBroadcastFieldsSize + payload_.size();
    }

    BaseMessage ToBaseMessage() const override;
    std::optional<std::vector<uint8_t>> Serialize() const override;

   private:
    BroadcastMessage(AddressType src, uint8_t ttl, uint8_t seq_num,
                     const std::vector<uint8_t>& payload);

    AddressType source_;
    uint8_t ttl_;
    uint8_t seq_num_;
    std::vector<uint8_t> payload_;
};

}  // namespace loramesher
