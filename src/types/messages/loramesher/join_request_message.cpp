/**
 * @file join_request_message.cpp
 * @brief Implementation of join request message functionality
 */

#include "join_request_message.hpp"

namespace loramesher {

JoinRequestMessage::JoinRequestMessage(
    const JoinRequestHeader& header,
    const std::vector<uint8_t>& additional_info)
    : header_(header), additional_info_(additional_info) {}

std::optional<JoinRequestMessage> JoinRequestMessage::Create(
    AddressType dest, AddressType src, uint8_t capabilities,
    uint8_t battery_level, uint8_t requested_slots,
    const std::vector<uint8_t>& additional_info, AddressType next_hop) {

    // Validate battery level
    if (battery_level > 100) {
        LOG_ERROR("Invalid battery level: %d", battery_level);
        return std::nullopt;
    }

    // Create the header with the join request information
    JoinRequestHeader header(dest, src, capabilities, battery_level,
                             requested_slots, next_hop, additional_info.size());

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

uint8_t JoinRequestMessage::GetCapabilities() const {
    return header_.GetCapabilities();
}

uint8_t JoinRequestMessage::GetBatteryLevel() const {
    return header_.GetBatteryLevel();
}

uint8_t JoinRequestMessage::GetRequestedSlots() const {
    return header_.GetRequestedSlots();
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
    std::vector<uint8_t> payload(payload_size);
    utils::ByteSerializer serializer(payload);

    // Serialize only the JOIN_REQUEST specific fields (not the BaseHeader part)
    serializer.WriteUint8(header_.GetCapabilities());
    serializer.WriteUint8(header_.GetBatteryLevel());
    serializer.WriteUint8(header_.GetRequestedSlots());
    serializer.WriteUint16(header_.GetNextHop());

    // Add any additional information
    if (!additional_info_.empty()) {
        serializer.WriteBytes(additional_info_.data(), additional_info_.size());
    }

    // Create the base message with the correct type and our payload
    return BaseMessage(header_.GetDestination(), header_.GetSource(),
                       MessageType::JOIN_REQUEST, payload);
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