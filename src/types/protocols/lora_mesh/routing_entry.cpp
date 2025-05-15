/**
 * @file routing_entry.cpp
 * @brief Implementation of routing table entry for LoRaMesh protocol
 */

#include "routing_entry.hpp"

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {

bool RoutingEntry::IsBetterThan(const RoutingEntry& other) const {
    // First, prefer active routes over inactive ones
    if (is_active && !other.is_active) {
        return true;
    }
    if (!is_active && other.is_active) {
        return false;
    }

    // If both active or both inactive, compare by hop count
    if (hop_count != other.hop_count) {
        return hop_count < other.hop_count;  // Fewer hops is better
    }

    // If hop counts are equal, compare by link quality
    return link_quality > other.link_quality;  // Higher quality is better
}

bool RoutingEntry::IsExpired(uint32_t current_time, uint32_t timeout_ms) const {
    return (current_time - last_updated) > timeout_ms;
}

void RoutingEntry::Update(AddressType new_next_hop, uint8_t new_hop_count,
                          uint8_t new_link_quality, uint8_t new_allocated_slots,
                          uint32_t current_time) {
    next_hop = new_next_hop;
    hop_count = new_hop_count;
    link_quality = new_link_quality;
    allocated_slots = new_allocated_slots;
    last_updated = current_time;
    is_active = true;
}

Result RoutingEntry::Serialize(utils::ByteSerializer& serializer) const {
    serializer.WriteUint16(destination);
    serializer.WriteUint16(next_hop);
    serializer.WriteUint8(hop_count);
    serializer.WriteUint8(allocated_slots);
    serializer.WriteUint8(link_quality);
    serializer.WriteUint32(last_updated);
    serializer.WriteUint8(is_active ? 1 : 0);

    return Result::Success();
}

std::optional<RoutingEntry> RoutingEntry::Deserialize(
    utils::ByteDeserializer& deserializer) {

    auto destination = deserializer.ReadUint16();
    auto next_hop = deserializer.ReadUint16();
    auto hop_count = deserializer.ReadUint8();
    auto allocated_slots = deserializer.ReadUint8();
    auto link_quality = deserializer.ReadUint8();
    auto last_updated = deserializer.ReadUint32();
    auto is_active_raw = deserializer.ReadUint8();

    if (!destination || !next_hop || !hop_count || !allocated_slots ||
        !link_quality || !last_updated || !is_active_raw) {
        return std::nullopt;
    }

    return RoutingEntry(*destination, *next_hop, *hop_count, *allocated_slots,
                        *link_quality, *last_updated, *is_active_raw != 0);
}

bool RoutingEntry::operator==(const RoutingEntry& other) const {
    return destination == other.destination && next_hop == other.next_hop &&
           hop_count == other.hop_count &&
           allocated_slots == other.allocated_slots &&
           link_quality == other.link_quality &&
           last_updated == other.last_updated && is_active == other.is_active;
}

bool RoutingEntry::operator!=(const RoutingEntry& other) const {
    return !(*this == other);
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher