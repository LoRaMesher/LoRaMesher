/**
 * @file group_message.hpp
 * @brief Definition of group (multicast) data message for membership-gated flooding
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
 * @brief Message for delivering data to the members of a logical group
 *
 * Uses the same TTL-based controlled flooding as BroadcastMessage, but carries
 * a group address in the BaseHeader destination instead of the broadcast
 * address. Every node relays the message (subject to TTL); only members of the
 * group deliver it to the application layer. A per-source sequence number
 * enables de-duplication at each node.
 *
 * Wire format (within BaseMessage payload, 5 bytes overhead):
 * - Bytes 0-1: next_hop (always kBroadcastAddress)
 * - Byte 2:    ttl (decremented at each hop)
 * - Byte 3:    flags (bit0 = request_acks)
 * - Byte 4:    seq_num (per-source sequence number for de-duplication)
 * - Bytes 5+:  user data payload
 */
class GroupMessage : public IConvertibleToBaseMessage {
   public:
    /**
     * @brief Size of group-specific fields within the BaseMessage payload
     *
     * next_hop (2) + ttl (1) + flags (1) + seq_num (1) = 5 bytes
     */
    static constexpr size_t kGroupFieldsSize =
        sizeof(AddressType) + sizeof(uint8_t) + sizeof(uint8_t) +
        sizeof(uint8_t);

    /// Flag bit requesting a per-recipient acknowledgement from each member.
    static constexpr uint8_t kFlagRequestAcks = 0x01;

    /**
     * @brief Creates a new group message
     *
     * @param group Destination group address
     * @param src Source address (original sender)
     * @param ttl Time-to-live (decremented at each hop)
     * @param flags Group flags (e.g. request_acks)
     * @param seq_num Per-source sequence number for de-duplication
     * @param payload User data payload
     * @return std::optional<GroupMessage> Valid message if creation succeeded
     */
    static std::optional<GroupMessage> Create(AddressType group,
                                              AddressType src, uint8_t ttl,
                                              uint8_t flags, uint8_t seq_num,
                                              std::span<const uint8_t> payload);

    /**
     * @brief Creates a group message directly from a BaseMessage
     *
     * @param message The base message to convert
     * @return std::optional<GroupMessage> Deserialized message if successful
     */
    static std::optional<GroupMessage> CreateFromBaseMessage(
        const BaseMessage& message);

    /**
     * @brief Creates a forwarded copy with TTL decremented by 1
     *
     * @param original The original group message to forward
     * @return std::optional<GroupMessage> Forwarded message if TTL > 1
     */
    static std::optional<GroupMessage> CreateForwarded(
        const GroupMessage& original);

    AddressType GetSource() const { return source_; }

    AddressType GetDestination() const { return group_; }

    AddressType GetGroup() const { return group_; }

    uint8_t GetTTL() const { return ttl_; }

    uint8_t GetFlags() const { return flags_; }

    bool RequestAcks() const { return (flags_ & kFlagRequestAcks) != 0; }

    uint8_t GetSeqNum() const { return seq_num_; }

    const std::vector<uint8_t>& GetPayload() const { return payload_; }

    /**
     * @brief Gets the total size of the serialized message
     */
    size_t GetTotalSize() const {
        return BaseHeader::Size() + kGroupFieldsSize + payload_.size();
    }

    BaseMessage ToBaseMessage() const override;
    std::optional<std::vector<uint8_t>> Serialize() const override;

   private:
    GroupMessage(AddressType group, AddressType src, uint8_t ttl, uint8_t flags,
                 uint8_t seq_num, const std::vector<uint8_t>& payload);

    AddressType group_;
    AddressType source_;
    uint8_t ttl_;
    uint8_t flags_;
    uint8_t seq_num_;
    std::vector<uint8_t> payload_;
};

}  // namespace loramesher
