/**
 * @file ping_pong_message.cpp
 * @brief Implementation of PingPong message functionality
 */

#include "ping_pong_message.hpp"

namespace loramesher {

PingPongMessage::PingPongMessage(const PingPongHeader& header)
    : header_(header) {}

std::optional<PingPongMessage> PingPongMessage::Create(AddressType dest,
                                                       AddressType src,
                                                       PingPongSubtype subtype,
                                                       uint16_t sequence_number,
                                                       uint32_t timestamp) {

    // Create a header with all fields set
    PingPongHeader header(dest, src, subtype, sequence_number, timestamp);

    // Validate the subtype using PingPongHeader's validation
    Result result = PingPongHeader::IsValidSubtype(subtype);
    if (!result.IsSuccess()) {
        LOG_ERROR("Invalid PingPong subtype");
        return std::nullopt;
    }

    return PingPongMessage(header);
}

std::optional<PingPongMessage> PingPongMessage::CreateFromSerialized(
    const std::vector<uint8_t>& data) {

    // Check minimum size requirements
    if (data.size() <
        PingPongHeader::PingPongFieldsSize() + BaseHeader::Size()) {
        LOG_ERROR("Data too small for PingPong message: %zu < %zu", data.size(),
                  PingPongHeader::PingPongFieldsSize());
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(data);

    // Deserialize the PingPong header
    auto header = PingPongHeader::Deserialize(deserializer);
    if (!header) {
        LOG_ERROR("Failed to deserialize PingPong header");
        return std::nullopt;
    }

    return PingPongMessage(*header);
}

Result PingPongMessage::SetInfo(PingPongSubtype subtype,
                                uint16_t sequence_number, uint32_t timestamp) {
    return header_.SetPingPongInfo(subtype, sequence_number, timestamp);
}

PingPongSubtype PingPongMessage::GetSubtype() const {
    return header_.GetSubtype();
}

uint16_t PingPongMessage::GetSequenceNumber() const {
    return header_.GetSequenceNumber();
}

uint32_t PingPongMessage::GetTimestamp() const {
    return header_.GetTimestamp();
}

AddressType PingPongMessage::GetSource() const {
    return header_.GetSource();
}

AddressType PingPongMessage::GetDestination() const {
    return header_.GetDestination();
}

const PingPongHeader& PingPongMessage::GetHeader() const {
    return header_;
}

uint32_t PingPongMessage::CalculateRTT(uint32_t reference_timestamp) const {
    return header_.GetTimestamp() - reference_timestamp;
}

size_t PingPongMessage::GetTotalSize() const {
    return header_.GetSize();
}

BaseMessage PingPongMessage::ToBaseMessage() const {
    // Create a serialized representation of PingPong-specific fields
    std::vector<uint8_t> payload;
    payload.resize(PingPongHeader::PingPongFieldsSize());

    utils::ByteSerializer serializer(payload);
    serializer.WriteUint16(header_.GetSequenceNumber());
    serializer.WriteUint32(header_.GetTimestamp());

    MessageType ping_pong_type = MessageType::PING;
    if (header_.GetSubtype() == PingPongSubtype::PONG) {
        ping_pong_type = MessageType::PONG;
    }

    // Create the base message with our serialized content as payload
    auto base_message = BaseMessage::Create(
        header_.GetDestination(), header_.GetSource(), ping_pong_type, payload);

    if (!base_message.has_value()) {
        LOG_ERROR("Failed to create base message from PingPong message");
        // TODO: In production code, we might want a better error handling strategy here
        return BaseMessage(header_.GetDestination(), header_.GetSource(),
                           ping_pong_type, {});
    }

    return base_message.value();
}

std::optional<std::vector<uint8_t>> PingPongMessage::Serialize() const {
    BaseMessage base_message = ToBaseMessage();
    auto serialized = base_message.Serialize();
    if (!serialized) {
        LOG_ERROR("Failed to serialize PingPong message");
        return std::nullopt;
    }

    return serialized;
}

}  // namespace loramesher