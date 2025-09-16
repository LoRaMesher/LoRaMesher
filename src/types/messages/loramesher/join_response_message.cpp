/**
 * @file join_response_message.cpp
 * @brief Implementation of join response message functionality
 */

#include "join_response_message.hpp"

namespace loramesher {

JoinResponseMessage::JoinResponseMessage(
    const JoinResponseHeader& header,
    const std::vector<uint8_t>& superframe_info)
    : header_(header), superframe_info_(superframe_info) {}

std::optional<JoinResponseMessage> JoinResponseMessage::Create(
    AddressType dest, AddressType src, uint16_t network_id,
    uint8_t allocated_slots, JoinResponseHeader::ResponseStatus status,
    const std::vector<uint8_t>& superframe_info, AddressType next_hop,
    AddressType target_address) {

    // Create the header with the join response information
    JoinResponseHeader header(dest, src, network_id, allocated_slots, status,
                              next_hop, superframe_info.size(), target_address);

    return JoinResponseMessage(header, superframe_info);
}

std::optional<JoinResponseMessage> JoinResponseMessage::CreateFromSerialized(
    const std::vector<uint8_t>& data) {

    static const size_t kMinHeaderSize =
        JoinResponseHeader::JoinResponseFieldsSize() + BaseHeader::Size();

    // Check minimum size requirements for header
    if (data.size() < kMinHeaderSize) {
        LOG_ERROR("Data too small for join response message: %zu < %zu",
                  data.size(), kMinHeaderSize);
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(data);

    // Deserialize the header
    auto header = JoinResponseHeader::Deserialize(deserializer);
    if (!header) {
        LOG_ERROR("Failed to deserialize join response header");
        return std::nullopt;
    }

    // Read any superframe information that might be present
    std::vector<uint8_t> superframe_info;
    if (data.size() > kMinHeaderSize) {
        size_t info_size = data.size() - kMinHeaderSize;
        superframe_info.resize(info_size);

        // Copy the remaining data as superframe information
        std::copy(data.begin() + kMinHeaderSize, data.end(),
                  superframe_info.begin());
    }

    return JoinResponseMessage(*header, superframe_info);
}

uint16_t JoinResponseMessage::GetNetworkId() const {
    return header_.GetNetworkId();
}

uint8_t JoinResponseMessage::GetAllocatedSlots() const {
    return header_.GetAllocatedSlots();
}

JoinResponseHeader::ResponseStatus JoinResponseMessage::GetStatus() const {
    return header_.GetStatus();
}

const std::vector<uint8_t>& JoinResponseMessage::GetSuperframeInfo() const {
    return superframe_info_;
}

AddressType JoinResponseMessage::GetSource() const {
    return header_.GetSource();
}

AddressType JoinResponseMessage::GetDestination() const {
    return header_.GetDestination();
}

const JoinResponseHeader& JoinResponseMessage::GetHeader() const {
    return header_;
}

size_t JoinResponseMessage::GetTotalSize() const {
    return header_.GetSize() + superframe_info_.size();
}

BaseMessage JoinResponseMessage::ToBaseMessage() const {
    // Calculate payload size
    size_t payload_size = GetTotalSize();

    std::vector<uint8_t> payload(payload_size);
    utils::ByteSerializer serializer(payload);

    Result result = header_.Serialize(serializer);
    if (!result.IsSuccess()) {
        LOG_ERROR("Failed to serialize join response header");
        return BaseMessage(header_.GetDestination(), header_.GetSource(),
                           MessageType::JOIN_RESPONSE, {});
    }

    // Add any superframe information
    if (!superframe_info_.empty()) {
        serializer.WriteBytes(superframe_info_.data(), superframe_info_.size());
    }

    // Create the base message with our serialized content as payload
    auto base_message = BaseMessage::CreateFromSerialized(payload);
    if (!base_message.has_value()) {
        LOG_ERROR("Failed to create base message from join response");
        // Return an empty message as fallback
        return BaseMessage(header_.GetDestination(), header_.GetSource(),
                           MessageType::SYNC_BEACON, {});
    }

    return base_message.value();
}

std::optional<std::vector<uint8_t>> JoinResponseMessage::Serialize() const {
    BaseMessage base_message = ToBaseMessage();
    auto serialized = base_message.Serialize();
    if (!serialized) {
        LOG_ERROR("Failed to serialize PingPong message");
        return std::nullopt;
    }

    return serialized;
}

}  // namespace loramesher