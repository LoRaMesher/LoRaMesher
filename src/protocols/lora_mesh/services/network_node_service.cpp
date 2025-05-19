/**
 * @file network_node_service.cpp
 * @brief Implementation of network node management service
 */

#include "network_node_service.hpp"
#include <algorithm>
#include <numeric>

namespace {
using namespace loramesher::types::protocols::lora_mesh;
}

namespace loramesher {
namespace protocols {
namespace lora_mesh {

NetworkNodeService::NetworkNodeService(
    std::shared_ptr<ITimeProvider> time_provider, size_t max_nodes)
    : time_provider_(time_provider), max_nodes_(max_nodes) {

    if (!time_provider_) {
        // Create default time provider if none provided
        time_provider_ = std::make_shared<TimeProvider>();
    }
}

bool NetworkNodeService::UpdateNetworkNode(AddressType node_address,
                                           uint8_t battery_level,
                                           bool is_network_manager) {
    return UpdateNetworkNode(node_address, battery_level, is_network_manager, 0,
                             0);
}

bool NetworkNodeService::UpdateNetworkNode(AddressType node_address,
                                           uint8_t battery_level,
                                           bool is_network_manager,
                                           uint8_t capabilities,
                                           uint8_t allocated_slots) {
    auto node_it = FindNode(node_address);
    uint32_t current_time = time_provider_->GetCurrentTime();

    if (node_it != network_nodes_.end()) {
        // Update existing node
        bool changed = false;

        // Update battery level if valid and different
        if (battery_level <= 100 && node_it->battery_level != battery_level) {
            node_it->battery_level = battery_level;
            changed = true;
        }

        // Update network manager status
        if (node_it->is_network_manager != is_network_manager) {
            node_it->is_network_manager = is_network_manager;
            changed = true;
        }

        // Update capabilities if provided
        if (capabilities != 0 && node_it->capabilities != capabilities) {
            node_it->capabilities = capabilities;
            changed = true;
        }

        // Update allocated slots if provided
        if (allocated_slots != 0 &&
            node_it->allocated_slots != allocated_slots) {
            node_it->allocated_slots = allocated_slots;
            changed = true;
        }

        // Always update last seen time
        node_it->last_seen = current_time;

        LOG_DEBUG("Updated node 0x%04X in network", node_address);
        if (changed) {
            LOG_INFO(
                "Node 0x%04X updated: battery=%d, manager=%d, "
                "capabilities=0x%02X, slots=%d",
                node_address, battery_level, is_network_manager, capabilities,
                allocated_slots);
        } else {
            LOG_DEBUG("Node 0x%04X unchanged", node_address);
        }

        return changed;
    } else {
        // Add new node
        if (WouldExceedLimit()) {
            if (!RemoveOldestNode()) {
                LOG_WARNING("Cannot add node 0x%04X: network full",
                            node_address);
                return false;
            }
        }

        // Validate battery level
        if (battery_level > 100) {
            battery_level = 100;
        }

        network_nodes_.emplace_back(node_address, battery_level, current_time,
                                    is_network_manager, capabilities,
                                    allocated_slots);

        LOG_INFO("Added new node 0x%04X to network", node_address);
        return true;
    }
}

bool NetworkNodeService::IsNodeInNetwork(AddressType node_address) const {
    return FindNode(node_address) != network_nodes_.end();
}

const std::vector<NetworkNode>& NetworkNodeService::GetNetworkNodes() const {
    return network_nodes_;
}

size_t NetworkNodeService::GetNetworkSize() const {
    return network_nodes_.size();
}

size_t NetworkNodeService::RemoveInactiveNodes(uint32_t timeout_ms) {
    uint32_t current_time = time_provider_->GetCurrentTime();
    size_t initial_size = network_nodes_.size();

    auto new_end =
        std::remove_if(network_nodes_.begin(), network_nodes_.end(),
                       [current_time, timeout_ms](const NetworkNode& node) {
                           return node.IsExpired(current_time, timeout_ms);
                       });

    network_nodes_.erase(new_end, network_nodes_.end());

    size_t removed_count = initial_size - network_nodes_.size();
    if (removed_count > 0) {
        LOG_INFO("Removed %zu inactive nodes from network", removed_count);
    }

    return removed_count;
}

const NetworkNode* NetworkNodeService::GetNode(AddressType node_address) const {
    auto node_it = FindNode(node_address);
    return (node_it != network_nodes_.end()) ? &(*node_it) : nullptr;
}

bool NetworkNodeService::RemoveNode(AddressType node_address) {
    auto node_it = FindNode(node_address);
    if (node_it != network_nodes_.end()) {
        network_nodes_.erase(node_it);
        LOG_INFO("Removed node 0x%04X from network", node_address);
        return true;
    }
    return false;
}

std::vector<const NetworkNode*> NetworkNodeService::GetNetworkManagers() const {
    std::vector<const NetworkNode*> managers;

    for (const auto& node : network_nodes_) {
        if (node.is_network_manager) {
            managers.push_back(&node);
        }
    }

    return managers;
}

bool NetworkNodeService::UpdateNodeCapabilities(AddressType node_address,
                                                uint8_t capabilities) {
    auto node_it = FindNode(node_address);
    if (node_it != network_nodes_.end()) {
        node_it->capabilities = capabilities;
        node_it->last_seen = time_provider_->GetCurrentTime();
        return true;
    }
    return false;
}

bool NetworkNodeService::UpdateNodeAllocatedSlots(AddressType node_address,
                                                  uint8_t allocated_slots) {
    auto node_it = FindNode(node_address);
    if (node_it != network_nodes_.end()) {
        node_it->allocated_slots = allocated_slots;
        node_it->last_seen = time_provider_->GetCurrentTime();
        return true;
    }
    return false;
}

std::vector<const NetworkNode*> NetworkNodeService::GetNodesWithCapability(
    uint8_t capability) const {

    std::vector<const NetworkNode*> matching_nodes;

    for (const auto& node : network_nodes_) {
        if (node.HasCapability(capability)) {
            matching_nodes.push_back(&node);
        }
    }

    return matching_nodes;
}

void NetworkNodeService::SortNodes(SortCriteria sort_by) {
    switch (sort_by) {
        case SortCriteria::ADDRESS:
            std::sort(network_nodes_.begin(), network_nodes_.end(),
                      [](const NetworkNode& a, const NetworkNode& b) {
                          return a.address < b.address;
                      });
            break;

        case SortCriteria::BATTERY_LEVEL:
            std::sort(network_nodes_.begin(), network_nodes_.end(),
                      [](const NetworkNode& a, const NetworkNode& b) {
                          return a.battery_level >
                                 b.battery_level;  // Descending
                      });
            break;

        case SortCriteria::LAST_SEEN:
            std::sort(network_nodes_.begin(), network_nodes_.end(),
                      [](const NetworkNode& a, const NetworkNode& b) {
                          return a.last_seen >
                                 b.last_seen;  // Most recent first
                      });
            break;

        case SortCriteria::ALLOCATED_SLOTS:
            std::sort(network_nodes_.begin(), network_nodes_.end(),
                      [](const NetworkNode& a, const NetworkNode& b) {
                          return a.allocated_slots >
                                 b.allocated_slots;  // Descending
                      });
            break;
    }
}

NetworkNodeService::NetworkStats NetworkNodeService::GetNetworkStats() const {
    NetworkStats stats = {};

    if (network_nodes_.empty()) {
        return stats;
    }

    stats.total_nodes = network_nodes_.size();

    uint32_t total_battery = 0;
    uint32_t total_slots = 0;
    uint32_t current_time = time_provider_->GetCurrentTime();
    uint32_t max_age = 0;

    for (const auto& node : network_nodes_) {
        total_battery += node.battery_level;
        total_slots += node.allocated_slots;

        if (node.is_network_manager) {
            stats.network_managers++;
        }

        // Check if node is active (seen within last 30 seconds)
        // TODO: Make this configurable
        if (current_time - node.last_seen <= 30000) {
            stats.active_nodes++;
        }

        uint32_t age = current_time - node.last_seen;
        if (age > max_age) {
            max_age = age;
        }
    }

    stats.avg_battery_level = total_battery / stats.total_nodes;
    stats.total_allocated_slots = total_slots;
    stats.oldest_node_age_ms = max_age;

    return stats;
}

std::vector<NetworkNode>::iterator NetworkNodeService::FindNode(
    AddressType node_address) {
    return std::find_if(network_nodes_.begin(), network_nodes_.end(),
                        [node_address](const NetworkNode& node) {
                            return node.address == node_address;
                        });
}

std::vector<NetworkNode>::const_iterator NetworkNodeService::FindNode(
    AddressType node_address) const {
    return std::find_if(network_nodes_.begin(), network_nodes_.end(),
                        [node_address](const NetworkNode& node) {
                            return node.address == node_address;
                        });
}

bool NetworkNodeService::WouldExceedLimit() const {
    return max_nodes_ > 0 && network_nodes_.size() >= max_nodes_;
}

bool NetworkNodeService::RemoveOldestNode() {
    if (network_nodes_.empty()) {
        return false;
    }

    // Find node with oldest last_seen time
    auto oldest_it =
        std::min_element(network_nodes_.begin(), network_nodes_.end(),
                         [](const NetworkNode& a, const NetworkNode& b) {
                             return a.last_seen < b.last_seen;
                         });

    if (oldest_it != network_nodes_.end()) {
        LOG_INFO("Removing oldest node 0x%04X to make space",
                 oldest_it->address);
        network_nodes_.erase(oldest_it);
        return true;
    }

    return false;
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher