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

    // Find best active route using ETX cost (consistent with IsBetterRouteThan)
    AddressType best_next_hop = 0;
    uint16_t best_cost = UINT16_MAX;
    uint8_t best_hop_count = UINT8_MAX;

    for (const auto& node : nodes_) {
        if (node.routing_entry.destination == destination && node.is_active) {
            uint16_t cost = types::protocols::lora_mesh::NetworkNodeRoute::
                CalculateRouteCost(node.routing_entry.hop_count,
                                   node.routing_entry.link_quality);
            if (cost < best_cost ||
                (cost == best_cost &&
                 node.routing_entry.hop_count < best_hop_count)) {
                best_cost = cost;
                best_hop_count = node.routing_entry.hop_count;
                best_next_hop = node.next_hop;
            }
        }
    }

    return best_next_hop;
}

bool DistanceVectorRoutingTable::UpdateRoute(
    AddressType source, AddressType destination, uint8_t hop_count,
    uint8_t link_quality, uint8_t allocated_data_slots, uint8_t capabilities,
    uint32_t current_time) {
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
    auto node_it = GetNode(destination);
    if (node_it != nodes_.end()) {
        // Update existing node if route is better
        types::protocols::lora_mesh::NetworkNodeRoute potential_route(
            destination, source, hop_count, actual_link_quality, current_time);

        bool should_update;
        if (!node_it->is_active) {
            // For inactive routes: only accept if not degrading hop count.
            // IsBetterRoute has active-beats-inactive semantics so it always
            // returns true here — use hop count directly instead.
            should_update = (hop_count <= node_it->routing_entry.hop_count);
        } else {
            should_update = IsBetterRoute(*node_it, potential_route);
        }
        if (!node_it->is_active && !should_update) {
            // Hysteresis: require multiple consecutive receptions before re-activation
            node_it->link_stats.recovery_counter++;
            node_it->last_seen = current_time;
            if (node_it->link_stats.recovery_counter >=
                reactivation_threshold_) {
                // Check if the old next_hop is still active. If not, accept
                // the new (worse-hops) route since the old one is broken.
                auto old_hop_it = GetNode(node_it->next_hop);
                bool old_hop_alive =
                    old_hop_it != nodes_.end() && old_hop_it->is_active;
                if (!old_hop_alive && source != node_it->next_hop) {
                    node_it->UpdateRouteInfo(source, hop_count,
                                             actual_link_quality, current_time);
                } else {
                    node_it->is_active = true;
                }
                node_it->link_stats.recovery_counter = 0;
                node_it->link_stats.inactive_probe_count = 0;
                route_changed = true;
                NotifyRouteUpdate(true, destination, node_it->next_hop,
                                  node_it->routing_entry.hop_count);
                LogRouteEntry(*node_it);
            }
        } else if (should_update) {
            route_changed = node_it->UpdateRouteInfo(
                source, hop_count, actual_link_quality, current_time);

            if (allocated_data_slots !=
                node_it->routing_entry.allocated_data_slots) {
                node_it->routing_entry.allocated_data_slots =
                    allocated_data_slots;
                route_changed = true;
            }

            // Update capabilities ONLY if the source is our next hop to this destination
            // Exception: Always accept if current capabilities are unknown (0x00)
            bool should_update_capabilities = false;

            if (node_it->routing_entry.capabilities == 0 && capabilities != 0) {
                // Current unknown - accept any non-zero value
                should_update_capabilities = true;
            } else if (node_it->next_hop == source && capabilities != 0 &&
                       capabilities != node_it->routing_entry.capabilities) {
                // Trust capabilities only from our next hop to this destination
                should_update_capabilities = true;
            }

            if (should_update_capabilities) {
                LOG_DEBUG(
                    "Updating capabilities for 0x%04X: 0x%02X → 0x%02X "
                    "(via next hop 0x%04X)",
                    destination, node_it->routing_entry.capabilities,
                    capabilities, source);
                node_it->routing_entry.capabilities = capabilities;
                route_changed = true;
            }

            if (route_changed) {
                NotifyRouteUpdate(true, destination, source, hop_count);
                LogRouteEntry(*node_it);
            }
        } else if (node_it->is_active) {
            // Route cost unchanged but node is still reachable — refresh timestamp
            node_it->last_seen = current_time;
        }

        // Capability propagation: update even when route cost hasn't
        // changed, so a late-arriving GATEWAY flag isn't lost
        if (node_it->is_active && capabilities != 0 &&
            capabilities != node_it->routing_entry.capabilities &&
            (node_it->routing_entry.capabilities == 0 ||
             node_it->next_hop == source)) {
            node_it->routing_entry.capabilities = capabilities;
            route_changed = true;
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

        // For new nodes, store capabilities only if non-zero
        // Source becomes the next_hop, so future updates will only be accepted from this source
        if (capabilities != 0) {
            new_node.routing_entry.capabilities = capabilities;
            LOG_DEBUG("New node 0x%04X via 0x%04X: caps=0x%02X", destination,
                      source, capabilities);
        } else {
            new_node.routing_entry.capabilities = 0;
            LOG_DEBUG("New node 0x%04X via 0x%04X: capabilities unknown",
                      destination, source);
        }

        nodes_.push_back(new_node);
        route_changed = true;

        LOG_INFO("Added node 0x%04X with route via 0x%04X, hop count %d",
                 destination, source, hop_count);

        NotifyRouteUpdate(true, destination, source, hop_count);
        LogRouteEntry(nodes_.back());
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

    auto node_it = GetNode(node.routing_entry.destination);
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
        nodes_.back().link_stats.ewma_alpha = ewma_alpha_fixed_;
        LOG_INFO("Added new node 0x%04X to routing table",
                 node.routing_entry.destination);
        return true;
    }
}

bool DistanceVectorRoutingTable::UpdateNode(
    AddressType node_address, uint8_t battery_level, bool is_network_manager,
    uint8_t allocated_data_slots, uint8_t capabilities, uint32_t current_time) {
    std::lock_guard<std::mutex> lock(table_mutex_);

    // Don't add self-entries via this method
    if (node_address == node_address_) {
        return false;
    }

    auto node_it = GetNode(node_address);
    if (node_it != nodes_.end()) {
        // Update existing node
        bool changed = node_it->UpdateNodeInfo(
            battery_level, is_network_manager, capabilities,
            allocated_data_slots, current_time);

        LOG_DEBUG("Updated node 0x%04X in routing table (caps=0x%02X)",
                  node_address, capabilities);
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

    auto node_it = GetNode(address);
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
            LogRouteEntry(node);
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
    return GetNode(address) != nodes_.end();
}

const std::vector<types::protocols::lora_mesh::NetworkNodeRoute>&
DistanceVectorRoutingTable::GetNodes() const {
    // Note: Caller must be careful with concurrent access
    return nodes_;
}

std::vector<types::protocols::lora_mesh::NetworkNodeRoute>
DistanceVectorRoutingTable::GetNodesCopy() const {
    std::lock_guard<std::mutex> lock(table_mutex_);
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
        // Exclude self-entries AND the specified exclude_address
        if (node.is_active &&
            node.routing_entry.destination != exclude_address &&
            node.routing_entry.destination != node_address_) {
            entries.push_back(node.ToRoutingTableEntry());
        }
    }

    return entries;
}

uint8_t DistanceVectorRoutingTable::GetLinkQuality(
    AddressType node_address) const {
    std::lock_guard<std::mutex> lock(table_mutex_);

    auto node_it = GetNode(node_address);
    if (node_it != nodes_.end()) {
        return node_it->GetLinkQuality();
    }
    return 0;
}

uint8_t DistanceVectorRoutingTable::GetDirectLinkQuality(
    AddressType node_address) const {
    std::lock_guard<std::mutex> lock(table_mutex_);

    auto node_it = GetNode(node_address);
    if (node_it != nodes_.end() && node_it->link_stats.messages_received > 0) {
        // Return at least 1 when we have measured data, reserving 0 for
        // "truly unknown node". This prevents callers from treating a
        // measured-but-terrible link as "no data available".
        return std::max(static_cast<uint8_t>(1),
                        node_it->link_stats.CalculateQuality());
    }
    return 0;
}

bool DistanceVectorRoutingTable::HasUnidirectionalRisk(
    AddressType node_address) const {
    std::lock_guard<std::mutex> lock(table_mutex_);
    for (const auto& node : nodes_) {
        if (node.routing_entry.destination == node_address &&
            node.routing_entry.hop_count == 1 && node.is_active) {
            return node.link_stats.messages_received >= 2 &&
                   node.link_stats.remote_link_quality == 0;
        }
    }
    return false;
}

void DistanceVectorRoutingTable::DegradeRouteQuality(AddressType destination,
                                                     uint8_t quality) {
    std::lock_guard<std::mutex> lock(table_mutex_);
    auto node_it = GetNode(destination);
    if (node_it != nodes_.end() &&
        node_it->routing_entry.link_quality > quality) {
        node_it->routing_entry.link_quality = quality;
    }
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

bool DistanceVectorRoutingTable::SetControlSlotIndex(
    AddressType node_address, uint8_t control_slot_index) {
    std::lock_guard<std::mutex> lock(table_mutex_);
    auto it = GetNode(node_address);
    if (it == nodes_.end()) {
        return false;
    }
    it->control_slot_index = control_slot_index;
    return true;
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

void DistanceVectorRoutingTable::SetLinkQualityParams(
    uint8_t ewma_alpha_fixed, uint8_t inactivation_threshold,
    uint8_t reactivation_threshold) {
    std::lock_guard<std::mutex> lock(table_mutex_);
    ewma_alpha_fixed_ = ewma_alpha_fixed;
    inactivation_threshold_ = inactivation_threshold;
    reactivation_threshold_ = reactivation_threshold;

    // Propagate alpha to all existing nodes
    for (auto& node : nodes_) {
        node.link_stats.ewma_alpha = ewma_alpha_fixed;
    }
}

void DistanceVectorRoutingTable::UpdateLinkStatistics() {
    std::lock_guard<std::mutex> lock(table_mutex_);

    for (auto& node : nodes_) {
        // Determine if this is an inactivated direct neighbor still being
        // probed for recovery
        bool is_probing =
            !node.is_active && node.routing_entry.hop_count == 1 &&
            node.link_stats.inactive_probe_count > 0 &&
            node.link_stats.inactive_probe_count < kMaxInactiveProbes;

        if (node.routing_entry.hop_count == 1 &&
            (node.is_active || is_probing)) {
            // Step 1: Read EWMA quality (updated by ReceivedMessage/ExpectMessage)
            constexpr uint32_t kMinExpectedForDegradation = 5;
            if (node.link_stats.messages_expected >=
                    kMinExpectedForDegradation &&
                node.link_stats.messages_received >=
                    kMinMessagesBeforeInvalidation) {
                uint8_t old_quality = node.routing_entry.link_quality;
                node.routing_entry.link_quality =
                    node.link_stats.CalculateQuality();
                if (node.routing_entry.link_quality != old_quality) {
                    LOG_DEBUG(
                        "LinkStats 0x%04X: quality %d -> %d "
                        "(ewma=%d remote=%d exp=%d recv=%d missed=%d%s)",
                        node.routing_entry.destination, old_quality,
                        node.routing_entry.link_quality,
                        node.link_stats.ewma_quality,
                        node.link_stats.remote_link_quality,
                        node.link_stats.messages_expected,
                        node.link_stats.messages_received,
                        node.link_stats.consecutive_missed,
                        is_probing ? " PROBING" : "");
                }
                LogRouteEntry(node);

                // Re-activate probing neighbor if quality recovered
                if (is_probing && node.routing_entry.link_quality >=
                                      kReactivationQualityThreshold) {
                    node.is_active = true;
                    node.link_stats.recovery_counter = 0;
                    node.link_stats.inactive_probe_count = 0;
                    NotifyRouteUpdate(true, node.routing_entry.destination,
                                      node.routing_entry.destination, 1);
                    LogRouteEntry(node);
                    LOG_DEBUG(
                        "Re-activated probing neighbor 0x%04X (quality=%d)",
                        node.routing_entry.destination,
                        node.routing_entry.link_quality);
                }

                // Cascade: cap multi-hop routes through this neighbor
                if (node.is_active) {
                    AddressType via = node.routing_entry.destination;
                    for (auto& other : nodes_) {
                        if (other.next_hop == via && other.is_active &&
                            other.routing_entry.hop_count > 1) {
                            if (other.routing_entry.link_quality >
                                node.routing_entry.link_quality) {
                                other.routing_entry.link_quality =
                                    node.routing_entry.link_quality;
                                LogRouteEntry(other);
                            }
                        }
                    }
                }
            }

            // Step 2: Hard invalidation — only for active nodes
            if (node.is_active &&
                node.link_stats.consecutive_missed >= inactivation_threshold_) {
                node.is_active = false;
                node.link_stats.recovery_counter = 0;
                NotifyRouteUpdate(false, node.routing_entry.destination, 0, 0);
                LogRouteEntry(node);

                AddressType lost_hop = node.routing_entry.destination;
                for (auto& other : nodes_) {
                    if (other.next_hop == lost_hop && other.is_active &&
                        other.routing_entry.hop_count > 1) {
                        other.is_active = false;
                        other.link_stats.recovery_counter = 0;
                        NotifyRouteUpdate(
                            false, other.routing_entry.destination, 0, 0);
                        LogRouteEntry(other);
                    }
                }
            }
        }

        // Step 3: Track expected messages for direct neighbors AND for
        // nodes we've received from.
        if (node.routing_entry.hop_count == 1) {
            if (node.is_active) {
                node.link_stats.inactive_probe_count = 0;
                node.ExpectRoutingMessage();
            } else if (node.link_stats.inactive_probe_count <
                       kMaxInactiveProbes) {
                node.link_stats.inactive_probe_count++;
                node.ExpectRoutingMessage();
            }
        } else if (node.is_active && node.link_stats.messages_received > 0) {
            node.ExpectRoutingMessage();
        }
    }
}

bool DistanceVectorRoutingTable::ProcessRoutingTableMessage(
    AddressType source_address, std::span<const RoutingTableEntry> entries,
    uint32_t reception_timestamp, uint8_t local_link_quality, uint8_t max_hops,
    uint8_t source_capabilities, uint8_t source_allocated_data_slots,
    float rssi, float snr) {
    std::lock_guard<std::mutex> lock(table_mutex_);
    update_count_++;

    bool routing_changed = false;

    // First, handle the source node as a direct neighbor
    auto source_node_it = GetNode(source_address);
    if (source_node_it != nodes_.end()) {
        // Save current route parameters before updating link stats
        uint8_t prev_hop_count = source_node_it->routing_entry.hop_count;
        uint8_t prev_quality = source_node_it->routing_entry.link_quality;
        AddressType prev_next_hop = source_node_it->next_hop;
        bool was_inactive = !source_node_it->is_active;

        // Update direct link statistics (always tracks the physical link)
        source_node_it->link_stats.ReceivedMessage(reception_timestamp, rssi,
                                                   snr);
        source_node_it->link_stats.UpdateRemoteQuality(local_link_quality);
        source_node_it->last_seen = reception_timestamp;
        uint8_t direct_quality = source_node_it->link_stats.CalculateQuality();

        LOG_DEBUG(
            "Source 0x%04X: remote_quality=%d ewma=%d link_quality=%d "
            "expected=%d received=%d%s",
            source_address, local_link_quality,
            source_node_it->link_stats.ewma_quality, direct_quality,
            source_node_it->link_stats.messages_expected,
            source_node_it->link_stats.messages_received,
            (local_link_quality == 0 &&
             source_node_it->link_stats.messages_expected >= 3)
                ? " [UNIDIRECTIONAL]"
                : "");

        // Always update capabilities for direct neighbor (source of the message)
        if (source_node_it->routing_entry.capabilities != source_capabilities) {
            source_node_it->routing_entry.capabilities = source_capabilities;
            routing_changed = true;
            LOG_DEBUG("Updated capabilities for direct neighbor 0x%04X: 0x%02X",
                      source_address, source_capabilities);
        }

        // Update allocated data slots for source node
        if (source_node_it->routing_entry.allocated_data_slots !=
            source_allocated_data_slots) {
            source_node_it->routing_entry.allocated_data_slots =
                source_allocated_data_slots;
            routing_changed = true;
            LOG_DEBUG(
                "Updated allocated data slots for direct neighbor 0x%04X: %d",
                source_address, source_allocated_data_slots);
        }

        // Only force direct route (hop_count=1) when it has a lower ETX cost
        // than the current route. An indirect route via a relay can be better
        // when the direct link is unidirectional or very weak.
        uint16_t direct_cost =
            types::protocols::lora_mesh::NetworkNodeRoute::CalculateRouteCost(
                1, direct_quality);
        uint16_t current_cost =
            types::protocols::lora_mesh::NetworkNodeRoute::CalculateRouteCost(
                prev_hop_count, prev_quality);

        // A confirmed-unidirectional direct link should not replace an
        // existing indirect route: the indirect path may still deliver
        // packets while the direct one never will.
        bool direct_confirmed_unidirectional =
            source_node_it->link_stats.remote_link_quality == 0 &&
            source_node_it->link_stats.messages_expected >= 3;

        bool use_direct =
            was_inactive || prev_next_hop == source_address ||
            (direct_cost <= current_cost && !direct_confirmed_unidirectional);

        if (use_direct) {
            source_node_it->routing_entry.link_quality = direct_quality;
            if (source_node_it->routing_entry.hop_count != 1 ||
                source_node_it->next_hop != source_address) {
                source_node_it->next_hop = source_address;
                source_node_it->routing_entry.hop_count = 1;
                routing_changed = true;

                NotifyRouteUpdate(true, source_address, source_address, 1);
                LogRouteEntry(*source_node_it);
            }
        } else {
            LOG_DEBUG(
                "Kept relay route to 0x%04X via 0x%04X (direct_cost=%u "
                "current_cost=%u uni=%d remote_q=%d exp=%d miss=%d recv=%d)",
                source_address, prev_next_hop, direct_cost, current_cost,
                direct_confirmed_unidirectional ? 1 : 0,
                source_node_it->link_stats.remote_link_quality,
                source_node_it->link_stats.messages_expected,
                source_node_it->link_stats.consecutive_missed,
                source_node_it->link_stats.messages_received);
        }

        // Re-activate if was inactive (hearing from node = proof of liveness)
        if (was_inactive) {
            source_node_it->is_active = true;
            source_node_it->link_stats.recovery_counter = 0;
            source_node_it->link_stats.inactive_probe_count = 0;
            routing_changed = true;
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
            new_node.routing_entry.capabilities = source_capabilities;
            new_node.routing_entry.allocated_data_slots =
                source_allocated_data_slots;
            new_node.is_active = true;
            new_node.link_stats.ewma_alpha = ewma_alpha_fixed_;

            // Register the received message for link quality tracking
            new_node.ReceivedRoutingMessage(local_link_quality,
                                            reception_timestamp, rssi, snr);

            nodes_.push_back(new_node);
            routing_changed = true;

            NotifyRouteUpdate(true, source_address, source_address, 1);
            LogRouteEntry(nodes_.back());
            LOG_INFO("Added new direct neighbor node 0x%04X (data_slots: %d)",
                     source_address, source_allocated_data_slots);
        }
    }

    // Get source node's current link quality for route calculations
    uint8_t source_link_quality =
        CalculateComprehensiveLinkQuality(source_address);

    // Cascade: degrade routes through this source when physical link
    // quality has dropped. Prevents stale high-quality entries from
    // persisting after a source becomes unidirectional or degrades.
    for (auto& node : nodes_) {
        if (node.next_hop == source_address && node.is_active &&
            node.routing_entry.hop_count > 1 &&
            node.routing_entry.link_quality > source_link_quality) {
            node.routing_entry.link_quality = source_link_quality;
            routing_changed = true;
        }
    }

    // Process each routing entry from the message
    for (const auto& entry : entries) {
        auto dest = entry.destination;

        // Skip entries for ourselves or invalid addresses
        if (dest == node_address_ || dest == 0) {
            continue;
        }

        // Receiver-side split horizon: skip entries where the sender
        // routes through us. Accepting would create a loop:
        // us → source → us → ... The wire format carries next_hop
        // so we can detect this deterministically.
        if (entry.next_hop == node_address_) {
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
        auto node_it = GetNode(dest);
        if (node_it != nodes_.end()) {
            // Check if this is a better route
            types::protocols::lora_mesh::NetworkNodeRoute potential_route(
                dest, source_address, hop_count_via_source, actual_link_quality,
                reception_timestamp);

            bool should_update;
            if (!node_it->is_active) {
                // For inactive routes: only accept if not degrading hop count.
                // IsBetterRoute has active-beats-inactive semantics so it
                // always returns true here — use hop count directly instead.
                should_update =
                    (hop_count_via_source <= node_it->routing_entry.hop_count);
            } else {
                should_update = IsBetterRoute(*node_it, potential_route);
            }

            if (!node_it->is_active && !should_update) {
                // Hysteresis: require multiple consecutive receptions
                node_it->link_stats.recovery_counter++;
                node_it->last_seen = reception_timestamp;
                if (node_it->link_stats.recovery_counter >=
                    reactivation_threshold_) {
                    // Check if old next_hop is still active. If broken,
                    // accept the new route even though it has worse hops.
                    auto old_hop_it = GetNode(node_it->next_hop);
                    bool old_hop_alive =
                        old_hop_it != nodes_.end() && old_hop_it->is_active;
                    if (!old_hop_alive && source_address != node_it->next_hop) {
                        LOG_DEBUG(
                            "Demote 0x%04X hop=%d via 0x%04X -> hop=%d via "
                            "0x%04X (relay advertisement, old hop dead)",
                            dest, node_it->routing_entry.hop_count,
                            node_it->next_hop, hop_count_via_source,
                            source_address);
                        node_it->UpdateRouteInfo(
                            source_address, hop_count_via_source,
                            actual_link_quality, reception_timestamp);
                    } else {
                        node_it->is_active = true;
                    }
                    node_it->link_stats.recovery_counter = 0;
                    node_it->link_stats.inactive_probe_count = 0;
                    routing_changed = true;
                    NotifyRouteUpdate(true, dest, node_it->next_hop,
                                      node_it->routing_entry.hop_count);
                    LogRouteEntry(*node_it);
                }
            } else if (should_update) {
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

                // Update capabilities ONLY if the message source is our next hop to this node
                // This ensures we only trust information from the optimal path
                // Exception: Always accept if current capabilities are unknown (0x00)
                bool should_update_capabilities = false;

                if (node_it->routing_entry.capabilities == 0 &&
                    entry.capabilities != 0) {
                    // Current unknown - accept any non-zero value from any source
                    should_update_capabilities = true;
                } else if (node_it->next_hop == source_address &&
                           entry.capabilities != 0 &&
                           entry.capabilities !=
                               node_it->routing_entry.capabilities) {
                    // Trust capabilities only from our next hop to this destination
                    should_update_capabilities = true;
                }

                if (should_update_capabilities) {
                    LOG_DEBUG(
                        "Updating capabilities for 0x%04X: 0x%02X → 0x%02X "
                        "(via next hop 0x%04X)",
                        dest, node_it->routing_entry.capabilities,
                        entry.capabilities, source_address);
                    node_it->routing_entry.capabilities = entry.capabilities;
                    changed = true;
                }

                if (changed) {
                    routing_changed = true;
                    NotifyRouteUpdate(true, dest, source_address,
                                      hop_count_via_source);
                    LogRouteEntry(*node_it);
                }
            }

            // Capability propagation: update even when route cost hasn't
            // changed, so a late-arriving GATEWAY flag isn't lost
            if (node_it->is_active && entry.capabilities != 0 &&
                entry.capabilities != node_it->routing_entry.capabilities &&
                (node_it->routing_entry.capabilities == 0 ||
                 node_it->next_hop == source_address)) {
                node_it->routing_entry.capabilities = entry.capabilities;
                routing_changed = true;
            }

            // Always refresh last_seen — receiving routing info about this
            // node proves it's still alive in the network
            node_it->UpdateLastSeen(reception_timestamp);
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

            // For new nodes, we learn capabilities from whoever told us about them
            // Since source_address is the next_hop, this is consistent with our rule
            // Only store non-zero capabilities
            if (entry.capabilities == 0) {
                new_node.routing_entry.capabilities = 0;
                LOG_DEBUG("New node 0x%04X via 0x%04X: capabilities unknown",
                          dest, source_address);
            } else {
                // Keep the capability from the entry (already set above via entry assignment)
                LOG_DEBUG("New node 0x%04X via 0x%04X: caps=0x%02X", dest,
                          source_address, entry.capabilities);
            }

            nodes_.push_back(new_node);
            routing_changed = true;

            NotifyRouteUpdate(true, dest, source_address, hop_count_via_source);
            LogRouteEntry(nodes_.back());
            LOG_DEBUG("Added route to 0x%04X via 0x%04X (hops: %d)", dest,
                      source_address, hop_count_via_source);
        }
    }

    return routing_changed;
}

// Private helper methods

std::vector<types::protocols::lora_mesh::NetworkNodeRoute>::iterator
DistanceVectorRoutingTable::GetNode(AddressType node_address) {
    return std::find_if(
        nodes_.begin(), nodes_.end(),
        [node_address](
            const types::protocols::lora_mesh::NetworkNodeRoute& node) {
            return node.routing_entry.destination == node_address;
        });
}

std::vector<types::protocols::lora_mesh::NetworkNodeRoute>::const_iterator
DistanceVectorRoutingTable::GetNode(AddressType node_address) const {
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
    auto node_it = GetNode(node_address);
    if (node_it != nodes_.end()) {
        // Use measured physical link quality when we have direct
        // observations. This prevents a stale routing_entry.link_quality
        // (from an indirect route) from inflating source quality in the
        // entries loop, which would create phantom routes through
        // unidirectional neighbors.
        if (node_it->link_stats.messages_received > 0) {
            return node_it->link_stats.CalculateQuality();
        }
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

void DistanceVectorRoutingTable::LogRouteEntry(
    const types::protocols::lora_mesh::NetworkNodeRoute& node) {
    LOG_DEBUG(
        "RTENTRY dest=0x%04X via=0x%04X hops=%d quality=%d active=%d nm=%d",
        node.routing_entry.destination, node.next_hop,
        node.routing_entry.hop_count, node.routing_entry.link_quality,
        node.is_active ? 1 : 0, node.is_network_manager ? 1 : 0);
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher