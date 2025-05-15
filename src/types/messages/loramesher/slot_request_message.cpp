/**
 * @file slot_request_message.cpp
 * @brief Implementation of slot request message functionality
 */

#include "slot_request_message.hpp"

namespace loramesher {

SlotRequestMessage::SlotRequestMessage(AddressType dest, AddressType src,
                                       uint8_t requested_slots)
    : destination_(dest), source_(src), requested_slots_(requested_slots) {}

std::optional<SlotRequestMessage> SlotRequestMessage::Create(
    AddressType dest, AddressType src, uint8_t requested_slots) {

    return SlotRequestMessage(dest, src, requested_slots);
}

std::optional<SlotRequestMessage> SlotRequestMessage::CreateFromSerialized(
    const std::vector<uint8_t>& data) {

    // Check minimum size requirement (just 1 byte for requested slots)
    if (data.size() < 1) {
        LOG_ERROR("Data too small for slot request message");
        return std::nullopt;
    }

    // The BaseMessage already has source and destination in its header
    // So we just need to extract the requested_slots field from the payload
    uint8_t requested_slots = data[0];

    // We'll need the source and destination addresses from the BaseMessage
    // This will be handled by the calling code

    // Return a placeholder - actual source and destination will be set by caller
    return SlotRequestMessage(0, 0, requested_slots);
}

size_t SlotRequestMessage::GetTotalSize() const {
    // Just the payload size (1 byte for requested slots)
    return 1;
}

BaseMessage SlotRequestMessage::ToBaseMessage() const {
    // Create payload with just the requested slots field
    std::vector<uint8_t> payload = {requested_slots_};

    // Create the base message
    auto base_message = BaseMessage::Create(destination_, source_,
                                            MessageType::SLOT_REQUEST, payload);

    if (!base_message.has_value()) {
        LOG_ERROR("Failed to create base message from slot request message");
        // Return an empty message as fallback
        return BaseMessage(destination_, source_, MessageType::SLOT_REQUEST,
                           {});
    }

    return base_message.value();
}

std::optional<std::vector<uint8_t>> SlotRequestMessage::Serialize() const {
    // Simply return the payload
    return std::vector<uint8_t>{requested_slots_};
}

}  // namespace loramesher