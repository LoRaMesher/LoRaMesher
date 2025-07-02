/**
 * @file join_request_header.cpp
 * @brief Implementation of join request header functionality
 */

#include "join_request_header.hpp"

namespace loramesher {

JoinRequestHeader::JoinRequestHeader(AddressType dest, AddressType src,
                                     uint8_t capabilities,
                                     uint8_t battery_level,
                                     uint8_t requested_slots,
                                     AddressType next_hop,
                                     uint8_t additional_info_size)
    : BaseHeader(
          dest, src, MessageType::JOIN_REQUEST,
          JoinRequestHeader::JoinRequestFieldsSize() + additional_info_size),
      capabilities_(capabilities),
      battery_level_(battery_level),
      requested_slots_(requested_slots),
      next_hop_(next_hop) {}

Result JoinRequestHeader::SetJoinRequestInfo(uint8_t capabilities,
                                             uint8_t battery_level,
                                             uint8_t requested_slots) {
    // Validate battery level
    if (battery_level > 100) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Battery level must be between 0-100%");
    }

    capabilities_ = capabilities;
    battery_level_ = battery_level;
    requested_slots_ = requested_slots;

    return Result::Success();
}

Result JoinRequestHeader::Serialize(utils::ByteSerializer& serializer) const {
    // First serialize the base header fields
    Result result = BaseHeader::Serialize(serializer);
    if (!result.IsSuccess()) {
        return result;
    }

    // Then serialize join request specific fields
    serializer.WriteUint8(capabilities_);
    serializer.WriteUint8(battery_level_);
    serializer.WriteUint8(requested_slots_);
    serializer.WriteUint16(next_hop_);

    return Result::Success();
}

std::optional<JoinRequestHeader> JoinRequestHeader::Deserialize(
    utils::ByteDeserializer& deserializer) {

    // First deserialize the base header
    auto base_header = BaseHeader::Deserialize(deserializer);
    if (!base_header) {
        LOG_ERROR("Failed to deserialize base header");
        return std::nullopt;
    }

    // Verify this is a join request message
    if (base_header->GetType() != MessageType::JOIN_REQUEST) {
        LOG_ERROR("Wrong message type for join request header: %d",
                  static_cast<int>(base_header->GetType()));
        return std::nullopt;
    }

    // Deserialize join request specific fields
    auto capabilities = deserializer.ReadUint8();
    auto battery_level = deserializer.ReadUint8();
    auto requested_slots = deserializer.ReadUint8();
    auto next_hop = deserializer.ReadUint16();

    if (!capabilities || !battery_level || !requested_slots || !next_hop) {
        LOG_ERROR("Failed to deserialize join request header fields");
        return std::nullopt;
    }

    // Create and return the join request header
    JoinRequestHeader header(base_header->GetDestination(),
                             base_header->GetSource(), *capabilities,
                             *battery_level, *requested_slots, *next_hop);

    return header;
}

}  // namespace loramesher