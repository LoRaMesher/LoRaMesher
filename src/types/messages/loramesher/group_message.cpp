/**
 * @file group_message.cpp
 * @brief Implementation of group message functionality
 */

#include "group_message.hpp"

#include <array>

#include "utils/compat/span.hpp"

namespace loramesher {

GroupMessage::GroupMessage(AddressType group, AddressType src, uint8_t ttl,
                           uint8_t flags, uint8_t seq_num,
                           const std::vector<uint8_t>& payload)
    : group_(group),
      source_(src),
      ttl_(ttl),
      flags_(flags),
      seq_num_(seq_num),
      payload_(payload) {}

std::optional<GroupMessage> GroupMessage::Create(
    AddressType group, AddressType src, uint8_t ttl, uint8_t flags,
    uint8_t seq_num, std::span<const uint8_t> payload) {

    if (!IsGroupAddress(group)) {
        LOG_ERROR("Group message destination 0x%04X is not a group address",
                  group);
        return std::nullopt;
    }

    if (payload.size() > BaseMessage::kMaxPayloadSize - kGroupFieldsSize) {
        LOG_ERROR("Group payload too large: %zu bytes (max %zu)",
                  payload.size(),
                  BaseMessage::kMaxPayloadSize - kGroupFieldsSize);
        return std::nullopt;
    }

    std::vector<uint8_t> payload_vec(payload.begin(), payload.end());
    return GroupMessage(group, src, ttl, flags, seq_num, payload_vec);
}

std::optional<GroupMessage> GroupMessage::CreateFromBaseMessage(
    const BaseMessage& message) {
    if (message.GetType() != MessageType::DATA_GROUP) {
        LOG_ERROR("Invalid message type for GroupMessage: %d",
                  static_cast<int>(message.GetType()));
        return std::nullopt;
    }

    auto payload = message.GetPayload();
    if (payload.size() < kGroupFieldsSize) {
        LOG_ERROR("Payload too small for group fields: %zu < %zu",
                  payload.size(), kGroupFieldsSize);
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(payload);

    auto next_hop = deserializer.ReadUint16();  // consumed but not stored
    auto ttl = deserializer.ReadUint8();
    auto flags = deserializer.ReadUint8();
    auto seq_num = deserializer.ReadUint8();

    if (!next_hop || !ttl || !flags || !seq_num) {
        LOG_ERROR("Failed to read group payload fields");
        return std::nullopt;
    }

    std::vector<uint8_t> user_payload;
    if (payload.size() > kGroupFieldsSize) {
        user_payload.assign(payload.begin() + kGroupFieldsSize, payload.end());
    }

    return GroupMessage(message.GetDestination(), message.GetSource(), *ttl,
                        *flags, *seq_num, user_payload);
}

std::optional<GroupMessage> GroupMessage::CreateForwarded(
    const GroupMessage& original) {

    if (original.ttl_ <= 1) {
        return std::nullopt;
    }

    return GroupMessage(original.group_, original.source_, original.ttl_ - 1,
                        original.flags_, original.seq_num_, original.payload_);
}

BaseMessage GroupMessage::ToBaseMessage() const {
    size_t total_payload_size = kGroupFieldsSize + payload_.size();

    std::array<uint8_t, BaseMessage::kMaxPayloadSize> payload_buf{};
    utils::ByteSerializer serializer(payload_buf.data(), total_payload_size);

    serializer.WriteUint16(kBroadcastAddress);  // next_hop
    serializer.WriteUint8(ttl_);
    serializer.WriteUint8(flags_);
    serializer.WriteUint8(seq_num_);

    if (!payload_.empty()) {
        serializer.WriteBytes(payload_.data(), payload_.size());
    }

    return BaseMessage(
        group_, source_, MessageType::DATA_GROUP,
        std::span<const uint8_t>(payload_buf.data(), serializer.getOffset()));
}

std::optional<std::vector<uint8_t>> GroupMessage::Serialize() const {
    std::vector<uint8_t> serialized(GetTotalSize());
    utils::ByteSerializer serializer(serialized);

    // BaseHeader fields
    serializer.WriteUint16(group_);  // dest
    serializer.WriteUint16(source_);
    serializer.WriteUint8(static_cast<uint8_t>(MessageType::DATA_GROUP));
    serializer.WriteUint8(
        static_cast<uint8_t>(kGroupFieldsSize + payload_.size()));

    // Group-specific fields
    serializer.WriteUint16(kBroadcastAddress);  // next_hop
    serializer.WriteUint8(ttl_);
    serializer.WriteUint8(flags_);
    serializer.WriteUint8(seq_num_);

    // User payload
    if (!payload_.empty()) {
        serializer.WriteBytes(payload_.data(), payload_.size());
    }

    return serialized;
}

}  // namespace loramesher
