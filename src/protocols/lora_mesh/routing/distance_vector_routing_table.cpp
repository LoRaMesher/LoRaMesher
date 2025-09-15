/**
 * @file distance_vector_routing_table.cpp
 * @brief Implementation of distance-vector routing table
 */

#include "distance_vector_routing_table.hpp"
#include <numeric>
#include <sstream>

namespace loramesher {
namespace protocols {
namespace lora_mesh {

DistanceVectorRoutingTable::DistanceVectorRoutingTable(AddressType node_address,
                                                       size_t max_nodes)
    : node_address_(node_address),
      max_nodes_(max_nodes),
      lookup_count_(0),
      update_count_(0),
      last_cleanup_time_(0) {

    // Reserve space for efficient vector operations
    nodes_.reserve(max_nodes_ > 0 ? max_nodes_ : 50);

    LOG_DEBUG(
        "Created distance-vector routing table for node 0x%04X (max_nodes: "
        "%zu)",
        node_address_, max_nodes_);
}

AddressType DistanceVectorRoutingTable::FindNextHop(
    AddressType destination) const {
    std::lock_guard<std::mutex> lock(table_mutex_);
    lookup_count_++;

    // Self-addressed
    if (destination == node_address_) {
        return node_address_;
    }

    // Find best active route
    AddressType best_next_hop = 0;
    uint8_t best_hop_count = UINT8_MAX;
    uint8_t best_link_quality = 0;

    for (const auto& node : nodes_) {
        if (node.routing_entry.destination == destination && node.is_active) {
            // Prefer routes with fewer hops
            if (node.routing_entry.hop_count < best_hop_count) {
                best_hop_count = node.routing_entry.hop_count;
                best_next_hop = node.next_hop;
                best_link_quality = node.routing_entry.link_quality;
            }
            // If hop counts equal, prefer better link quality
            else if (node.routing_entry.hop_count == best_hop_count &&
                     node.routing_entry.link_quality > best_link_quality) {
                best_next_hop = node.next_hop;
                best_link_quality = node.routing_entry.link_quality;
            }
        }
    }

    return best_next_hop;
}

bool DistanceVectorRoutingTable::UpdateRoute(
    AddressType source, AddressType destination, uint8_t hop_count,
    uint8_t link_quality, uint8_t allocated_data_slots, uint32_t current_time) {
    std::lock_guard<std::mutex> lock(table_mutex_);
    update_count_++;

    // Get source link quality
    uint8_t source_link_quality = CalculateComprehensiveLinkQuality(source);
    uint8_t actual_link_quality = std::min(link_quality, source_link_quality);

    // Check hop limit (assume max 10 hops as reasonable default)
    constexpr uint8_t MAX_HOPS = 10;
    if (hop_count > MAX_HOPS) {
        return false;
    }

    bool route_changed = false;

    // Find or create node
    auto node_it = FindNode(destination);
    if (node_it != nodes_.end()) {
        // Update existing node if route is better
        types::protocols::lora_mesh::NetworkNodeRoute potential_route(
            destination, source, hop_count, actual_link_quality, current_time);

        if (!node_it->is_active || IsBetterRoute(*node_it, potential_route)) {
            route_changed = node_it->UpdateRouteInfo(
                source, hop_count, actual_link_quality, current_time);

            if (allocated_data_slots !=
                node_it->routing_entry.allocated_data_slots) {
                node_it->routing_entry.allocated_data_slots =
                    allocated_data_slots;
                route_changed = true;
            }

            if (route_changed) {
                NotifyRouteUpdate(true, destination, source, hop_count);
            }
        }
    } else {
        // Add new node
        if (WouldExceedLimit()) {
            if (!RemoveOldestNode()) {
                LOG_WARNING("Cannot add node 0x%04X: routing table full",
                            destination);
                return false;
            }
        }

        types::protocols::lora_mesh::NetworkNodeRoute new_node(
            destination, source, hop_count, actual_link_quality, current_time);
        new_node.routing_entry.allocated_data_slots = allocated_data_slots;

        nodes_.push_back(new_node);
        route_changed = true;

        LOG_INFO("Added node 0x%04X with route via 0x%04X, hop count %d",
                 destination, source, hop_count);

        NotifyRouteUpdate(true, destination, source, hop_count);
    }

    return route_changed;
}

bool DistanceVectorRoutingTable::AddNode(
    const types::protocols::lora_mesh::NetworkNodeRoute& node) {

    std::lock_guard<std::mutex> lock(table_mutex_);

    // Allow adding local node for network manager or when it's needed for tests
    // We do track our own node in specific cases like when we're network manager
    if (node.routing_entry.destination == node_address_) {
        LOG_DEBUG(
            "Adding local node 0x%04X to routing table (network manager: %s)",
            node_address_, node.is_network_manager ? "yes" : "no");
    }

    auto node_it = FindNode(node.routing_entry.destination);
    if (node_it != nodes_.end()) {
        // Update existing node
        *node_it = node;
        LOG_DEBUG("Updated existing node 0x%04X in routing table",
                  node.routing_entry.destination);
        return true;
    } else {
        // Add new node
        if (WouldExceedLimit()) {
            if (!RemoveOldestNode()) {
                LOG_WARNING("Cannot add node 0x%04X: routing table full",
                            node.routing_entry.destination);
                return false;
            }
        }

        nodes_.push_back(node);
        LOG_INFO("Added new node 0x%04X to routing table",
                 node.routing_entry.destination);
        return true;
    }
}

bool DistanceVectorRoutingTable::UpdateNode(
    AddressType node_address, uint8_t battery_level, bool is_network_manager,
    uint8_t allocated_data_slots, uint8_t capabilities, uint32_t current_time) {
    std::lock_guard<std::mutex> lock(table_mutex_);

    auto node_it = FindNode(node_address);
    if (node_it != nodes_.end()) {
        // Update existing node
        bool changed = node_it->UpdateNodeInfo(
            battery_level, is_network_manager, capabilities,
            allocated_data_slots, current_time);

        LOG_DEBUG("Updated node 0x%04X in routing table", node_address);
        return changed;
    } else {
        // Add new node if it doesn't exist
        if (WouldExceedLimit()) {
            if (!RemoveOldestNode()) {
                LOG_WARNING("Cannot add node 0x%04X: routing table full",
                            node_address);
                return false;
            }
        }

        // Create new node with NetworkNodeRoute
        types::protocols::lora_mesh::NetworkNodeRoute new_node(
            node_address, battery_level, current_time, is_network_manager,
            capabilities, allocated_data_slots);

        // For new nodes, assume they're direct neighbors initially
        new_node.next_hop = node_address;
        new_node.routing_entry.hop_count = 1;
        new_node.is_active = true;

        nodes_.push_back(new_node);
        LOG_INFO("Added new node 0x%04X to routing table", node_address);
        return true;
    }
}

bool DistanceVectorRoutingTable::RemoveNode(AddressType address) {
    std::lock_guard<std::mutex> lock(table_mutex_);

    auto node_it = FindNode(address);
    if (node_it != nodes_.end()) {
        LOG_INFO("Removing node 0x%04X from routing table", address);

        // Notify of route removal
        NotifyRouteUpdate(false, address, 0, 0);

        nodes_.erase(node_it);
        return true;
    }

    return false;
}

size_t DistanceVectorRoutingTable::RemoveInactiveNodes(
    uint32_t current_time, uint32_t route_timeout_ms,
    uint32_t node_timeout_ms) {
    std::lock_guard<std::mutex> lock(table_mutex_);

    size_t initial_size = nodes_.size();
    bool topology_changed = false;

    // First, mark routes as inactive if they've timed out
    for (auto& node : nodes_) {
        if (node.IsExpired(current_time, route_timeout_ms) && node.is_active) {
            node.is_active = false;

            // Notify of route removal
            NotifyRouteUpdate(false, node.routing_entry.destination, 0, 0);
            topology_changed = true;
        }
    }

    // Remove nodes that have been inactive for too long
    auto new_end = std::remove_if(
        nodes_.begin(), nodes_.end(),
        [current_time, node_timeout_ms](
            const types::protocols::lora_mesh::NetworkNodeRoute& node) {
            return node.IsExpired(current_time, node_timeout_ms);
        });

    size_t nodes_to_remove = std::distance(new_end, nodes_.end());

    if (nodes_to_remove > 0) {
        nodes_.erase(new_end, nodes_.end());
        topology_changed = true;
        LOG_INFO("Removed %zu inactive nodes from routing table",
                 nodes_to_remove);
    }

    if (topology_changed) {
        last_cleanup_time_ = current_time;
    }

    return initial_size - nodes_.size();
}

bool DistanceVectorRoutingTable::IsNodePresent(AddressType address) const {
    std::lock_guard<std::mutex> lock(table_mutex_);
    return FindNode(address) != nodes_.end();
}

const std::vector<types::protocols::lora_mesh::NetworkNodeRoute>&
DistanceVectorRoutingTable::GetNodes() const {
    // Note: Caller must be careful with concurrent access
    return nodes_;
}

size_t DistanceVectorRoutingTable::GetSize() const {
    std::lock_guard<std::mutex> lock(table_mutex_);
    return nodes_.size();
}

std::vector<RoutingTableEntry> DistanceVectorRoutingTable::GetRoutingEntries(
    AddressType exclude_address) const {

    std::lock_guard<std::mutex> lock(table_mutex_);

    std::vector<RoutingTableEntry> entries;
    entries.reserve(nodes_.size());

    for (const auto& node : nodes_) {
        if (node.is_active &&
            node.routing_entry.destination != exclude_address) {
            entries.push_back(node.ToRoutingTableEntry());
        }
    }

    return entries;
}

uint8_t DistanceVectorRoutingTable::GetLinkQuality(
    AddressType node_address) const {
    std::lock_guard<std::mutex> lock(table_mutex_);

    auto node_it = FindNode(node_address);
    if (node_it != nodes_.end()) {
        return node_it->GetLinkQuality();
    }
    return 0;
}

void DistanceVectorRoutingTable::SetRouteUpdateCallback(
    RouteUpdateCallback callback) {
    std::lock_guard<std::mutex> lock(table_mutex_);
    route_callback_ = callback;
}

void DistanceVectorRoutingTable::SetMaxNodes(size_t max_nodes) {
    std::lock_guard<std::mutex> lock(table_mutex_);
    max_nodes_ = max_nodes;

    // If we now exceed the limit, remove oldest nodes
    while (max_nodes_ > 0 && nodes_.size() > max_nodes_) {
        if (!RemoveOldestNode()) {
            break;  // Safety break if we can't remove any more nodes
        }
    }
}

void DistanceVectorRoutingTable::Clear() {
    std::lock_guard<std::mutex> lock(table_mutex_);

    // Notify of all route removals
    for (const auto& node : nodes_) {
        NotifyRouteUpdate(false, node.routing_entry.destination, 0, 0);
    }

    nodes_.clear();
    lookup_count_ = 0;
    update_count_ = 0;
    last_cleanup_time_ = 0;

    LOG_INFO("Cleared routing table for node 0x%04X", node_address_);
}

std::string DistanceVectorRoutingTable::GetStatistics() const {
    std::lock_guard<std::mutex> lock(table_mutex_);

    std::ostringstream oss;
    oss << "Routing Table Statistics (Node 0x" << std::hex << node_address_
        << "):\n";
    oss << "  Nodes: " << nodes_.size() << "/" << max_nodes_ << "\n";
    oss << "  Lookups: " << std::dec << lookup_count_ << "\n";
    oss << "  Updates: " << update_count_ << "\n";
    oss << "  Active routes: ";

    size_t active_routes = 0;
    for (const auto& node : nodes_) {
        if (node.is_active)
            active_routes++;
    }
    oss << active_routes << "\n";

    return oss.str();
}

void DistanceVectorRoutingTable::UpdateLinkStatistics() {
    std::lock_guard<std::mutex> lock(table_mutex_);

    // For each direct neighbor, expect a routing message
    for (auto& node : nodes_) {
        if (node.IsDirectNeighbor()) {
            node.ExpectRoutingMessage();
        }
    }
}

bool DistanceVectorRoutingTable::ProcessRoutingTableMessage(
    AddressType source_address, const std::vector<RoutingTableEntry>& entries,
    uint32_t reception_timestamp, uint8_t local_link_quality,
    uint8_t max_hops) {
    std::lock_guard<std::mutex> lock(table_mutex_);
    update_count_++;

    bool routing_changed = false;

    // First, handle the source node as a direct neighbor
    auto source_node_it = FindNode(source_address);
    if (source_node_it != nodes_.end()) {
        // Update existing source node - it's a direct neighbor
        source_node_it->ReceivedRoutingMessage(local_link_quality,
                                               reception_timestamp);

        // Ensure it's marked as direct neighbor with hop count 1
        if (source_node_it->routing_entry.hop_count != 1 ||
            source_node_it->next_hop != source_address) {
            source_node_it->next_hop = source_address;
            source_node_it->routing_entry.hop_count = 1;
            source_node_it->is_active = true;
            routing_changed = true;

            NotifyRouteUpdate(true, source_address, source_address, 1);
        }
    } else {
        // Add source as new direct neighbor
        if (WouldExceedLimit()) {
            if (!RemoveOldestNode()) {
                // Can't make space, but continue processing other entries
                LOG_WARNING("Cannot add source node 0x%04X: routing table full",
                            source_address);
            }
        }

        if (!WouldExceedLimit()) {  // Check again after potential removal
            types::protocols::lora_mesh::NetworkNodeRoute new_node(
                source_address, 100,
                reception_timestamp  // Assume 100% battery for new nodes
            );
            new_node.next_hop = source_address;
            new_node.routing_entry.hop_count = 1;
            new_node.routing_entry.link_quality = local_link_quality;
            new_node.is_active = true;

            // Register the received message for link quality tracking
            new_node.ReceivedRoutingMessage(local_link_quality,
                                            reception_timestamp);

            nodes_.push_back(new_node);
            routing_changed = true;

            NotifyRouteUpdate(true, source_address, source_address, 1);
            LOG_INFO("Added new direct neighbor node 0x%04X", source_address);
        }
    }

    // Get source node's current link quality for route calculations
    uint8_t source_link_quality =
        CalculateComprehensiveLinkQuality(source_address);

    // Process each routing entry from the message
    for (const auto& entry : entries) {
        auto dest = entry.destination;

        // Skip entries for ourselves or invalid addresses
        if (dest == node_address_ || dest == 0) {
            continue;
        }

        // Calculate actual metrics through source
        uint8_t hop_count_via_source = entry.hop_count + 1;
        uint8_t actual_link_quality =
            std::min(entry.link_quality, source_link_quality);

        // Don't consider routes that exceed max hops
        if (hop_count_via_source > max_hops) {
            continue;
        }

        // Check if this node already exists
        auto node_it = FindNode(dest);
        if (node_it != nodes_.end()) {
            // Check if this is a better route
            types::protocols::lora_mesh::NetworkNodeRoute potential_route(
                dest, source_address, hop_count_via_source, actual_link_quality,
                reception_timestamp);

            if (!node_it->is_active ||
                IsBetterRoute(*node_it, potential_route)) {
                // Update the existing route
                bool changed = node_it->UpdateRouteInfo(
                    source_address, hop_count_via_source, actual_link_quality,
                    reception_timestamp);

                // Update allocated data slots if available
                if (entry.allocated_data_slots !=
                    node_it->routing_entry.allocated_data_slots) {
                    node_it->routing_entry.allocated_data_slots =
                        entry.allocated_data_slots;
                    changed = true;
                }

                if (changed) {
                    routing_changed = true;
                    NotifyRouteUpdate(true, dest, source_address,
                                      hop_count_via_source);
                }
            }
        } else {
            // Add new node
            if (WouldExceedLimit()) {
                if (!RemoveOldestNode()) {
                    continue;  // Skip this entry if we can't make space
                }
            }

            types::protocols::lora_mesh::NetworkNodeRoute new_node;
            new_node.routing_entry = entry;
            new_node.next_hop = source_address;
            new_node.routing_entry.hop_count = hop_count_via_source;
            new_node.routing_entry.link_quality = actual_link_quality;
            new_node.last_updated = reception_timestamp;
            new_node.last_seen = reception_timestamp;
            new_node.is_active = true;

            nodes_.push_back(new_node);
            routing_changed = true;

            NotifyRouteUpdate(true, dest, source_address, hop_count_via_source);
            LOG_DEBUG("Added route to 0x%04X via 0x%04X (hops: %d)", dest,
                      source_address, hop_count_via_source);
        }
    }

    return routing_changed;
}

// Private helper methods

std::vector<types::protocols::lora_mesh::NetworkNodeRoute>::iterator
DistanceVectorRoutingTable::FindNode(AddressType node_address) {
    return std::find_if(
        nodes_.begin(), nodes_.end(),
        [node_address](
            const types::protocols::lora_mesh::NetworkNodeRoute& node) {
            return node.routing_entry.destination == node_address;
        });
}

std::vector<types::protocols::lora_mesh::NetworkNodeRoute>::const_iterator
DistanceVectorRoutingTable::FindNode(AddressType node_address) const {
    return std::find_if(
        nodes_.begin(), nodes_.end(),
        [node_address](
            const types::protocols::lora_mesh::NetworkNodeRoute& node) {
            return node.routing_entry.destination == node_address;
        });
}

bool DistanceVectorRoutingTable::WouldExceedLimit() const {
    return max_nodes_ > 0 && nodes_.size() >= max_nodes_;
}

bool DistanceVectorRoutingTable::RemoveOldestNode() {
    if (nodes_.empty()) {
        return false;
    }

    // Find oldest non-manager node
    auto oldest_it = nodes_.end();
    uint32_t oldest_time = UINT32_MAX;

    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
        // Don't remove network manager
        if (!it->is_network_manager && it->last_seen < oldest_time) {
            oldest_time = it->last_seen;
            oldest_it = it;
        }
    }

    if (oldest_it != nodes_.end()) {
        LOG_INFO("Removing oldest node 0x%04X to make space",
                 oldest_it->routing_entry.destination);

        // Notify of route removal
        NotifyRouteUpdate(false, oldest_it->routing_entry.destination, 0, 0);

        nodes_.erase(oldest_it);
        return true;
    }

    return false;
}

uint8_t DistanceVectorRoutingTable::CalculateComprehensiveLinkQuality(
    AddressType node_address) const {
    auto node_it = FindNode(node_address);
    if (node_it != nodes_.end()) {
        return node_it->GetLinkQuality();
    }
    return 128;  // Default medium quality for unknown nodes
}

bool DistanceVectorRoutingTable::IsBetterRoute(
    const types::protocols::lora_mesh::NetworkNodeRoute& current,
    const types::protocols::lora_mesh::NetworkNodeRoute& potential) const {

    return potential.IsBetterRouteThan(current);
}

void DistanceVectorRoutingTable::NotifyRouteUpdate(bool route_added,
                                                   AddressType destination,
                                                   AddressType next_hop,
                                                   uint8_t hop_count) {
    if (route_callback_) {
        route_callback_(route_added, destination, next_hop, hop_count);
    }
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher