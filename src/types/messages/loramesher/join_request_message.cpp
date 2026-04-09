/**
 * @file join_request_message.cpp
 * @brief Implementation of join request message functionality
 */

#include "join_request_message.hpp"

#include <array>

#include "utils/compat/span.hpp"

namespace loramesher {

JoinRequestMessage::JoinRequestMessage(
    const JoinRequestHeader& header,
    const std::vector<uint8_t>& additional_info)
    : header_(header), additional_info_(additional_info) {}

std::optional<JoinRequestMessage> JoinRequestMessage::Create(
    AddressType dest, AddressType src, uint8_t battery_level,
    uint8_t requested_slots, const std::vector<uint8_t>& additional_info,
    AddressType next_hop, AddressType sponsor_address, uint8_t hop_count) {

    // Validate battery level
    if (battery_level > 100) {
        LOG_ERROR("Invalid battery level: %d", battery_level);
        return std::nullopt;
    }

    // Create the header with the join request information
    JoinRequestHeader header(dest, src, battery_level, requested_slots,
                             next_hop, additional_info.size(), sponsor_address,
                             hop_count);

    return JoinRequestMessage(header, additional_info);
}

std::optional<JoinRequestMessage> JoinRequestMessage::CreateFromSerialized(
    const std::vector<uint8_t>& data) {

    static const size_t kMinHeaderSize =
        JoinRequestHeader::JoinRequestFieldsSize() + BaseHeader::Size();

    // Check minimum size requirements for header
    if (data.size() < kMinHeaderSize) {
        LOG_ERROR("Data too small for join request message: %zu < %zu",
                  data.size(), kMinHeaderSize);
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(data);

    // Deserialize the header
    auto header = JoinRequestHeader::Deserialize(deserializer);
    if (!header) {
        LOG_ERROR("Failed to deserialize join request header");
        return std::nullopt;
    }

    // Read any additional information that might be present
    std::vector<uint8_t> additional_info;
    if (data.size() > kMinHeaderSize) {
        size_t additional_size = data.size() - kMinHeaderSize;
        additional_info.resize(additional_size);

        // Copy the remaining data as additional information
        std::copy(data.begin() + kMinHeaderSize, data.end(),
                  additional_info.begin());
    }

    return JoinRequestMessage(*header, additional_info);
}

std::optional<JoinRequestMessage> JoinRequestMessage::CreateFromBaseMessage(
    const BaseMessage& message) {
    if (message.GetType() != MessageType::JOIN_REQUEST) {
        LOG_ERROR("Invalid message type for JoinRequestMessage: %d",
                  static_cast<int>(message.GetType()));
        return std::nullopt;
    }

    auto payload = message.GetPayload();
    if (payload.size() < JoinRequestHeader::JoinRequestFieldsSize()) {
        LOG_ERROR("Payload too small for join request fields: %zu < %zu",
                  payload.size(), JoinRequestHeader::JoinRequestFieldsSize());
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(payload);

    auto battery_level = deserializer.ReadUint8();
    auto requested_slots = deserializer.ReadUint8();
    auto next_hop = deserializer.ReadUint16();
    auto sponsor_address = deserializer.ReadUint16();
    auto hop_count = deserializer.ReadUint8();

    if (!battery_level || !requested_slots || !next_hop || !sponsor_address ||
        !hop_count) {
        LOG_ERROR("Failed to read join request payload fields");
        return std::nullopt;
    }

    std::vector<uint8_t> additional_info;
    size_t fields_size = JoinRequestHeader::JoinRequestFieldsSize();
    if (payload.size() > fields_size) {
        additional_info.assign(payload.begin() + fields_size, payload.end());
    }

    JoinRequestHeader header(message.GetDestination(), message.GetSource(),
                             *battery_level, *requested_slots, *next_hop,
                             additional_info.size(), *sponsor_address,
                             *hop_count);

    return JoinRequestMessage(header, additional_info);
}

uint8_t JoinRequestMessage::GetBatteryLevel() const {
    return header_.GetBatteryLevel();
}

uint8_t JoinRequestMessage::GetRequestedSlots() const {
    return header_.GetRequestedSlots();
}

uint8_t JoinRequestMessage::GetHopCount() const {
    return header_.GetHopCount();
}

const std::vector<uint8_t>& JoinRequestMessage::GetAdditionalInfo() const {
    return additional_info_;
}

AddressType JoinRequestMessage::GetSource() const {
    return header_.GetSource();
}

AddressType JoinRequestMessage::GetDestination() const {
    return header_.GetDestination();
}

const JoinRequestHeader& JoinRequestMessage::GetHeader() const {
    return header_;
}

size_t JoinRequestMessage::GetTotalSize() const {
    return header_.GetSize() + additional_info_.size();
}

Result JoinRequestMessage::SetRequestedSlots(uint8_t requested_slots) {
    return header_.SetRequestedSlots(requested_slots);
}

BaseMessage JoinRequestMessage::ToBaseMessage() const {
    // Calculate payload size (only JOIN_REQUEST specific fields + additional info)
    size_t payload_size =
        JoinRequestHeader::JoinRequestFieldsSize() + additional_info_.size();

    std::array<uint8_t, BaseMessage::kMaxPayloadSize> payload_buf{};
    utils::ByteSerializer serializer(payload_buf.data(), payload_size);

    // Serialize only the JOIN_REQUEST specific fields (not the BaseHeader part)
    serializer.WriteUint8(header_.GetBatteryLevel());
    serializer.WriteUint8(header_.GetRequestedSlots());
    serializer.WriteUint16(header_.GetNextHop());
    serializer.WriteUint16(header_.GetSponsorAddress());
    serializer.WriteUint8(header_.GetHopCount());

    // Add any additional information
    if (!additional_info_.empty()) {
        serializer.WriteBytes(additional_info_.data(), additional_info_.size());
    }

    // Create the base message with the correct type and our payload
    return BaseMessage(
        header_.GetDestination(), header_.GetSource(),
        MessageType::JOIN_REQUEST,
        std::span<const uint8_t>(payload_buf.data(), serializer.getOffset()));
}

std::optional<std::vector<uint8_t>> JoinRequestMessage::Serialize() const {
    // Create a buffer for the serialized message
    std::vector<uint8_t> serialized(GetTotalSize());
    utils::ByteSerializer serializer(serialized);

    // Serialize the header
    Result result = header_.Serialize(serializer);
    if (!result.IsSuccess()) {
        LOG_ERROR("Failed to serialize join request header");
        return std::nullopt;
    }

    // Add any additional information
    if (!additional_info_.empty()) {
        serializer.WriteBytes(additional_info_.data(), additional_info_.size());
    }

    return serialized;
}

}  // namespace loramesher