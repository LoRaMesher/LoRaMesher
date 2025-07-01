/**
 * @file sync_beacon_header.cpp
 * @brief Implementation of multi-hop synchronization beacon header functionality
 */

#include "sync_beacon_header.hpp"
#include "utils/logger.hpp"

namespace loramesher {

SyncBeaconHeader::SyncBeaconHeader(AddressType dest, AddressType src,
                                   uint16_t network_id,
                                   uint32_t superframe_number,
                                   uint16_t superframe_duration_ms,
                                   uint8_t total_slots,
                                   uint32_t slot_duration_ms)
    : BaseHeader(dest, src, MessageType::SYNC_BEACON,
                 0),  // Payload size set later
      network_id_(network_id),
      superframe_number_(superframe_number),
      superframe_duration_ms_(superframe_duration_ms),
      total_slots_(total_slots),
      slot_duration_ms_(slot_duration_ms),
      original_source_(src),      // Initially, source is the original source
      hop_count_(0),              // Original transmission has hop count 0
      original_timestamp_ms_(0),  // Set when transmitting
      propagation_delay_ms_(0),   // No delay for original transmission
      max_hops_(5),               // Default network diameter
      sequence_number_(0) {}      // Set when transmitting

SyncBeaconHeader::SyncBeaconHeader(
    AddressType dest, AddressType src, uint16_t network_id,
    uint32_t superframe_number, uint16_t superframe_duration_ms,
    uint8_t total_slots, uint32_t slot_duration_ms, AddressType original_source,
    uint8_t hop_count, uint32_t original_timestamp_ms,
    uint32_t propagation_delay_ms, uint8_t max_hops, uint16_t sequence_number)
    : BaseHeader(dest, src, MessageType::SYNC_BEACON,
                 0),  // Payload size set later
      network_id_(network_id),
      superframe_number_(superframe_number),
      superframe_duration_ms_(superframe_duration_ms),
      total_slots_(total_slots),
      slot_duration_ms_(slot_duration_ms),
      original_source_(original_source),
      hop_count_(hop_count),
      original_timestamp_ms_(original_timestamp_ms),
      propagation_delay_ms_(propagation_delay_ms),
      max_hops_(max_hops),
      sequence_number_(sequence_number) {}

Result SyncBeaconHeader::SetSyncInfo(uint16_t network_id,
                                     uint32_t superframe_number,
                                     uint16_t superframe_duration_ms,
                                     uint8_t total_slots,
                                     uint32_t slot_duration_ms) {
    // Validate parameters
    if (total_slots == 0) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Total slots must be greater than 0");
    }

    if (superframe_duration_ms == 0 || slot_duration_ms == 0) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Duration values must be greater than 0");
    }

    network_id_ = network_id;
    superframe_number_ = superframe_number;
    superframe_duration_ms_ = superframe_duration_ms;
    total_slots_ = total_slots;
    slot_duration_ms_ = slot_duration_ms;

    return Result::Success();
}

Result SyncBeaconHeader::SetForwardingInfo(AddressType original_source,
                                           uint8_t hop_count,
                                           uint32_t original_timestamp_ms,
                                           uint32_t propagation_delay_ms,
                                           uint8_t max_hops,
                                           uint16_t sequence_number) {
    // Validate hop count
    if (hop_count > max_hops) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Hop count cannot exceed max hops");
    }

    original_source_ = original_source;
    hop_count_ = hop_count;
    original_timestamp_ms_ = original_timestamp_ms;
    propagation_delay_ms_ = propagation_delay_ms;
    max_hops_ = max_hops;
    sequence_number_ = sequence_number;

    return Result::Success();
}

SyncBeaconHeader SyncBeaconHeader::CreateForwardedBeacon(
    AddressType forwarding_node, uint32_t processing_delay) const {
    // Create a new header with all the sync beacon fields and forwarding node as source
    SyncBeaconHeader forwarded(
        GetDestination(), forwarding_node, network_id_, superframe_number_,
        superframe_duration_ms_, total_slots_, slot_duration_ms_,
        original_source_, hop_count_ + 1, original_timestamp_ms_,
        propagation_delay_ms_ + processing_delay, max_hops_, sequence_number_);

    return forwarded;
}

Result SyncBeaconHeader::Serialize(utils::ByteSerializer& serializer) const {
    // First serialize the base header fields
    Result result = BaseHeader::Serialize(serializer);
    if (!result.IsSuccess()) {
        return result;
    }

    // Serialize sync beacon specific fields
    serializer.WriteUint16(network_id_);
    serializer.WriteUint32(superframe_number_);
    serializer.WriteUint16(superframe_duration_ms_);
    serializer.WriteUint8(total_slots_);
    serializer.WriteUint32(slot_duration_ms_);
    serializer.WriteUint16(original_source_);  // AddressType is uint16_t
    serializer.WriteUint8(hop_count_);
    serializer.WriteUint32(original_timestamp_ms_);
    serializer.WriteUint32(propagation_delay_ms_);
    serializer.WriteUint8(max_hops_);
    serializer.WriteUint16(sequence_number_);

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

    // Deserialize sync beacon specific fields
    auto network_id = deserializer.ReadUint16();
    auto superframe_number = deserializer.ReadUint32();
    auto superframe_duration_ms = deserializer.ReadUint16();
    auto total_slots = deserializer.ReadUint8();
    auto slot_duration_ms = deserializer.ReadUint32();
    auto original_source = deserializer.ReadUint16();  // AddressType
    auto hop_count = deserializer.ReadUint8();
    auto original_timestamp_ms = deserializer.ReadUint32();
    auto propagation_delay_ms = deserializer.ReadUint32();
    auto max_hops = deserializer.ReadUint8();
    auto sequence_number = deserializer.ReadUint16();

    // Check if all fields were successfully read
    if (!network_id || !superframe_number || !superframe_duration_ms ||
        !total_slots || !slot_duration_ms || !original_source || !hop_count ||
        !original_timestamp_ms || !propagation_delay_ms || !max_hops ||
        !sequence_number) {
        LOG_ERROR("Failed to deserialize sync beacon header fields");
        return std::nullopt;
    }

    // Create and return the sync beacon header
    SyncBeaconHeader header(
        base_header->GetDestination(), base_header->GetSource(), *network_id,
        *superframe_number, *superframe_duration_ms, *total_slots,
        *slot_duration_ms, *original_source, *hop_count, *original_timestamp_ms,
        *propagation_delay_ms, *max_hops, *sequence_number);

    return header;
}

}  // namespace loramesher