/**
 * @file slot_allocation_message.cpp
 * @brief Implementation of slot allocation message functionality
 */

#include "slot_allocation_message.hpp"

namespace loramesher {

SlotAllocationMessage::SlotAllocationMessage(AddressType dest, AddressType src,
                                             uint16_t network_id,
                                             uint8_t allocated_slots,
                                             uint8_t total_nodes)
    : destination_(dest),
      source_(src),
      network_id_(network_id),
      allocated_slots_(allocated_slots),
      total_nodes_(total_nodes) {}

std::optional<SlotAllocationMessage> SlotAllocationMessage::Create(
    AddressType dest, AddressType src, uint16_t network_id,
    uint8_t allocated_slots, uint8_t total_nodes) {

    return SlotAllocationMessage(dest, src, network_id, allocated_slots,
                                 total_nodes);
}

std::optional<SlotAllocationMessage>
SlotAllocationMessage::CreateFromSerialized(const std::vector<uint8_t>& data) {

    static constexpr size_t kMinHeaderSize =
        4;  // 2 bytes for network_id + 1 byte for allocated_slots + 1 byte for total_nodes

    // Check minimum size requirement (network_id + allocated_slots + total_nodes = 4 bytes)
    if (data.size() < kMinHeaderSize) {
        LOG_ERROR("Data too small for slot allocation message");
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(data);

    // Extract fields
    auto network_id = deserializer.ReadUint16();
    auto allocated_slots = deserializer.ReadUint8();
    auto total_nodes = deserializer.ReadUint8();

    if (!network_id || !allocated_slots || !total_nodes) {
        LOG_ERROR("Failed to read slot allocation fields");
        return std::nullopt;
    }

    // Return a placeholder with actual values - source/dest will be set by caller
    return SlotAllocationMessage(0, 0, *network_id, *allocated_slots,
                                 *total_nodes);
}

size_t SlotAllocationMessage::GetTotalSize() const {
    // 2 bytes for network_id + 1 byte for allocated_slots + 1 byte for total_nodes
    return 4;
}

BaseMessage SlotAllocationMessage::ToBaseMessage() const {
    // Create payload
    std::vector<uint8_t> payload(4);
    utils::ByteSerializer serializer(payload);

    // Serialize fields
    serializer.WriteUint16(network_id_);
    serializer.WriteUint8(allocated_slots_);
    serializer.WriteUint8(total_nodes_);

    // Create the base message
    auto base_message = BaseMessage::Create(
        destination_, source_, MessageType::SLOT_ALLOCATION, payload);

    if (!base_message.has_value()) {
        LOG_ERROR("Failed to create base message from slot allocation message");
        // Return an empty message as fallback
        return BaseMessage(destination_, source_, MessageType::SLOT_ALLOCATION,
                           {});
    }

    return base_message.value();
}

std::optional<std::vector<uint8_t>> SlotAllocationMessage::Serialize() const {
    // Create payload buffer
    std::vector<uint8_t> serialized(GetTotalSize());
    utils::ByteSerializer serializer(serialized);

    // Serialize fields
    serializer.WriteUint16(network_id_);
    serializer.WriteUint8(allocated_slots_);
    serializer.WriteUint8(total_nodes_);

    return serialized;
}

}  // namespace loramesher