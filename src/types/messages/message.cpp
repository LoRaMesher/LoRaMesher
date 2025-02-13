#include "message.hpp"

#include <algorithm>

namespace loramesher {

BaseMessage::BaseMessage(AddressType dest, AddressType src, MessageType type,
                         const std::vector<uint8_t>& data)
    : base_header_({dest, src, type, static_cast<uint8_t>(data.size())}),
      payload_(data) {}

BaseMessage::BaseMessage(const BaseMessage& other)
    : base_header_(other.base_header_), payload_(other.payload_) {}

BaseMessage& BaseMessage::operator=(const BaseMessage& other) {
    if (this != &other) {
        base_header_ = other.base_header_;
        payload_ = other.payload_;
    }
    return *this;
}

BaseMessage::BaseMessage(BaseMessage&& other) noexcept
    : base_header_(other.base_header_), payload_(std::move(other.payload_)) {}

BaseMessage& BaseMessage::operator=(BaseMessage&& other) noexcept {
    if (this != &other) {
        base_header_ = other.base_header_;
        payload_ = std::move(other.payload_);
    }
    return *this;
}

Result BaseMessage::SetBaseHeader(const BaseHeader& header) {
    base_header_ = header;
    return Result::success();
}

Result BaseMessage::SetBaseHeader(AddressType dest, AddressType src,
                                  MessageType type,
                                  const std::vector<uint8_t>& data) {
    Result result = ValidateInputs(dest, src, type, data);
    if (!result.isSuccess()) {
        return result;
    }

    base_header_ = {dest, src, type, static_cast<uint8_t>(data.size())};
    payload_ = data;

    return Result::success();
}

std::optional<BaseMessage> BaseMessage::Create(
    AddressType dest, AddressType src, MessageType type,
    const std::vector<uint8_t>& data) {

    auto validation_result = ValidateInputs(dest, src, type, data);
    if (!validation_result.isSuccess()) {
        return std::nullopt;
    }

    return BaseMessage(dest, src, type, data);
}

std::optional<BaseMessage> BaseMessage::CreateFromSerialized(
    const std::vector<uint8_t>& data) {

    if (data.size() < BaseHeader::size()) {
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(data);
    auto header = Deserialize(deserializer);
    if (!header) {
        return std::nullopt;
    }

    auto payload = deserializer.readBytes(header->payloadSize);
    if (!payload) {
        return std::nullopt;
    }

    return Create(header->destination, header->source, header->type, *payload);
}

Result BaseMessage::Serialize(utils::ByteSerializer& serializer) const {
    serializer.writeUint16(base_header_.destination);
    serializer.writeUint16(base_header_.source);
    serializer.writeUint8(static_cast<uint8_t>(base_header_.type));
    serializer.writeUint8(base_header_.payloadSize);

    return Result::success();
}

std::optional<std::vector<uint8_t>> BaseMessage::Serialize() const {
    std::vector<uint8_t> serialized(GetTotalSize());
    utils::ByteSerializer serializer(serialized);

    auto result = Serialize(serializer);
    if (!result) {
        return std::nullopt;
    }

    serializer.writeBytes(payload_.data(), payload_.size());
    return serialized;
}

std::optional<BaseHeader> BaseMessage::Deserialize(
    utils::ByteDeserializer& deserializer) {

    BaseHeader header;

    auto dest = deserializer.readUint16();
    if (!dest) {
        return std::nullopt;
    }
    header.destination = *dest;

    auto src = deserializer.readUint16();
    if (!src) {
        return std::nullopt;
    }
    header.source = *src;

    auto type = deserializer.readUint8();
    if (!type) {
        return std::nullopt;
    }

    if (!IsValidMessageType(static_cast<MessageType>(*type))) {
        return std::nullopt;
    }
    header.type = static_cast<MessageType>(*type);

    auto size = deserializer.readUint8();
    if (!size) {
        return std::nullopt;
    }
    header.payloadSize = *size;

    return header;
}

Result BaseMessage::ValidateInputs(AddressType dest, AddressType src,
                                   MessageType type,
                                   const std::vector<uint8_t>& data) {

    // Check payload size
    if (data.size() > MAX_PAYLOAD_SIZE) {
        return Result::error(LoraMesherErrorCode::kBufferOverflow);
    }

    // TODO: Validate addresses (add any specific address validation rules)
    // if (dest == 0 || src == 0) {
    //     return radio::Error(radio::RadioErrorCode::kInvalidParameter);
    // }

    // Validate message type
    return IsValidMessageType(type);
}

Result BaseMessage::IsValidMessageType(MessageType type) {
    static constexpr MessageType kValidTypes[] = {
        MessageType::DATA,     MessageType::XL_DATA,    MessageType::HELLO,
        MessageType::ACK,      MessageType::LOST,       MessageType::SYNC,
        MessageType::NEED_ACK, MessageType::ROUTING_MSG};

    // Check if the type exists in the valid types array
    const bool is_valid =
        std::find(std::begin(kValidTypes), std::end(kValidTypes), type) !=
        std::end(kValidTypes);

    if (!is_valid) {
        return Result::error(LoraMesherErrorCode::kInvalidParameter);
    }

    return Result::success();
}

}  // namespace loramesher