/**
 * @file join_response_header.cpp
 * @brief Implementation of join response header functionality
 */

#include "join_response_header.hpp"

namespace loramesher {

JoinResponseHeader::JoinResponseHeader(
    AddressType dest, AddressType src, uint16_t network_id,
    uint8_t allocated_slots, ResponseStatus status, AddressType next_hop,
    uint8_t additional_info_size, AddressType target_address)
    : BaseHeader(
          dest, src, MessageType::JOIN_RESPONSE,
          JoinResponseHeader::JoinResponseFieldsSize() + additional_info_size),
      network_id_(network_id),
      allocated_slots_(allocated_slots),
      status_(status),
      next_hop_(next_hop),
      target_address_(target_address) {}

Result JoinResponseHeader::SetJoinResponseInfo(uint16_t network_id,
                                               uint8_t allocated_slots,
                                               ResponseStatus status) {
    network_id_ = network_id;
    allocated_slots_ = allocated_slots;
    status_ = status;

    return Result::Success();
}

Result JoinResponseHeader::Serialize(utils::ByteSerializer& serializer) const {
    // First serialize the base header fields
    Result result = BaseHeader::Serialize(serializer);
    if (!result.IsSuccess()) {
        return result;
    }

    // Then serialize join response specific fields
    serializer.WriteUint16(network_id_);
    serializer.WriteUint8(allocated_slots_);
    serializer.WriteUint8(static_cast<uint8_t>(status_));
    serializer.WriteUint16(next_hop_);
    serializer.WriteUint16(target_address_);

    return Result::Success();
}

std::optional<JoinResponseHeader> JoinResponseHeader::Deserialize(
    utils::ByteDeserializer& deserializer) {

    // First deserialize the base header
    auto base_header = BaseHeader::Deserialize(deserializer);
    if (!base_header) {
        LOG_ERROR("Failed to deserialize base header");
        return std::nullopt;
    }

    // Verify this is a join response message
    if (base_header->GetType() != MessageType::JOIN_RESPONSE) {
        LOG_ERROR("Wrong message type for join response header: %d",
                  static_cast<int>(base_header->GetType()));
        return std::nullopt;
    }

    // Deserialize join response specific fields
    auto network_id = deserializer.ReadUint16();
    auto allocated_slots = deserializer.ReadUint8();
    auto status_raw = deserializer.ReadUint8();
    auto next_hop = deserializer.ReadUint16();
    auto target_address = deserializer.ReadUint16();

    if (!network_id || !allocated_slots || !status_raw || !next_hop ||
        !target_address) {
        LOG_ERROR("Failed to deserialize join response header fields");
        return std::nullopt;
    }

    // Convert raw status to enum
    auto status = static_cast<ResponseStatus>(*status_raw);

    // Create and return the join response header
    JoinResponseHeader header(
        base_header->GetDestination(), base_header->GetSource(), *network_id,
        *allocated_slots, status, *next_hop, 0, *target_address);

    return header;
}

Result JoinResponseHeader::SetTargetAddress(AddressType target_address) {
    target_address_ = target_address;
    return Result::Success();
}

}  // namespace loramesher