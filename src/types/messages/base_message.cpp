/**
 * @file message.cpp
 * @brief Implementation of base message functionality
 */

#include "base_message.hpp"

#include "utils/logger.hpp"

namespace loramesher {

BaseMessage::BaseMessage(AddressType dest, AddressType src, MessageType type,
                         std::span<const uint8_t> data)
    : header_(dest, src, type, static_cast<uint8_t>(data.size())),
      payload_size_(static_cast<uint8_t>(data.size())) {
    if (payload_size_ > 0) {
        std::memcpy(payload_data_, data.data(), payload_size_);
    }
}

BaseMessage::BaseMessage(const BaseMessage& other)
    : header_(other.header_),
      payload_size_(other.payload_size_),
      security_context_(other.security_context_),
      pre_send_callback_(other.pre_send_callback_) {
    if (payload_size_ > 0) {
        std::memcpy(payload_data_, other.payload_data_, payload_size_);
    }
}

BaseMessage& BaseMessage::operator=(const BaseMessage& other) {
    if (this != &other) {
        header_ = other.header_;
        payload_size_ = other.payload_size_;
        security_context_ = other.security_context_;
        if (payload_size_ > 0) {
            std::memcpy(payload_data_, other.payload_data_, payload_size_);
        }
        pre_send_callback_ = other.pre_send_callback_;
    }
    return *this;
}

BaseMessage::BaseMessage(BaseMessage&& other) noexcept
    : header_(other.header_),
      payload_size_(other.payload_size_),
      security_context_(other.security_context_),
      pre_send_callback_(std::move(other.pre_send_callback_)) {
    if (payload_size_ > 0) {
        std::memcpy(payload_data_, other.payload_data_, payload_size_);
    }
    other.payload_size_ = 0;
}

BaseMessage& BaseMessage::operator=(BaseMessage&& other) noexcept {
    if (this != &other) {
        header_ = other.header_;
        payload_size_ = other.payload_size_;
        security_context_ = other.security_context_;
        if (payload_size_ > 0) {
            std::memcpy(payload_data_, other.payload_data_, payload_size_);
        }
        pre_send_callback_ = std::move(other.pre_send_callback_);
        other.payload_size_ = 0;
    }
    return *this;
}

Result BaseMessage::SetHeader(const BaseHeader& header) {
    header_ = header;
    return Result::Success();
}

Result BaseMessage::SetMessage(AddressType dest, AddressType src,
                               MessageType type,
                               std::span<const uint8_t> data) {
    Result result = ValidateInputs(dest, src, type, data);
    if (!result.IsSuccess()) {
        return result;
    }

    header_.SetHeader(dest, src, type, static_cast<uint8_t>(data.size()));
    payload_size_ = static_cast<uint8_t>(data.size());
    if (payload_size_ > 0) {
        std::memcpy(payload_data_, data.data(), payload_size_);
    }
    // New content was never sealed, so no received security fields apply
    security_context_ = security::SecurityContext{};

    return Result::Success();
}

std::optional<BaseMessage> BaseMessage::Create(AddressType dest,
                                               AddressType src,
                                               MessageType type,
                                               std::span<const uint8_t> data) {

    Result validation_result = ValidateInputs(dest, src, type, data);
    if (!validation_result.IsSuccess()) {
        LOG_ERROR("Failed to validate message inputs");
        return std::nullopt;
    }

    return BaseMessage(dest, src, type, data);
}

std::optional<BaseMessage> BaseMessage::CreateFromSerialized(
    std::span<const uint8_t> data) {

    if (data.size() < BaseHeader::Size()) {
        LOG_ERROR("Invalid message size");
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(data);
    auto header = BaseHeader::Deserialize(deserializer);
    if (!header) {
        LOG_ERROR("Failed to deserialize message header");
        return std::nullopt;
    }

    auto payload = deserializer.ReadBytesAsSpan(header->GetPayloadSize());
    if (!payload) {
        LOG_ERROR("Failed to read message payload");
        return std::nullopt;
    }

    return Create(header->GetDestination(), header->GetSource(),
                  header->GetType(), *payload);
}

std::optional<std::vector<uint8_t>> BaseMessage::Serialize() const {
    std::vector<uint8_t> serialized(GetTotalSize());
    utils::ByteSerializer serializer(serialized);

    // Serialize the header
    Result result = header_.Serialize(serializer);
    if (!result.IsSuccess()) {
        LOG_ERROR("Failed to serialize header: %s",
                  result.GetErrorMessage().c_str());
        return std::nullopt;
    }

    // Serialize the payload
    serializer.WriteBytes(payload_data_, payload_size_);
    return serialized;
}

Result BaseMessage::ValidateInputs(AddressType dest, AddressType src,
                                   MessageType type,
                                   std::span<const uint8_t> data) {

    // Check payload size
    if (data.size() > kMaxPayloadSize) {
        LOG_ERROR("Payload size exceeds maximum allowed size");
        return Result::Error(LoraMesherErrorCode::kBufferOverflow);
    }

    if (dest == 0 || src == 0) {
        // TODO: Validate addresses (add any specific address validation rules)
        //     return Result::Error(LoraMesherErrorCode::kInvalidParameter);
    }

    // Validate message type
    return BaseHeader::IsValidMessageType(type);
}

void BaseMessage::SetPreSendCallback(
    std::function<void(BaseMessage&)> callback) {
    pre_send_callback_ = std::move(callback);
}

bool BaseMessage::InvokePreSendCallback() {
    if (pre_send_callback_) {
        pre_send_callback_(*this);
        return true;
    }
    return false;
}

}  // namespace loramesher
