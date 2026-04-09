/**
 * @file data_message.cpp
 * @brief Implementation of data message functionality
 */

#include "data_message.hpp"

#include <array>

#include "utils/compat/span.hpp"

namespace loramesher {

DataMessage::DataMessage(const DataHeader& header,
                         const std::vector<uint8_t>& payload)
    : header_(header), payload_(payload) {}

std::optional<DataMessage> DataMessage::Create(
    AddressType dest, AddressType src, AddressType next_hop,
    const std::vector<uint8_t>& payload, uint8_t ttl, uint8_t seq_num) {

    // Validate payload size
    if (payload.size() >
        BaseMessage::kMaxPayloadSize - DataHeader::DataFieldsSize()) {
        LOG_ERROR("Payload too large: %zu bytes (max %zu)", payload.size(),
                  BaseMessage::kMaxPayloadSize - DataHeader::DataFieldsSize());
        return std::nullopt;
    }

    // Create the header with the data information
    DataHeader header(dest, src, next_hop, payload.size(), ttl, seq_num);

    return DataMessage(header, payload);
}

std::optional<DataMessage> DataMessage::CreateForwarded(
    const DataMessage& original, AddressType new_next_hop) {

    if (original.GetTTL() <= 1) {
        return std::nullopt;
    }

    return DataMessage::Create(original.GetDestination(), original.GetSource(),
                               new_next_hop, original.GetPayload(),
                               original.GetTTL() - 1, original.GetSeqNum());
}

std::optional<DataMessage> DataMessage::CreateFromSerialized(
    const std::vector<uint8_t>& data) {

    static const size_t kMinHeaderSize =
        DataHeader::DataFieldsSize() + BaseHeader::Size();

    // Check minimum size requirements for header
    if (data.size() < kMinHeaderSize) {
        LOG_ERROR("Data too small for data message: %zu < %zu", data.size(),
                  kMinHeaderSize);
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(data);

    // Deserialize the header
    auto header = DataHeader::Deserialize(deserializer);
    if (!header) {
        LOG_ERROR("Failed to deserialize data header");
        return std::nullopt;
    }

    // Read any payload that might be present
    std::vector<uint8_t> payload;
    if (data.size() > kMinHeaderSize) {
        size_t payload_size = data.size() - kMinHeaderSize;
        payload.resize(payload_size);

        // Copy the remaining data as payload
        std::copy(data.begin() + kMinHeaderSize, data.end(), payload.begin());
    }

    return DataMessage(*header, payload);
}

std::optional<DataMessage> DataMessage::CreateFromBaseMessage(
    const BaseMessage& message) {
    if (message.GetType() != MessageType::DATA) {
        LOG_ERROR("Invalid message type for DataMessage: %d",
                  static_cast<int>(message.GetType()));
        return std::nullopt;
    }

    auto payload = message.GetPayload();
    if (payload.size() < DataHeader::DataFieldsSize()) {
        LOG_ERROR("Payload too small for data fields: %zu < %zu",
                  payload.size(), DataHeader::DataFieldsSize());
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(payload);

    auto next_hop = deserializer.ReadUint16();
    auto ttl = deserializer.ReadUint8();
    auto seq_num = deserializer.ReadUint8();

    if (!next_hop || !ttl || !seq_num) {
        LOG_ERROR("Failed to read data message payload fields");
        return std::nullopt;
    }

    std::vector<uint8_t> user_payload;
    size_t fields_size = DataHeader::DataFieldsSize();
    if (payload.size() > fields_size) {
        user_payload.assign(payload.begin() + fields_size, payload.end());
    }

    DataHeader header(message.GetDestination(), message.GetSource(), *next_hop,
                      user_payload.size(), *ttl, *seq_num);

    return DataMessage(header, user_payload);
}

AddressType DataMessage::GetDestination() const {
    return header_.GetDestination();
}

AddressType DataMessage::GetSource() const {
    return header_.GetSource();
}

AddressType DataMessage::GetNextHop() const {
    return header_.GetNextHop();
}

uint8_t DataMessage::GetTTL() const {
    return header_.GetTTL();
}

uint8_t DataMessage::GetSeqNum() const {
    return header_.GetSeqNum();
}

const std::vector<uint8_t>& DataMessage::GetPayload() const {
    return payload_;
}

const DataHeader& DataMessage::GetHeader() const {
    return header_;
}

DataHeader& DataMessage::GetMutableHeader() {
    return header_;
}

size_t DataMessage::GetTotalSize() const {
    return header_.GetSize() + payload_.size();
}

Result DataMessage::SetNextHop(AddressType next_hop) {
    return header_.SetNextHop(next_hop);
}

BaseMessage DataMessage::ToBaseMessage() const {
    // Calculate payload size (only DATA specific fields + user payload)
    size_t total_payload_size = DataHeader::DataFieldsSize() + payload_.size();

    std::array<uint8_t, BaseMessage::kMaxPayloadSize> payload_buf{};
    utils::ByteSerializer serializer(payload_buf.data(), total_payload_size);

    // Serialize only the DATA specific fields (not the BaseHeader part)
    serializer.WriteUint16(header_.GetNextHop());
    serializer.WriteUint8(header_.GetTTL());
    serializer.WriteUint8(header_.GetSeqNum());

    // Add user payload
    if (!payload_.empty()) {
        serializer.WriteBytes(payload_.data(), payload_.size());
    }

    // Create the base message with the correct type and our payload
    return BaseMessage(
        header_.GetDestination(), header_.GetSource(), MessageType::DATA,
        std::span<const uint8_t>(payload_buf.data(), serializer.getOffset()));
}

std::optional<std::vector<uint8_t>> DataMessage::Serialize() const {
    // Create a buffer for the serialized message
    std::vector<uint8_t> serialized(GetTotalSize());
    utils::ByteSerializer serializer(serialized);

    // Serialize the header
    Result result = header_.Serialize(serializer);
    if (!result.IsSuccess()) {
        LOG_ERROR("Failed to serialize data header");
        return std::nullopt;
    }

    // Add user payload
    if (!payload_.empty()) {
        serializer.WriteBytes(payload_.data(), payload_.size());
    }

    return serialized;
}

}  // namespace loramesher
