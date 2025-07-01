/**
 * @file sync_beacon_header.cpp
 * @brief Implementation of multi-hop synchronization beacon header functionality
 */

#include "sync_beacon_header.hpp"
#include "utils/logger.hpp"

namespace loramesher {

SyncBeaconHeader::SyncBeaconHeader(AddressType dest, AddressType src,
                                   uint16_t network_id, uint8_t total_slots,
                                   uint16_t slot_duration_ms)
    : BaseHeader(dest, src, MessageType::SYNC_BEACON,
                 0),  // Payload size set later
      network_id_(network_id),
      total_slots_(total_slots),
      slot_duration_ms_(slot_duration_ms),
      hop_count_(0),              // Original transmission has hop count 0
      original_timestamp_ms_(0),  // Set when transmitting
      propagation_delay_ms_(0),   // No delay for original transmission
      max_hops_(5) {}             // Default network diameter

SyncBeaconHeader::SyncBeaconHeader(AddressType dest, AddressType src,
                                   uint16_t network_id, uint8_t total_slots,
                                   uint16_t slot_duration_ms, uint8_t hop_count,
                                   uint16_t original_timestamp_ms,
                                   uint32_t propagation_delay_ms,
                                   uint8_t max_hops)
    : BaseHeader(dest, src, MessageType::SYNC_BEACON,
                 0),  // Payload size set later
      network_id_(network_id),
      total_slots_(total_slots),
      slot_duration_ms_(slot_duration_ms),
      hop_count_(hop_count),
      original_timestamp_ms_(original_timestamp_ms),
      propagation_delay_ms_(propagation_delay_ms),
      max_hops_(max_hops) {}

Result SyncBeaconHeader::SetSyncInfo(uint16_t network_id, uint8_t total_slots,
                                     uint16_t slot_duration_ms) {
    // Validate parameters
    if (total_slots == 0) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Total slots must be greater than 0");
    }

    if (slot_duration_ms == 0) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Slot duration must be greater than 0");
    }

    network_id_ = network_id;
    total_slots_ = total_slots;
    slot_duration_ms_ = slot_duration_ms;

    return Result::Success();
}

Result SyncBeaconHeader::SetForwardingInfo(uint8_t hop_count,
                                           uint16_t original_timestamp_ms,
                                           uint32_t propagation_delay_ms,
                                           uint8_t max_hops) {
    // Validate hop count
    if (hop_count > max_hops) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Hop count cannot exceed max hops");
    }

    hop_count_ = hop_count;
    original_timestamp_ms_ = original_timestamp_ms;
    propagation_delay_ms_ = propagation_delay_ms;
    max_hops_ = max_hops;

    return Result::Success();
}

SyncBeaconHeader SyncBeaconHeader::CreateForwardedBeacon(
    AddressType forwarding_node, uint32_t processing_delay) const {
    // Create a new header with optimized fields and forwarding node as source
    SyncBeaconHeader forwarded(
        GetDestination(), forwarding_node, network_id_, total_slots_,
        slot_duration_ms_, hop_count_ + 1, original_timestamp_ms_,
        propagation_delay_ms_ + processing_delay, max_hops_);

    return forwarded;
}

Result SyncBeaconHeader::Serialize(utils::ByteSerializer& serializer) const {
    // First serialize the base header fields
    Result result = BaseHeader::Serialize(serializer);
    if (!result.IsSuccess()) {
        return result;
    }

    // Serialize optimized sync beacon specific fields
    serializer.WriteUint16(network_id_);
    serializer.WriteUint8(total_slots_);
    serializer.WriteUint16(slot_duration_ms_);  // Optimized from uint32_t
    serializer.WriteUint8(hop_count_);
    serializer.WriteUint16(original_timestamp_ms_);  // Optimized from uint32_t
    serializer.WriteUint32(propagation_delay_ms_);
    serializer.WriteUint8(max_hops_);

    return Result::Success();
}

std::optional<SyncBeaconHeader> SyncBeaconHeader::Deserialize(
    utils::ByteDeserializer& deserializer) {

    // First deserialize the base header
    auto base_header = BaseHeader::Deserialize(deserializer);
    if (!base_header) {
        LOG_ERROR("Failed to deserialize base header");
        return std::nullopt;
    }

    // Verify this is a sync beacon message
    if (base_header->GetType() != MessageType::SYNC_BEACON) {
        LOG_ERROR("Wrong message type for sync beacon header: %d",
                  static_cast<int>(base_header->GetType()));
        return std::nullopt;
    }

    // Deserialize optimized sync beacon specific fields
    auto network_id = deserializer.ReadUint16();
    auto total_slots = deserializer.ReadUint8();
    auto slot_duration_ms =
        deserializer.ReadUint16();  // Optimized from uint32_t
    auto hop_count = deserializer.ReadUint8();
    auto original_timestamp_ms =
        deserializer.ReadUint16();  // Optimized from uint32_t
    auto propagation_delay_ms = deserializer.ReadUint32();
    auto max_hops = deserializer.ReadUint8();

    // Check if all fields were successfully read
    if (!network_id || !total_slots || !slot_duration_ms || !hop_count ||
        !original_timestamp_ms || !propagation_delay_ms || !max_hops) {
        LOG_ERROR("Failed to deserialize sync beacon header fields");
        return std::nullopt;
    }

    // Create and return the sync beacon header
    SyncBeaconHeader header(
        base_header->GetDestination(), base_header->GetSource(), *network_id,
        *total_slots, *slot_duration_ms, *hop_count, *original_timestamp_ms,
        *propagation_delay_ms, *max_hops);

    return header;
}

}  // namespace loramesher