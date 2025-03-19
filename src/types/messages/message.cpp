#include "message.hpp"

#include <algorithm>

#include "utils/logger.hpp"

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

Result BaseMessage::setBaseHeader(const BaseHeader& header) {
    base_header_ = header;
    return Result::Success();
}

Result BaseMessage::setBaseHeader(AddressType dest, AddressType src,
                                  MessageType type,
                                  const std::vector<uint8_t>& data) {
    Result result = ValidateInputs(dest, src, type, data);
    if (!result.IsSuccess()) {
        return result;
    }

    base_header_ = {dest, src, type, static_cast<uint8_t>(data.size())};
    payload_ = data;

    return Result::Success();
}

std::optional<BaseMessage> BaseMessage::Create(
    AddressType dest, AddressType src, MessageType type,
    const std::vector<uint8_t>& data) {

    Result validation_result = ValidateInputs(dest, src, type, data);
    if (!validation_result.IsSuccess()) {
        LOG_ERROR("Failed to validate message inputs");
        return std::nullopt;
    }

    return BaseMessage(dest, src, type, data);
}

std::optional<BaseMessage> BaseMessage::CreateFromSerialized(
    const std::vector<uint8_t>& data) {

    if (data.size() < BaseHeader::size()) {
        LOG_ERROR("Invalid message size");
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(data);
    auto header = Deserialize(deserializer);
    if (!header) {
        LOG_ERROR("Failed to deserialize message header");
        return std::nullopt;
    }

    auto payload = deserializer.ReadBytes(header->payloadSize);
    if (!payload) {
        LOG_ERROR("Failed to read message payload");
        return std::nullopt;
    }

    return Create(header->destination, header->source, header->type, *payload);
}

Result BaseMessage::Serialize(utils::ByteSerializer& serializer) const {
    serializer.WriteUint16(base_header_.destination);
    serializer.WriteUint16(base_header_.source);
    serializer.WriteUint8(static_cast<uint8_t>(base_header_.type));
    serializer.WriteUint8(base_header_.payloadSize);

    return Result::Success();
}

std::optional<std::vector<uint8_t>> BaseMessage::Serialize() const {
    std::vector<uint8_t> serialized(getTotalSize());
    utils::ByteSerializer serializer(serialized);

    auto result = Serialize(serializer);
    if (!result) {
        return std::nullopt;
    }

    serializer.WriteBytes(payload_.data(), payload_.size());
    return serialized;
}

std::optional<BaseHeader> BaseMessage::Deserialize(
    utils::ByteDeserializer& deserializer) {

    BaseHeader header;

    auto dest = deserializer.ReadUint16();
    if (!dest) {
        LOG_ERROR("Failed to read destination address");
        return std::nullopt;
    }
    header.destination = *dest;

    auto src = deserializer.ReadUint16();
    if (!src) {
        LOG_ERROR("Failed to read source address");
        return std::nullopt;
    }
    header.source = *src;

    auto type = deserializer.ReadUint8();
    if (!type) {
        LOG_ERROR("Failed to read message type");
        return std::nullopt;
    }

    if (!IsValidMessageType(static_cast<MessageType>(*type))) {
        return std::nullopt;
    }
    header.type = static_cast<MessageType>(*type);

    auto size = deserializer.ReadUint8();
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
        return Result::Error(LoraMesherErrorCode::kBufferOverflow);
    }

    // TODO: Validate addresses (add any specific address validation rules)
    // if (dest == 0 || src == 0) {
    //     return radio::Error(radio::RadioErrorCode::kInvalidParameter);
    // }
    // Prevent unused parameter warnings until implemented
    (void)dest;
    (void)src;

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
        return Result::Error(LoraMesherErrorCode::kInvalidParameter);
    }

    return Result::Success();
}

}  // namespace loramesher