/**
 * @file ping_pong_header.cpp
 * @brief Implementation of PingPong header functionality
 */

#include "ping_pong_header.hpp"

namespace loramesher {

PingPongHeader::PingPongHeader(AddressType dest, AddressType src,
                               PingPongSubtype subtype,
                               uint16_t sequence_number, uint32_t timestamp)
    : BaseHeader(dest, src,
                 static_cast<MessageType>(
                     static_cast<uint8_t>(MessageType::CONTROL_MSG) +
                     static_cast<uint8_t>(subtype)),
                 PingPongHeader::GetSize()),
      subtype_(subtype),
      sequence_number_(sequence_number),
      timestamp_(timestamp) {}

Result PingPongHeader::SetPingPongInfo(PingPongSubtype subtype,
                                       uint16_t sequence_number,
                                       uint32_t timestamp) {

    Result result = IsValidSubtype(subtype);
    if (!result.IsSuccess()) {
        return result;
    }

    subtype_ = subtype;
    sequence_number_ = sequence_number;
    timestamp_ = timestamp;

    return Result::Success();
}

Result PingPongHeader::Serialize(utils::ByteSerializer& serializer) const {
    // First serialize the base header fields
    Result result = BaseHeader::Serialize(serializer);
    if (!result.IsSuccess()) {
        return result;
    }

    // Then serialize PingPong-specific fields
    serializer.WriteUint16(sequence_number_);
    serializer.WriteUint32(timestamp_);

    return Result::Success();
}

std::optional<PingPongHeader> PingPongHeader::Deserialize(
    utils::ByteDeserializer& deserializer) {

    // First deserialize the base header
    auto base_header = BaseHeader::Deserialize(deserializer);
    if (!base_header) {
        LOG_ERROR("Failed to deserialize base header");
        return std::nullopt;
    }

    MessageType type = message_type::GetMainType(base_header->GetType());
    PingPongSubtype sub_type = static_cast<PingPongSubtype>(
        message_type::GetSubtype(base_header->GetType()));

    // Verify that this is a PingPong message
    Result result = IsValidSubtype(sub_type);
    if (!result.IsSuccess()) {
        LOG_ERROR("Invalid PingPong subtype: %d", static_cast<int>(sub_type));
        return std::nullopt;
    }

    // Deserialize PingPong-specific fields
    auto sequence_number = deserializer.ReadUint16();
    auto timestamp = deserializer.ReadUint32();

    if (!sequence_number || !timestamp) {
        LOG_ERROR("Failed to deserialize PingPong header fields");
        return std::nullopt;
    }

    // Create and return the PingPong header
    PingPongHeader header(base_header->GetDestination(),
                          base_header->GetSource(), sub_type, *sequence_number,
                          *timestamp);

    return header;
}

Result PingPongHeader::IsValidSubtype(PingPongSubtype subtype) {
    if (subtype != PingPongSubtype::PING && subtype != PingPongSubtype::PONG) {
        return Result::Error(LoraMesherErrorCode::kInvalidParameter);
    }

    return Result::Success();
}

}  // namespace loramesher