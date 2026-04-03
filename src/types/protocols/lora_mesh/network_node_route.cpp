/**
 * @file network_node_route.cpp
 * @brief Implementation of combined network node and routing information
 */

#include "network_node_route.hpp"
#include <algorithm>
#include <sstream>

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {

// LinkQualityStats implementation
uint8_t NetworkNodeRoute::LinkQualityStats::CalculateQuality() const {
    if (messages_expected == 0) {
        return remote_link_quality > 0 ? remote_link_quality : 200;
    }

    // Use sliding window PDR when ready, otherwise fall back to EWMA
    uint8_t local_quality = window.IsReady() ? window.GetPDR() : ewma_quality;

    // Bidirectional: weighted bottleneck — 70% min + 30% average.
    // Pure min() causes premature abandonment of marginal direct links
    // (death spiral: route switch → EWMA decay → inactivation →
    // false UNIDIRECTIONAL → /4 penalty → no recovery).
    if (remote_link_quality > 0) {
        uint16_t bottleneck = std::min(local_quality, remote_link_quality);
        uint16_t average =
            (static_cast<uint16_t>(local_quality) + remote_link_quality) / 2;
        return static_cast<uint8_t>((bottleneck * 7 + average * 3) / 10);
    }

    // Unidirectional link: received 3+ routing tables from peer
    // but peer never lists us — they cannot hear us.
    // Return 1 (minimum quality): a link we cannot transmit on has
    // maximum ETX cost (65535). This lets the entries loop find an
    // indirect route via relay. Using 1 instead of 0 avoids the
    // "unknown/unset" semantics of quality=0.
    if (messages_expected >= 3) {
        return 1;
    }

    return local_quality;
}

void NetworkNodeRoute::LinkQualityStats::Reset() {
    messages_expected = 0;
    messages_received = 0;
    ewma_quality = 200;
    recovery_counter = 0;
    inactive_probe_count = 0;
    window.Reset();
    // Don't reset last_message_time or remote_link_quality
}

void NetworkNodeRoute::LinkQualityStats::ExpectMessage() {
    // Deferred decay: only apply EWMA decay if the previous expected
    // message was actually missed (consecutive_missed > 0 means no
    // ReceivedMessage() call since the last ExpectMessage())
    if (consecutive_missed > 0) {
        ewma_quality = static_cast<uint8_t>(
            (static_cast<uint16_t>(256u - ewma_alpha) * ewma_quality) / 256u);
    }
    messages_expected++;
    consecutive_missed++;
    window.Expect();
}

void NetworkNodeRoute::LinkQualityStats::ReceivedMessage(
    uint32_t current_time) {
    messages_received++;
    last_message_time = current_time;
    consecutive_missed = 0;
    // EWMA update: sample = 255 (successful reception)
    // ewma = alpha * 255 + (1 - alpha) * ewma
    ewma_quality = static_cast<uint8_t>(
        (static_cast<uint16_t>(ewma_alpha) * 255u +
         static_cast<uint16_t>(256u - ewma_alpha) * ewma_quality) /
        256u);
    window.Received();
}

void NetworkNodeRoute::LinkQualityStats::UpdateRemoteQuality(uint8_t quality) {
    remote_link_quality = quality;
}

// NetworkNodeRoute implementation
NetworkNodeRoute::NetworkNodeRoute(AddressType addr, uint32_t time)
    : routing_entry(addr, 0, 0, 0, 0), last_seen(time), last_updated(time) {}

NetworkNodeRoute::NetworkNodeRoute(AddressType addr, uint8_t battery,
                                   uint32_t time, bool is_manager, uint8_t caps,
                                   uint8_t slots)
    : routing_entry(addr, 0, 0, slots, caps),
      battery_level(battery),
      last_seen(time),
      is_network_manager(is_manager),
      next_hop(0),
      last_updated(time),
      is_active(true) {
    // LOG_DEBUG(
    //     "New routing entry created with address 0x%04X, "
    //     "battery %d%%, manager %s, slots %d",
    //     addr, battery, is_manager ? "yes" : "no", slots);
}

NetworkNodeRoute::NetworkNodeRoute(AddressType addr, uint8_t battery,
                                   uint32_t time, bool is_manager, uint8_t caps,
                                   uint8_t slots, uint8_t hops)
    : routing_entry(addr, hops, 200, slots,
                    caps),  // Default link quality of 200
      battery_level(battery),
      last_seen(time),
      is_network_manager(is_manager),
      next_hop(addr),  // Simple default: next hop is the node itself
      last_updated(time),
      is_active(true) {
    // LOG_DEBUG(
    //     "New routing entry created with address 0x%04X, "
    //     "battery %d%%, manager %s, slots %d, hops %d",
    //     addr, battery, is_manager ? "yes" : "no", slots, hops);
}

NetworkNodeRoute::NetworkNodeRoute(AddressType dest, AddressType next,
                                   uint8_t hops, uint8_t quality, uint32_t time)
    : routing_entry(dest, hops, quality, 0, 0),
      last_seen(time),
      next_hop(next),
      last_updated(time),
      is_active(true) {}

bool NetworkNodeRoute::IsExpired(uint32_t current_time,
                                 uint32_t timeout_ms) const {
    return (current_time - last_seen) > timeout_ms;
}

bool NetworkNodeRoute::IsDirectNeighbor() const {
    return routing_entry.hop_count == 1 && is_active;
}

uint16_t NetworkNodeRoute::CalculateRouteCost(uint8_t hop_count,
                                              uint8_t link_quality) {
    // ETX-inspired metric: cost = hop_count * (256 / quality), scaled by 256.
    // Approximates Expected Transmission Count path cost (RFC 6551).
    // Each hop adds at least 256 to the cost (for a perfect link),
    // naturally penalizing longer paths without a tunable weight.
    if (link_quality == 0)
        return 65535;
    return static_cast<uint16_t>(
        std::min(static_cast<uint32_t>(hop_count) * 65536u / link_quality,
                 static_cast<uint32_t>(65535)));
}

bool NetworkNodeRoute::IsBetterRouteThan(const NetworkNodeRoute& other) const {
    // Active routes always preferred
    if (is_active && !other.is_active) {
        return true;
    }
    if (!is_active && other.is_active) {
        return false;
    }

    // Compare by composite cost (lower is better)
    uint16_t this_cost =
        CalculateRouteCost(routing_entry.hop_count, routing_entry.link_quality);
    uint16_t other_cost = CalculateRouteCost(other.routing_entry.hop_count,
                                             other.routing_entry.link_quality);

    if (this_cost != other_cost) {
        return this_cost < other_cost;
    }

    // Tiebreaker: prefer fewer hops
    return routing_entry.hop_count < other.routing_entry.hop_count;
}

void NetworkNodeRoute::UpdateLastSeen(uint32_t current_time) {
    last_seen = current_time;
}

bool NetworkNodeRoute::UpdateNodeInfo(uint8_t battery, bool is_manager,
                                      uint8_t caps, uint8_t data_slots,
                                      uint32_t current_time) {
    bool changed = false;

    // Update battery level if valid and different
    if (battery <= 100 && battery_level != battery) {
        battery_level = battery;
        changed = true;
    }

    // Update network manager status
    if (is_network_manager != is_manager) {
        is_network_manager = is_manager;
        changed = true;
    }

    // Update capabilities (allows setting to NONE/0x00)
    if (routing_entry.capabilities != caps) {
        routing_entry.capabilities = caps;
        changed = true;
    }

    // Update allocated slots if provided
    if (data_slots != 0 && routing_entry.allocated_data_slots != data_slots) {
        routing_entry.allocated_data_slots = data_slots;
        changed = true;
    }

    // Always update last seen time
    last_seen = current_time;

    return changed;
}

bool NetworkNodeRoute::UpdateRouteInfo(AddressType new_next_hop,
                                       uint8_t new_hop_count,
                                       uint8_t new_link_quality,
                                       uint32_t current_time) {
    bool changed = false;

    if (next_hop != new_next_hop) {
        next_hop = new_next_hop;
        changed = true;
    }

    if (routing_entry.hop_count != new_hop_count) {
        routing_entry.hop_count = new_hop_count;
        changed = true;
    }

    // Update link quality based on parameter
    if (routing_entry.link_quality != new_link_quality) {
        routing_entry.link_quality = new_link_quality;
        changed = true;
    }

    last_updated = current_time;
    is_active = true;

    return changed;
}

bool NetworkNodeRoute::UpdateFromRoutingTableEntry(
    const RoutingTableEntry& entry, AddressType next_hop_addr,
    uint32_t current_time) {
    bool changed = false;

    // Update routing information
    if (next_hop != next_hop_addr) {
        next_hop = next_hop_addr;
        changed = true;
    }

    if (routing_entry.hop_count != entry.hop_count) {
        routing_entry.hop_count = entry.hop_count;
        changed = true;
    }

    if (routing_entry.link_quality != entry.link_quality) {
        routing_entry.link_quality = entry.link_quality;
        changed = true;
    }

    if (routing_entry.allocated_data_slots != entry.allocated_data_slots) {
        routing_entry.allocated_data_slots = entry.allocated_data_slots;
        changed = true;
    }

    if (routing_entry.capabilities != entry.capabilities) {
        routing_entry.capabilities = entry.capabilities;
        changed = true;
    }

    // Update timestamps and status
    last_updated = current_time;
    is_active = true;

    return changed;
}

bool NetworkNodeRoute::UpdateBatteryLevel(uint8_t new_battery,
                                          uint32_t current_time) {
    if (new_battery > 100) {
        return false;  // Invalid battery level
    }
    if (battery_level != new_battery) {
        battery_level = new_battery;
        last_seen = current_time;  // Update last seen time on battery change
        return true;               // Battery level changed
    }
    return false;  // No change
}

bool NetworkNodeRoute::UpdateAllocatedSlots(uint8_t new_slots,
                                            uint32_t current_time) {
    if (routing_entry.allocated_data_slots != new_slots) {
        routing_entry.allocated_data_slots = new_slots;
        last_seen = current_time;  // Update last seen time on slots change
        return true;               // Slots changed
    }
    return false;  // No change
}

bool NetworkNodeRoute::UpdateCapabilities(uint8_t new_capabilities,
                                          uint32_t current_time) {
    if (routing_entry.capabilities != new_capabilities) {
        routing_entry.capabilities = new_capabilities;
        last_seen =
            current_time;  // Update last seen time on capabilities change
        return true;       // Capabilities changed
    }
    return false;  // No change
}

RoutingTableEntry NetworkNodeRoute::ToRoutingTableEntry() const {
    RoutingTableEntry entry = routing_entry;
    entry.control_slot_index = control_slot_index;
    entry.next_hop = next_hop;
    if (link_stats.messages_received > 0) {
        entry.reception_quality = link_stats.window.IsReady()
                                      ? link_stats.window.GetPDR()
                                      : link_stats.ewma_quality;
    }
    return entry;
}

void NetworkNodeRoute::ExpectRoutingMessage() {
    link_stats.ExpectMessage();
}

void NetworkNodeRoute::ReceivedRoutingMessage(uint8_t remote_quality,
                                              uint32_t current_time) {
    link_stats.ReceivedMessage(current_time);
    link_stats.UpdateRemoteQuality(remote_quality);

    // Update link quality from EWMA statistics
    routing_entry.link_quality = link_stats.CalculateQuality();

    // Update last seen time
    last_seen = current_time;
}

uint8_t NetworkNodeRoute::GetLinkQuality() const {
    return routing_entry.link_quality;
}

uint8_t NetworkNodeRoute::GetRemoteLinkQuality() const {
    return link_stats.remote_link_quality;
}

void NetworkNodeRoute::ResetLinkStats() {
    link_stats.Reset();
}

Result NetworkNodeRoute::Serialize(utils::ByteSerializer& serializer) const {
    // Node identity and status information
    serializer.WriteUint16(routing_entry.destination);
    serializer.WriteUint8(battery_level);
    serializer.WriteUint32(last_seen);
    serializer.WriteUint8(is_network_manager ? 1 : 0);

    // Routing information
    serializer.WriteUint16(next_hop);
    serializer.WriteUint32(last_updated);
    serializer.WriteUint8(is_active ? 1 : 0);

    return Result::Success();
}

std::optional<NetworkNodeRoute> NetworkNodeRoute::Deserialize(
    utils::ByteDeserializer& deserializer) {

    // Read node identity and status information
    auto address = deserializer.ReadUint16();
    auto battery_level = deserializer.ReadUint8();
    auto last_seen = deserializer.ReadUint32();
    auto is_manager_raw = deserializer.ReadUint8();

    // Read routing information
    auto next_hop = deserializer.ReadUint16();
    auto last_updated = deserializer.ReadUint32();
    auto is_active_raw = deserializer.ReadUint8();

    // Check if all reads were successful
    if (!address || !battery_level || !last_seen || !is_manager_raw ||
        !next_hop || !last_updated || !is_active_raw) {
        return std::nullopt;
    }

    // Create and populate NetworkNodeRoute
    NetworkNodeRoute node_route;

    // Set node identity and status
    node_route.routing_entry = RoutingTableEntry(*address, 0, 0, 0, 0);
    node_route.battery_level = *battery_level;
    node_route.last_seen = *last_seen;
    node_route.is_network_manager = (*is_manager_raw != 0);

    // Set routing information
    node_route.next_hop = *next_hop;
    node_route.last_updated = *last_updated;
    node_route.is_active = (*is_active_raw != 0);

    return node_route;
}

bool NetworkNodeRoute::operator==(const NetworkNodeRoute& other) const {
    return routing_entry.destination == other.routing_entry.destination;
}

bool NetworkNodeRoute::operator!=(const NetworkNodeRoute& other) const {
    return !(*this == other);
}

bool NetworkNodeRoute::operator<(const NetworkNodeRoute& other) const {
    return routing_entry.destination < other.routing_entry.destination;
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher