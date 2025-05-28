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
        return 0;
    }

    // Calculate local quality (0-255)
    uint16_t local_quality =
        std::min(static_cast<uint32_t>(255),
                 (messages_received * 255) / messages_expected);

    // Average with remote link quality if available
    if (remote_link_quality > 0) {
        return (local_quality + remote_link_quality) / 2;
    }

    return static_cast<uint8_t>(local_quality);
}

void NetworkNodeRoute::LinkQualityStats::Reset() {
    messages_expected = 0;
    messages_received = 0;
    // Don't reset last_message_time or remote_link_quality
}

void NetworkNodeRoute::LinkQualityStats::ExpectMessage() {
    messages_expected++;
}

void NetworkNodeRoute::LinkQualityStats::ReceivedMessage(
    uint32_t current_time) {
    messages_received++;
    last_message_time = current_time;
}

void NetworkNodeRoute::LinkQualityStats::UpdateRemoteQuality(uint8_t quality) {
    remote_link_quality = quality;
}

// NetworkNodeRoute implementation
NetworkNodeRoute::NetworkNodeRoute(AddressType addr, uint32_t time)
    : address(addr), last_updated(time), last_seen(time) {}

NetworkNodeRoute::NetworkNodeRoute(AddressType addr, uint8_t battery,
                                   uint32_t time, bool is_manager, uint8_t caps,
                                   uint8_t slots)
    : address(addr),
      battery_level(battery),
      last_seen(time),
      last_updated(time),
      is_network_manager(is_manager),
      capabilities(caps),
      allocated_slots(slots) {}

NetworkNodeRoute::NetworkNodeRoute(AddressType dest, AddressType next,
                                   uint8_t hops, uint8_t quality, uint32_t time)
    : address(dest),
      next_hop(next),
      hop_count(hops),
      link_quality(quality),
      last_updated(time),
      last_seen(time),
      is_active(true) {}

bool NetworkNodeRoute::IsExpired(uint32_t current_time,
                                 uint32_t timeout_ms) const {
    return (current_time - last_updated) > timeout_ms;
}

bool NetworkNodeRoute::IsDirectNeighbor() const {
    return hop_count == 1 && is_active;
}

