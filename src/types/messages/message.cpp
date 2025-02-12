#include "message.hpp"

namespace loramesher {

BaseMessage::BaseMessage(AddressType dest, AddressType src, MessageType type,
                         const std::vector<uint8_t>& data) {
    validateInputs(dest, src, type, data);
    baseHeader_ = {dest, src, type, static_cast<uint8_t>(data.size())};
    payload_ = data;
}

BaseMessage::BaseMessage(const std::vector<uint8_t>& data) {
    if (data.size() < BaseHeader::size()) {
        throw std::out_of_range(
            "Data vector is too short to contain a valid message");
    }

    utils::ByteDeserializer deserializer(data);
    baseHeader_ = deserialize(deserializer);
    payload_ = deserializer.readBytes(baseHeader_.payloadSize);
}

BaseMessage::BaseMessage(const BaseMessage& other)
    : baseHeader_(other.baseHeader_), payload_(other.payload_) {}

BaseMessage& BaseMessage::operator=(const BaseMessage& other) {
    if (this != &other) {
        baseHeader_ = other.baseHeader_;
        payload_ = other.payload_;
    }
    return *this;
}

BaseMessage::BaseMessage(BaseMessage&& other) noexcept
    : baseHeader_(other.baseHeader_), payload_(std::move(other.payload_)) {}

BaseMessage& BaseMessage::operator=(BaseMessage&& other) noexcept {
    if (this != &other) {
        baseHeader_ = other.baseHeader_;
        payload_ = std::move(other.payload_);
    }
    return *this;
}

void BaseMessage::serialize(utils::ByteSerializer& serializer) const {
    serializer.writeUint16(baseHeader_.destination);
    serializer.writeUint16(baseHeader_.source);
    serializer.writeUint8(static_cast<uint8_t>(baseHeader_.type));
    serializer.writeUint8(baseHeader_.payloadSize);
}

std::vector<uint8_t> BaseMessage::serialize() const {
    std::vector<uint8_t> serialized(getTotalSize());
    utils::ByteSerializer serializer(serialized);

    serialize(serializer);
    serializer.writeBytes(payload_.data(), payload_.size());

    return serialized;
}

BaseHeader BaseMessage::deserialize(utils::ByteDeserializer& deserializer) {
    BaseHeader header;
    header.destination = deserializer.readUint16();
    header.source = deserializer.readUint16();
    header.type = static_cast<MessageType>(deserializer.readUint8());
    header.payloadSize = deserializer.readUint8();
    return header;
}

std::unique_ptr<BaseMessage> BaseMessage::deserialize(
    const std::vector<uint8_t>& data) {
    utils::ByteDeserializer deserializer(data);
    BaseHeader header = deserialize(deserializer);
    std::vector<uint8_t> payload = deserializer.readBytes(header.payloadSize);

    return std::make_unique<BaseMessage>(header.destination, header.source,
                                         header.type, payload);
}

void BaseMessage::validateInputs(AddressType dest, AddressType src,
                                 MessageType type,
                                 const std::vector<uint8_t>& data) const {
    // Check payload size
    if (data.size() > MAX_PAYLOAD_SIZE) {
        throw std::length_error("Payload size exceeds maximum allowed size");
    }

    // TODO: Validate addresses (add any specific address validation rules)
    // if (dest == 0 || src == 0) {
    //     throw std::invalid_argument("Invalid address: addresses cannot be 0");
    // }

    // Validate message type (optional, depending on your requirements)
    switch (type) {
        case MessageType::DATA:
        case MessageType::XL_DATA:
        case MessageType::HELLO:
        case MessageType::ACK:
        case MessageType::LOST:
        case MessageType::SYNC:
        case MessageType::NEED_ACK:
            break;
        default:
            throw std::invalid_argument("Invalid message type");
    }
}

}  // namespace loramesher