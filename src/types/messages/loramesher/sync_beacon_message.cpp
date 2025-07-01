/**
 * @file sync_beacon_message.cpp
 * @brief Implementation of multi-hop synchronization beacon message functionality
 */

#include "sync_beacon_message.hpp"

namespace loramesher {

SyncBeaconMessage::SyncBeaconMessage(const SyncBeaconHeader& header)
    : header_(header) {}

std::optional<SyncBeaconMessage> SyncBeaconMessage::CreateOriginal(
    AddressType dest, AddressType src, uint16_t network_id, uint8_t total_slots,
    uint16_t slot_duration_ms, uint16_t original_timestamp_ms,
    uint8_t max_hops) {

    // Validate parameters
    if (total_slots == 0) {
        LOG_ERROR("Invalid total slots: %d", total_slots);
        return std::nullopt;
    }

    if (slot_duration_ms == 0) {
        LOG_ERROR("Invalid slot duration: %d", slot_duration_ms);
        return std::nullopt;
    }

    // Create the header with optimized sync beacon information
    SyncBeaconHeader header(dest, src, network_id, total_slots,
                            slot_duration_ms);

    // Set timing and forwarding info for original beacon
    Result result =
        header.SetForwardingInfo(0, original_timestamp_ms, 0, max_hops);
    if (!result.IsSuccess()) {
        LOG_ERROR("Failed to set forwarding info: %s",
                  result.GetErrorMessage().c_str());
        return std::nullopt;
    }

    return SyncBeaconMessage(header);
}

std::optional<SyncBeaconMessage> SyncBeaconMessage::CreateForwarded(
    AddressType dest, AddressType src, uint16_t network_id, uint8_t total_slots,
    uint16_t slot_duration_ms, uint8_t hop_count,
    uint16_t original_timestamp_ms, uint32_t propagation_delay_ms,
    uint8_t max_hops) {

    // Validate parameters
    if (hop_count > max_hops) {
        LOG_ERROR("Hop count %d exceeds max hops %d", hop_count, max_hops);
        return std::nullopt;
    }

    // Create the header with optimized forwarding information
    SyncBeaconHeader header(dest, src, network_id, total_slots,
                            slot_duration_ms, hop_count, original_timestamp_ms,
                            propagation_delay_ms, max_hops);

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

// Calculated/derived field getters
uint16_t SyncBeaconMessage::GetSuperframeDuration() const {
    return header_.GetSuperframeDuration();
}

// Multi-hop forwarding field getters (optimized)
uint8_t SyncBeaconMessage::GetHopCount() const {
    return header_.GetHopCount();
}

uint16_t SyncBeaconMessage::GetOriginalTimestamp() const {
    return header_.GetOriginalTimestamp();
}

uint32_t SyncBeaconMessage::GetPropagationDelay() const {
    return header_.GetPropagationDelay();
}

uint8_t SyncBeaconMessage::GetMaxHops() const {
    return header_.GetMaxHops();
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

bool SyncBeaconMessage::ShouldBeForwardedBy(uint8_t node_hop_count) const {
    return header_.ShouldBeForwardedBy(node_hop_count);
}

std::optional<SyncBeaconMessage> SyncBeaconMessage::CreateForwardedBeacon(
    AddressType forwarding_node, uint32_t processing_delay) const {

    // Create forwarded header
    SyncBeaconHeader forwarded_header =
        header_.CreateForwardedBeacon(forwarding_node, processing_delay);

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
    // For sync beacons, all data is in the header, so no additional payload needed
    std::vector<uint8_t> empty_payload;

    // Create the base message with empty payload (all data is in header)
    auto base_message =
        BaseMessage::Create(header_.GetDestination(), header_.GetSource(),
                            MessageType::SYNC_BEACON, empty_payload);

    if (!base_message.has_value()) {
        LOG_ERROR("Failed to create base message from sync beacon message");
        // Return an empty message as fallback
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

}  // namespace loramesher