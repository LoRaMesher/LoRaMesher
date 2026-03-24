/**
 * @file data_header.cpp
 * @brief Implementation of data header functionality
 */

#include "data_header.hpp"

namespace loramesher {

DataHeader::DataHeader(AddressType dest, AddressType src, AddressType next_hop,
                       uint8_t payload_size, uint8_t ttl, uint8_t seq_num)
    : BaseHeader(dest, src, MessageType::DATA,
                 DataHeader::DataFieldsSize() + payload_size),
      next_hop_(next_hop),
      ttl_(ttl),
      seq_num_(seq_num) {}

Result DataHeader::SetNextHop(AddressType next_hop) {
    next_hop_ = next_hop;
    return Result::Success();
}

Result DataHeader::Serialize(utils::ByteSerializer& serializer) const {
    // First serialize the base header fields
    Result result = BaseHeader::Serialize(serializer);
    if (!result.IsSuccess()) {
        return result;
    }

    // Then serialize data header specific fields
    serializer.WriteUint16(next_hop_);
    serializer.WriteUint8(ttl_);
    serializer.WriteUint8(seq_num_);

    return Result::Success();
}

std::optional<DataHeader> DataHeader::Deserialize(
    utils::ByteDeserializer& deserializer) {

    // First deserialize the base header
    auto base_header = BaseHeader::Deserialize(deserializer);
    if (!base_header) {
        LOG_ERROR("Failed to deserialize base header");
        return std::nullopt;
    }

    // Verify this is a data message
    if (base_header->GetType() != MessageType::DATA) {
        LOG_ERROR("Wrong message type for data header: %d",
                  static_cast<int>(base_header->GetType()));
        return std::nullopt;
    }

    // Deserialize data header specific fields
    auto next_hop = deserializer.ReadUint16();
    if (!next_hop) {
        LOG_ERROR("Failed to deserialize data header next_hop");
        return std::nullopt;
    }

    auto ttl = deserializer.ReadUint8();
    if (!ttl) {
        LOG_ERROR("Failed to deserialize data header TTL");
        return std::nullopt;
    }

    auto seq_num = deserializer.ReadUint8();
    if (!seq_num) {
        LOG_ERROR("Failed to deserialize data header seq_num");
        return std::nullopt;
    }

    // Calculate actual payload size (total payload size - data fields size)
    uint8_t actual_payload_size =
        base_header->GetPayloadSize() > DataFieldsSize()
            ? base_header->GetPayloadSize() - DataFieldsSize()
            : 0;

    // Create and return the data header
    DataHeader header(base_header->GetDestination(), base_header->GetSource(),
                      *next_hop, actual_payload_size, *ttl, *seq_num);

    return header;
}

}  // namespace loramesher
