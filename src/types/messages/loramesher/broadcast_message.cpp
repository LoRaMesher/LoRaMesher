/**
 * @file broadcast_message.cpp
 * @brief Implementation of broadcast message functionality
 */

#include "broadcast_message.hpp"

#include <array>

#include "utils/compat/span.hpp"

namespace loramesher {

BroadcastMessage::BroadcastMessage(AddressType src, uint8_t ttl,
                                   uint8_t seq_num,
                                   const std::vector<uint8_t>& payload)
    : source_(src), ttl_(ttl), seq_num_(seq_num), payload_(payload) {}

std::optional<BroadcastMessage> BroadcastMessage::Create(
    AddressType src, uint8_t ttl, uint8_t seq_num,
    std::span<const uint8_t> payload) {

    if (payload.size() > BaseMessage::kMaxPayloadSize - kBroadcastFieldsSize) {
        LOG_ERROR("Broadcast payload too large: %zu bytes (max %zu)",
                  payload.size(),
                  BaseMessage::kMaxPayloadSize - kBroadcastFieldsSize);
        return std::nullopt;
    }

    std::vector<uint8_t> payload_vec(payload.begin(), payload.end());
    return BroadcastMessage(src, ttl, seq_num, payload_vec);
}

std::optional<BroadcastMessage> BroadcastMessage::CreateFromSerialized(
    const std::vector<uint8_t>& data) {

    static const size_t kMinSize = BaseHeader::Size() + kBroadcastFieldsSize;

    if (data.size() < kMinSize) {
        LOG_ERROR("Data too small for broadcast message: %zu < %zu",
                  data.size(), kMinSize);
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(data);

    // Deserialize base header
    auto base_header = BaseHeader::Deserialize(deserializer);
    if (!base_header) {
        LOG_ERROR("Failed to deserialize base header for broadcast");
        return std::nullopt;
    }

    if (base_header->GetType() != MessageType::DATA_BROADCAST) {
        LOG_ERROR("Wrong message type for broadcast: 0x%02X",
                  static_cast<int>(base_header->GetType()));
        return std::nullopt;
    }

    // Read next_hop (always kBroadcastAddress, consumed but not stored)
    auto next_hop = deserializer.ReadUint16();
    if (!next_hop) {
        LOG_ERROR("Failed to read broadcast next_hop");
        return std::nullopt;
    }

    // Read TTL
    auto ttl = deserializer.ReadUint8();
    if (!ttl) {
        LOG_ERROR("Failed to read broadcast TTL");
        return std::nullopt;
    }

    // Read sequence number
    auto seq_num = deserializer.ReadUint8();
    if (!seq_num) {
        LOG_ERROR("Failed to read broadcast seq_num");
        return std::nullopt;
    }

    // Read remaining bytes as user payload
    std::vector<uint8_t> payload;
    if (data.size() > kMinSize) {
        size_t payload_size = data.size() - kMinSize;
        payload.resize(payload_size);
        std::copy(data.begin() + kMinSize, data.end(), payload.begin());
    }

    return BroadcastMessage(base_header->GetSource(), *ttl, *seq_num, payload);
}

std::optional<BroadcastMessage> BroadcastMessage::CreateForwarded(
    const BroadcastMessage& original) {

    if (original.ttl_ <= 1) {
        return std::nullopt;
    }

    return BroadcastMessage(original.source_, original.ttl_ - 1,
                            original.seq_num_, original.payload_);
}

BaseMessage BroadcastMessage::ToBaseMessage() const {
    size_t total_payload_size = kBroadcastFieldsSize + payload_.size();

    std::array<uint8_t, BaseMessage::kMaxPayloadSize> payload_buf{};
    utils::ByteSerializer serializer(payload_buf.data(), total_payload_size);

    // next_hop = broadcast address
    serializer.WriteUint16(kBroadcastAddress);
    // TTL
    serializer.WriteUint8(ttl_);
    // Sequence number
    serializer.WriteUint8(seq_num_);

    // User payload
    if (!payload_.empty()) {
        serializer.WriteBytes(payload_.data(), payload_.size());
    }

    return BaseMessage(
        kBroadcastAddress, source_, MessageType::DATA_BROADCAST,
        std::span<const uint8_t>(payload_buf.data(), serializer.getOffset()));
}

std::optional<std::vector<uint8_t>> BroadcastMessage::Serialize() const {
    std::vector<uint8_t> serialized(GetTotalSize());
    utils::ByteSerializer serializer(serialized);

    // BaseHeader fields
    serializer.WriteUint16(kBroadcastAddress);  // dest
    serializer.WriteUint16(source_);
    serializer.WriteUint8(static_cast<uint8_t>(MessageType::DATA_BROADCAST));
    serializer.WriteUint8(
        static_cast<uint8_t>(kBroadcastFieldsSize + payload_.size()));

    // Broadcast-specific fields
    serializer.WriteUint16(kBroadcastAddress);  // next_hop
    serializer.WriteUint8(ttl_);
    serializer.WriteUint8(seq_num_);

    // User payload
    if (!payload_.empty()) {
        serializer.WriteBytes(payload_.data(), payload_.size());
    }

    return serialized;
}

}  // namespace loramesher
