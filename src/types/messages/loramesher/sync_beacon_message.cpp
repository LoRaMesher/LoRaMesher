/**
 * @file sync_beacon_message.cpp
 * @brief Implementation of multi-hop synchronization beacon message functionality
 */

#include "sync_beacon_message.hpp"

#include <array>

#include "utils/compat/span.hpp"

namespace loramesher {

SyncBeaconMessage::SyncBeaconMessage(const SyncBeaconHeader& header)
    : header_(header) {}

std::optional<SyncBeaconMessage> SyncBeaconMessage::CreateOriginal(
    AddressType dest, AddressType src, uint16_t network_id, uint8_t total_slots,
    uint16_t slot_duration_ms, AddressType network_manager,
    uint32_t guard_time_ms, uint8_t max_hops, uint8_t node_count) {

    // Validate parameters
    if (total_slots == 0) {
        LOG_ERROR("Invalid total slots: %d", total_slots);
        return std::nullopt;
    }

    if (slot_duration_ms == 0) {
        LOG_ERROR("Invalid slot duration: %d", slot_duration_ms);
        return std::nullopt;
    }

    // Create the header with all fields including node_count
    // Use guard_time_ms as the propagation delay for original beacon
    SyncBeaconHeader header(dest, src, network_id, total_slots,
                            slot_duration_ms, network_manager, 0, guard_time_ms,
                            max_hops, node_count);

    return SyncBeaconMessage(header);
}

std::optional<SyncBeaconMessage> SyncBeaconMessage::CreateForwarded(
    AddressType dest, AddressType src, uint16_t network_id, uint8_t total_slots,
    uint16_t slot_duration_ms, AddressType network_manager, uint8_t hop_count,
    uint32_t propagation_delay_ms, uint32_t guard_time_ms, uint8_t max_hops) {

    // Validate parameters
    if (hop_count > max_hops) {
        LOG_ERROR("Hop count %d exceeds max hops %d", hop_count, max_hops);
        return std::nullopt;
    }

    // Create the header with optimized forwarding information
    // Add guard_time_ms to the propagation delay
    SyncBeaconHeader header(dest, src, network_id, total_slots,
                            slot_duration_ms, network_manager, hop_count,
                            propagation_delay_ms + guard_time_ms, max_hops);

    return SyncBeaconMessage(header);
}

std::optional<SyncBeaconMessage> SyncBeaconMessage::CreateFromSerialized(
    const std::vector<uint8_t>& data) {

    static const size_t kMinHeaderSize =
        SyncBeaconHeader::SyncBeaconFieldsSize() + BaseHeader::Size();

    // Check minimum size requirements for header
    if (data.size() < kMinHeaderSize) {
        LOG_ERROR("Data too small for sync beacon message: %zu < %zu",
                  data.size(), kMinHeaderSize);
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(data);

    // Deserialize the header
    auto header = SyncBeaconHeader::Deserialize(deserializer);
    if (!header) {
        LOG_ERROR("Failed to deserialize sync beacon header");
        return std::nullopt;
    }

    return SyncBeaconMessage(*header);
}

std::optional<SyncBeaconMessage> SyncBeaconMessage::CreateFromBaseMessage(
    const BaseMessage& msg) {
    auto payload = msg.GetPayload();

    if (payload.size() < SyncBeaconHeader::SyncBeaconFieldsSize()) {
        LOG_ERROR("Payload too small for sync beacon fields: %zu < %zu",
                  payload.size(), SyncBeaconHeader::SyncBeaconFieldsSize());
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(payload);

    auto network_id = deserializer.ReadUint16();
    auto total_slots = deserializer.ReadUint8();
    auto slot_duration_ms = deserializer.ReadUint16();
    auto network_manager = deserializer.ReadUint16();
    auto hop_count = deserializer.ReadUint8();
    auto propagation_delay_ms = deserializer.ReadUint32();
    auto max_hops = deserializer.ReadUint8();
    auto node_count = deserializer.ReadUint8();

    if (!network_id || !total_slots || !slot_duration_ms || !network_manager ||
        !hop_count || !propagation_delay_ms || !max_hops || !node_count) {
        LOG_ERROR("Failed to read sync beacon fields from payload");
        return std::nullopt;
    }

    SyncBeaconHeader header(msg.GetDestination(), msg.GetSource(), *network_id,
                            *total_slots, *slot_duration_ms, *network_manager,
                            *hop_count, *propagation_delay_ms, *max_hops,
                            *node_count);

    return SyncBeaconMessage(header);
}

// Core synchronization field getters (optimized)
uint16_t SyncBeaconMessage::GetNetworkId() const {
    return header_.GetNetworkId();
}

uint8_t SyncBeaconMessage::GetTotalSlots() const {
    return header_.GetTotalSlots();
}

uint16_t SyncBeaconMessage::GetSlotDuration() const {
    return header_.GetSlotDuration();
}

AddressType SyncBeaconMessage::GetNetworkManager() const {
    return header_.GetNetworkManager();
}

// Calculated/derived field getters
uint16_t SyncBeaconMessage::GetSuperframeDuration() const {
    return header_.GetSuperframeDuration();
}

// Multi-hop forwarding field getters (optimized)
uint8_t SyncBeaconMessage::GetHopCount() const {
    return header_.GetHopCount();
}

uint32_t SyncBeaconMessage::GetPropagationDelay() const {
    return header_.GetPropagationDelay();
}

void SyncBeaconMessage::UpdatePropagationDelay(uint32_t propagation_delay_ms) {
    header_.SetPropagationDelay(propagation_delay_ms);
}

uint8_t SyncBeaconMessage::GetMaxHops() const {
    return header_.GetMaxHops();
}

uint8_t SyncBeaconMessage::GetNodeCount() const {
    return header_.GetNodeCount();
}

AddressType SyncBeaconMessage::GetSource() const {
    return header_.GetSource();
}

AddressType SyncBeaconMessage::GetDestination() const {
    return header_.GetDestination();
}

const SyncBeaconHeader& SyncBeaconMessage::GetHeader() const {
    return header_;
}

size_t SyncBeaconMessage::GetTotalSize() const {
    return header_.GetSize();  // No additional payload for sync beacons
}

std::optional<SyncBeaconMessage> SyncBeaconMessage::CreateForwardedBeacon(
    AddressType forwarding_node, uint32_t processing_delay,
    uint32_t guard_time_ms) const {

    // Create forwarded header
    SyncBeaconHeader forwarded_header = header_.CreateForwardedBeacon(
        forwarding_node, processing_delay, guard_time_ms);

    return SyncBeaconMessage(forwarded_header);
}

uint32_t SyncBeaconMessage::CalculateOriginalTiming(
    uint32_t reception_time) const {
    // Calculate when the Network Manager originally sent this beacon
    return reception_time - header_.GetPropagationDelay();
}

bool SyncBeaconMessage::IsOriginalBeacon() const {
    return header_.GetHopCount() == 0;
}

BaseMessage SyncBeaconMessage::ToBaseMessage() const {
    // Calculate total message size for sync beacon
    size_t total_message_size =
        header_.SyncBeaconFieldsSize() + BaseHeader::Size();

    std::array<uint8_t, 256> payload_buf{};
    utils::ByteSerializer serializer(payload_buf.data(), total_message_size);

    // Serialize the sync beacon header-specific fields
    Result result = header_.Serialize(serializer);
    if (!result.IsSuccess()) {
        LOG_ERROR("Failed to serialize sync beacon header");
        return BaseMessage(header_.GetDestination(), header_.GetSource(),
                           MessageType::SYNC_BEACON, {});
    }

    // Create the base message from the serialized bytes (includes full header)
    auto base_message = BaseMessage::CreateFromSerialized(
        std::span<const uint8_t>(payload_buf.data(), serializer.getOffset()));
    if (!base_message.has_value()) {
        LOG_ERROR("Failed to create base message from sync beacon message");
        return BaseMessage(header_.GetDestination(), header_.GetSource(),
                           MessageType::SYNC_BEACON, {});
    }

    return base_message.value();
}

std::optional<std::vector<uint8_t>> SyncBeaconMessage::Serialize() const {
    // Create a buffer for the serialized message
    std::vector<uint8_t> serialized(GetTotalSize());
    utils::ByteSerializer serializer(serialized);

    // Serialize the header (contains all sync beacon data)
    Result result = header_.Serialize(serializer);
    if (!result.IsSuccess()) {
        LOG_ERROR("Failed to serialize sync beacon header");
        return std::nullopt;
    }

    return serialized;
}

void SyncBeaconMessage::Print() const {
    LOG_DEBUG("SyncBeaconMessage:");
    LOG_DEBUG("  Source: {0x%04X}", GetSource());
    LOG_DEBUG("  Destination: {0x%04X}", GetDestination());
    LOG_DEBUG("  Network ID: {0x%04X}", GetNetworkId());
    LOG_DEBUG("  Total Slots: {%d}", GetTotalSlots());
    LOG_DEBUG("  Slot Duration: {%d}", GetSlotDuration());
    LOG_DEBUG("  Network Manager: {0x%04X}", GetNetworkManager());
    LOG_DEBUG("  Hop Count: {%d}", GetHopCount());
    LOG_DEBUG("  Propagation Delay: {%u}", GetPropagationDelay());
    LOG_DEBUG("  Max Hops: {%d}", GetMaxHops());
    LOG_DEBUG("  Node Count: {%d}", GetNodeCount());
}

}  // namespace loramesher