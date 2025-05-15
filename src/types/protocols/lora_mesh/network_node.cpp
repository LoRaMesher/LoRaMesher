/**
 * @file network_node.cpp
 * @brief Implementation of network node information for LoRaMesh protocol
 */

#include "network_node.hpp"
#include <algorithm>
#include <sstream>

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {

bool NetworkNode::IsExpired(uint32_t current_time, uint32_t timeout_ms) const {
    return (current_time - last_seen) > timeout_ms;
}

void NetworkNode::UpdateLastSeen(uint32_t current_time) {
    last_seen = current_time;
}

bool NetworkNode::UpdateBatteryLevel(uint8_t new_battery_level,
                                     uint32_t current_time) {
    // Validate battery level
    if (new_battery_level > 100) {
        return false;
    }

    bool changed = (battery_level != new_battery_level);
    battery_level = new_battery_level;

    if (changed) {
        last_seen = current_time;
    }

    return changed;
}

void NetworkNode::UpdateCapabilities(uint8_t new_capabilities,
                                     uint32_t current_time) {
    capabilities = new_capabilities;
    last_seen = current_time;
}

void NetworkNode::UpdateAllocatedSlots(uint8_t new_slots,
                                       uint32_t current_time) {
    allocated_slots = new_slots;
    last_seen = current_time;
}

bool NetworkNode::HasCapability(uint8_t capability) const {
    return (capabilities & capability) != 0;
}

std::string NetworkNode::GetCapabilitiesString() const {
    return node_utils::CapabilitiesToString(capabilities);
}

Result NetworkNode::Serialize(utils::ByteSerializer& serializer) const {
    serializer.WriteUint16(address);
    serializer.WriteUint8(battery_level);
    serializer.WriteUint32(last_seen);
    serializer.WriteUint8(is_network_manager ? 1 : 0);
    serializer.WriteUint8(capabilities);
    serializer.WriteUint8(allocated_slots);

    return Result::Success();
}

std::optional<NetworkNode> NetworkNode::Deserialize(
    utils::ByteDeserializer& deserializer) {

    auto address = deserializer.ReadUint16();
    auto battery_level = deserializer.ReadUint8();
    auto last_seen = deserializer.ReadUint32();
    auto is_manager_raw = deserializer.ReadUint8();
    auto capabilities = deserializer.ReadUint8();
    auto allocated_slots = deserializer.ReadUint8();

    if (!address || !battery_level || !last_seen || !is_manager_raw ||
        !capabilities || !allocated_slots) {
        return std::nullopt;
    }

    return NetworkNode(*address, *battery_level, *last_seen,
                       *is_manager_raw != 0, *capabilities, *allocated_slots);
}

bool NetworkNode::operator==(const NetworkNode& other) const {
    return address == other.address;
}

bool NetworkNode::operator!=(const NetworkNode& other) const {
    return !(*this == other);
}

bool NetworkNode::operator<(const NetworkNode& other) const {
    return address < other.address;
}

namespace node_utils {

std::string CapabilitiesToString(uint8_t capabilities) {
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

std::vector<NetworkNode>::iterator FindNodeByAddress(
    std::vector<NetworkNode>& nodes, AddressType address) {

    return std::find_if(
        nodes.begin(), nodes.end(),
        [address](const NetworkNode& node) { return node.address == address; });
}

std::vector<NetworkNode>::const_iterator FindNodeByAddress(
    const std::vector<NetworkNode>& nodes, AddressType address) {

    return std::find_if(
        nodes.begin(), nodes.end(),
        [address](const NetworkNode& node) { return node.address == address; });
}

size_t RemoveExpiredNodes(std::vector<NetworkNode>& nodes,
                          uint32_t current_time, uint32_t timeout_ms) {
    size_t initial_size = nodes.size();

    nodes.erase(
        std::remove_if(nodes.begin(), nodes.end(),
                       [current_time, timeout_ms](const NetworkNode& node) {
                           return node.IsExpired(current_time, timeout_ms);
                       }),
        nodes.end());

    return initial_size - nodes.size();
}

}  // namespace node_utils

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher