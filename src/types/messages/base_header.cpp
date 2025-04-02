/**
 * @file base_header.cpp
 * @brief Implementation of common header functionality
 */

#include "base_header.hpp"

#include <algorithm>

namespace loramesher {

BaseHeader::BaseHeader(AddressType dest, AddressType src, MessageType type,
                       uint8_t payload_size)
    : destination_(dest),
      source_(src),
      type_(type),
      payload_size_(payload_size) {}

Result BaseHeader::SetHeader(AddressType dest, AddressType src,
                             MessageType type, uint8_t payload_size) {
    Result result = IsValidMessageType(type);
    if (!result.IsSuccess()) {
        return result;
    }

    // TODO: Add address validation if needed
    // if (dest == 0 || src == 0) {
    //     return Result::Error(LoraMesherErrorCode::kInvalidParameter);
    // }

    destination_ = dest;
    source_ = src;
    type_ = type;
    payload_size_ = payload_size;

    return Result::Success();
}

Result BaseHeader::Serialize(utils::ByteSerializer& serializer) const {
    serializer.WriteUint16(destination_);
    serializer.WriteUint16(source_);
    serializer.WriteUint8(static_cast<uint8_t>(type_));
    serializer.WriteUint8(payload_size_);

    return Result::Success();
}

std::vector<uint8_t> BaseHeader::Serialize() const {
    std::vector<uint8_t> buffer(Size());
    utils::ByteSerializer serializer(buffer);

    Serialize(serializer);
    return buffer;
}

std::optional<BaseHeader> BaseHeader::Deserialize(
    utils::ByteDeserializer& deserializer) {

    auto dest = deserializer.ReadUint16();
    if (!dest) {
        LOG_ERROR("Failed to read destination address");
        return std::nullopt;
    }

    auto src = deserializer.ReadUint16();
    if (!src) {
        LOG_ERROR("Failed to read source address");
        return std::nullopt;
    }

    auto type_raw = deserializer.ReadUint8();
    if (!type_raw) {
        LOG_ERROR("Failed to read message type");
        return std::nullopt;
    }

    auto type = static_cast<MessageType>(*type_raw);
    Result result = IsValidMessageType(type);
    if (!result.IsSuccess()) {
        LOG_ERROR("Invalid message type: %d", *type_raw);
        return std::nullopt;
    }

    auto payload_size = deserializer.ReadUint8();
    if (!payload_size) {
        LOG_ERROR("Failed to read payload size");
        return std::nullopt;
    }

    return BaseHeader(*dest, *src, type, *payload_size);
}

Result BaseHeader::IsValidMessageType(MessageType type) {
    if (!message_type::IsValidType(type)) {
        return Result(LoraMesherErrorCode::kInvalidArgument,
                      "Invalid Message Type");
    }

    return Result::Success();
}

}  // namespace loramesher