bool NetworkNodeRoute::IsBetterRouteThan(const NetworkNodeRoute& other) const {
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

void NetworkNodeRoute::UpdateLastSeen(uint32_t current_time) {
    last_seen = current_time;
}

bool NetworkNodeRoute::UpdateNodeInfo(uint8_t battery, bool is_manager,
                                      uint8_t caps, uint8_t slots,
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

    // Update capabilities if provided
    if (caps != 0 && capabilities != caps) {
        capabilities = caps;
        changed = true;
    }

    // Update allocated slots if provided
    if (slots != 0 && allocated_slots != slots) {
        allocated_slots = slots;
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

    if (hop_count != new_hop_count) {
        hop_count = new_hop_count;
        changed = true;
    }

    // Update link quality based on parameter
    if (link_quality != new_link_quality) {
        link_quality = new_link_quality;
        changed = true;
    }

    last_updated = current_time;
    is_active = true;

    return changed;
}

bool NetworkNodeRoute::UpdateFromRoutingEntry(const RoutingTableEntry& entry,
                                              AddressType next_hop_addr,
                                              uint32_t current_time) {
    bool changed = false;

    // Update routing information
    if (next_hop != next_hop_addr) {
        next_hop = next_hop_addr;
        changed = true;
    }

    if (hop_count != entry.hop_count) {
        hop_count = entry.hop_count;
        changed = true;
    }

    if (link_quality != entry.link_quality) {
        link_quality = entry.link_quality;
        changed = true;
    }

    if (allocated_slots != entry.allocated_slots) {
        allocated_slots = entry.allocated_slots;
        changed = true;
    }

    // Update timestamps and status
    last_updated = current_time;
    is_active = true;

    return changed;
}

NetworkNodeRoute::RoutingTableEntry NetworkNodeRoute::ToRoutingEntry() const {
    return RoutingTableEntry(address, hop_count, link_quality, allocated_slots);
}

void NetworkNodeRoute::ExpectRoutingMessage() {
    link_stats.ExpectMessage();

    // Update link quality from statistics
    link_quality = link_stats.CalculateQuality();
}

void NetworkNodeRoute::ReceivedRoutingMessage(uint8_t remote_quality,
                                              uint32_t current_time) {
    link_stats.ReceivedMessage(current_time);
    link_stats.UpdateRemoteQuality(remote_quality);

    // Update link quality from statistics
    link_quality = link_stats.CalculateQuality();

    // Update last seen time
    last_seen = current_time;
}

uint8_t NetworkNodeRoute::GetLinkQuality() const {
    return link_quality;
}

uint8_t NetworkNodeRoute::GetRemoteLinkQuality() const {
    return link_stats.remote_link_quality;
}

void NetworkNodeRoute::ResetLinkStats() {
    link_stats.Reset();
}

bool NetworkNodeRoute::HasCapability(uint8_t capability) const {
    return (capabilities & capability) != 0;
}

std::string NetworkNodeRoute::GetCapabilitiesString() const {
    std::vector<std::string> caps;

    // Define capability bits (these should match JoinRequestHeader capabilities)
    if (capabilities & 0x01)
        caps.push_back("ROUTER");
    if (capabilities & 0x02)
        caps.push_back("GATEWAY");
    if (capabilities & 0x04)
        caps.push_back("BATTERY_POWERED");
    if (capabilities & 0x08)
        caps.push_back("HIGH_BANDWIDTH");
    if (capabilities & 0x10)
        caps.push_back("TIME_SYNC_SOURCE");
    if (capabilities & 0x20)
        caps.push_back("SENSOR_NODE");
    if (capabilities & 0x40)
        caps.push_back("RESERVED");
    if (capabilities & 0x80)
        caps.push_back("EXTENDED_CAPS");

    if (caps.empty()) {
        return "NONE";
    }

    // Join capabilities with " | "
    std::ostringstream oss;
    for (size_t i = 0; i < caps.size(); ++i) {
        if (i > 0)
            oss << " | ";
        oss << caps[i];
    }

    return oss.str();
}

Result NetworkNodeRoute::Serialize(utils::ByteSerializer& serializer) const {
    // Node identity and status information
    serializer.WriteUint16(address);
    serializer.WriteUint8(battery_level);
    serializer.WriteUint32(last_seen);
    serializer.WriteUint8(is_network_manager ? 1 : 0);
    serializer.WriteUint8(capabilities);
    serializer.WriteUint8(allocated_slots);

    // Routing information
    serializer.WriteUint16(next_hop);
    serializer.WriteUint8(hop_count);
    serializer.WriteUint8(link_quality);
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
    auto capabilities = deserializer.ReadUint8();
    auto allocated_slots = deserializer.ReadUint8();

    // Read routing information
    auto next_hop = deserializer.ReadUint16();
    auto hop_count = deserializer.ReadUint8();
    auto link_quality = deserializer.ReadUint8();
    auto last_updated = deserializer.ReadUint32();
    auto is_active_raw = deserializer.ReadUint8();

    // Check if all reads were successful
    if (!address || !battery_level || !last_seen || !is_manager_raw ||
        !capabilities || !allocated_slots || !next_hop || !hop_count ||
        !link_quality || !last_updated || !is_active_raw) {
        return std::nullopt;
    }

    // Create and populate NetworkNodeRoute
    NetworkNodeRoute node_route;

    // Set node identity and status
    node_route.address = *address;
    node_route.battery_level = *battery_level;
    node_route.last_seen = *last_seen;
    node_route.is_network_manager = (*is_manager_raw != 0);
    node_route.capabilities = *capabilities;
    node_route.allocated_slots = *allocated_slots;

    // Set routing information
    node_route.next_hop = *next_hop;
    node_route.hop_count = *hop_count;
    node_route.link_quality = *link_quality;
    node_route.last_updated = *last_updated;
    node_route.is_active = (*is_active_raw != 0);

    return node_route;
}

bool NetworkNodeRoute::operator==(const NetworkNodeRoute& other) const {
    return address == other.address;
}

bool NetworkNodeRoute::operator!=(const NetworkNodeRoute& other) const {
    return !(*this == other);
}

bool NetworkNodeRoute::operator<(const NetworkNodeRoute& other) const {
    return address < other.address;
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher