/**
 * @file network_service.cpp
 * @brief Implementation of combined network service
 */

#include "network_service.hpp"
#include <algorithm>

namespace {
using namespace loramesher;
using namespace loramesher::types::protocols::lora_mesh;
}  // namespace

namespace loramesher {
namespace protocols {
namespace lora_mesh {

NetworkService::NetworkService(
    AddressType node_address,
    std::shared_ptr<IMessageQueueService> message_queue_service,
    std::shared_ptr<ITimeProvider> time_provider,
    std::shared_ptr<ISuperframeService> superframe_service)
    : node_address_(node_address),
      message_queue_service_(message_queue_service),
      time_provider_(time_provider),
      superframe_service_(superframe_service) {

    // Initialize configuration with defaults
    config_.node_address = node_address;

    // Initialize protocol state
    state_ = ProtocolState::INITIALIZING;
}

bool NetworkService::UpdateNetworkNode(AddressType node_address,
                                       uint8_t battery_level,
                                       bool is_network_manager,
                                       uint8_t capabilities,
                                       uint8_t allocated_slots) {
    // Don't track our own node
    if (node_address == node_address_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(network_mutex_);

    uint32_t current_time = time_provider_->GetCurrentTime();
    auto node_it = FindNode(node_address);

    if (node_it != network_nodes_.end()) {
        // Update existing node
        bool changed = node_it->UpdateNodeInfo(battery_level,
                                               is_network_manager, capabilities,
                                               allocated_slots, current_time);

        LOG_DEBUG("Updated node 0x%04X in network", node_address);

        if (changed) {
            LOG_INFO(
                "Node 0x%04X updated: battery=%d, manager=%d, "
                "capabilities=0x%02X, slots=%d",
                node_address, battery_level, is_network_manager, capabilities,
                allocated_slots);

            // If node became network manager, update network manager
            if (is_network_manager) {
                network_manager_ = node_address;
                LOG_INFO("Updated network manager to 0x%04X", node_address);
            }

            // Trigger superframe update if needed
            if (is_network_manager || allocated_slots > 0) {
                NotifySuperframeOfNetworkChanges();
            }
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

        // Create new node
        NetworkNodeRoute new_node(node_address, battery_level, current_time,
                                  is_network_manager, capabilities,
                                  allocated_slots);

        network_nodes_.push_back(new_node);

        LOG_INFO("Added new node 0x%04X to network", node_address);

        // If node is network manager, update network manager
        if (is_network_manager) {
            network_manager_ = node_address;
            LOG_INFO("Updated network manager to 0x%04X", node_address);
        }

        // Trigger superframe update if needed
        if (is_network_manager || allocated_slots > 0) {
            NotifySuperframeOfNetworkChanges();
        }

        return true;
    }
}

bool NetworkService::IsNodeInNetwork(AddressType node_address) const {
    std::lock_guard<std::mutex> lock(network_mutex_);
    return FindNode(node_address) != network_nodes_.end();
}

const std::vector<NetworkNodeRoute>& NetworkService::GetNetworkNodes() const {
    // Note: Caller must be careful with concurrent access
    return network_nodes_;
}

size_t NetworkService::GetNetworkSize() const {
    std::lock_guard<std::mutex> lock(network_mutex_);
    return network_nodes_.size();
}

size_t NetworkService::RemoveInactiveNodes() {
    std::lock_guard<std::mutex> lock(network_mutex_);

    uint32_t current_time = time_provider_->GetCurrentTime();
    size_t initial_size = network_nodes_.size();
    bool topology_changed = false;

    // First, identify nodes with inactive routes
    for (auto& node : network_nodes_) {
        if (node.IsExpired(current_time, config_.route_timeout_ms) &&
            node.is_active) {
            node.is_active = false;

            // Notify of route update
            if (route_update_callback_) {
                route_update_callback_(false, node.address, 0, 0);
            }

            topology_changed = true;
        }
    }

    // Remove nodes that have been inactive for twice the timeout
    auto new_end = std::remove_if(
        network_nodes_.begin(), network_nodes_.end(),
        [this, current_time](const NetworkNodeRoute& node) {
            return (current_time - node.last_seen) > config_.node_timeout_ms;
        });

    size_t nodes_to_remove = std::distance(new_end, network_nodes_.end());

    if (nodes_to_remove > 0) {
        network_nodes_.erase(new_end, network_nodes_.end());
        topology_changed = true;
        LOG_INFO("Removed %zu inactive nodes from network", nodes_to_remove);
    }

    // Update topology if changed
    if (topology_changed) {
        UpdateNetworkTopology();
    }

    return initial_size - network_nodes_.size();
}

Result NetworkService::ProcessRoutingTableMessage(const BaseMessage& message) {
    // Extract serialized message
    auto opt_serialized = message.Serialize();
    if (!opt_serialized) {
        LOG_ERROR("Failed to serialize routing message");
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to serialize routing message");
    }

    // Deserialize routing table message
    auto routing_msg =
        RoutingTableMessage::CreateFromSerialized(*opt_serialized);
    if (!routing_msg) {
        LOG_ERROR("Failed to deserialize routing message");
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to deserialize routing message");
    }

    auto source = message.GetSource();
    auto table_version = routing_msg->GetTableVersion();
    auto entries = routing_msg->GetEntries();
    auto current_time = time_provider_->GetCurrentTime();

    LOG_INFO(
        "Received routing table update from 0x%04X: version %d, %d entries",
        source, table_version, entries.size());

    // Process routing entries
    bool routing_changed = false;
    bool node_updated = false;

    // Update network manager from routing message
    auto network_manager = routing_msg->GetNetworkManager();
    if (network_manager != network_manager_) {
        std::lock_guard<std::mutex> lock(network_mutex_);
        network_manager_ = network_manager;
        LOG_INFO("Updated network manager to 0x%04X", network_manager);
        routing_changed = true;
    }

    // Update time synchronization
    if (source == network_manager_) {
        is_synchronized_ = true;
        last_sync_time_ = current_time;
    }

    // First, handle the source node and update its link quality
    {
        std::lock_guard<std::mutex> lock(network_mutex_);
        auto source_node_it = FindNode(source);

        // Get the link quality that the source reports for us
        uint8_t reported_quality =
            routing_msg->GetLinkQualityFor(node_address_);

        if (source_node_it != network_nodes_.end()) {
            // Source node exists, update it
            source_node_it->ReceivedRoutingMessage(reported_quality,
                                                   current_time);

            // Also update node info
            bool updated = source_node_it->UpdateNodeInfo(
                0,  // Battery level (not provided in routing message)
                source == network_manager,
                0,  // Capabilities (not provided in routing message)
                0,  // Allocated slots (not provided in routing message)
                current_time);

            node_updated |= updated;

            // Ensure source node is marked as direct neighbor with hop count 1
            if (source_node_it->hop_count != 1 ||
                source_node_it->next_hop != source) {
                source_node_it->next_hop = source;
                source_node_it->hop_count = 1;
                source_node_it->is_active = true;
                routing_changed = true;

                LOG_DEBUG("Updated source node 0x%04X as direct neighbor",
                          source);
            }
        } else {
            // Source node doesn't exist, add it
            NetworkNodeRoute new_node;
            new_node.address = source;
            new_node.next_hop = source;   // Direct neighbor
            new_node.hop_count = 1;       // Direct neighbor
            new_node.link_quality = 128;  // Initial quality (middle range)
            new_node.last_updated = current_time;
            new_node.last_seen = current_time;
            new_node.is_active = true;
            new_node.is_network_manager = (source == network_manager);

            // Register the received message in link quality stats
            new_node.link_stats.ReceivedMessage(current_time);
            new_node.link_stats.UpdateRemoteQuality(reported_quality);

            network_nodes_.push_back(new_node);
            node_updated = true;
            routing_changed = true;

            LOG_INFO("Added new direct neighbor node 0x%04X", source);
        }
    }

    // Process each route entry from the message
    for (const auto& entry : entries) {
        auto dest = entry.address;

        // Skip entries for ourselves or the source node
        if (dest == node_address_ || dest == source) {
            continue;
        }

        // Calculate the actual hop count and link quality
        uint8_t actual_hop_count = entry.hop_count + 1;
        uint8_t source_link_quality = CalculateComprehensiveLinkQuality(source);
        uint8_t actual_link_quality =
            std::min(entry.link_quality, source_link_quality);

        // Don't consider routes that exceed max hops
        if (actual_hop_count > config_.max_hops) {
            continue;
        }

        std::lock_guard<std::mutex> lock(network_mutex_);

        // Check if this node already exists in our network
        auto node_it = FindNode(dest);
        if (node_it != network_nodes_.end()) {
            // Create a routing entry for comparison
            NetworkNodeRoute::RoutingTableEntry route_entry(
                dest, actual_hop_count, actual_link_quality,
                entry.allocated_slots);

            // Check if route is better and update if needed
            if (node_it->IsBetterRouteThan(
                    NetworkNodeRoute(dest, source, actual_hop_count,
                                     actual_link_quality, current_time))) {
                bool changed = node_it->UpdateFromRoutingEntry(
                    route_entry, source, current_time);
                node_it->UpdateNodeInfo(
                    entry.battery_level, entry.is_network_manager,
                    entry.capabilities, entry.allocated_slots, current_time);

                routing_changed |= changed;
            }
        } else {
            // Add new node
            if (WouldExceedLimit()) {
                if (!RemoveOldestNode()) {
                    continue;
                }
            }

            NetworkNodeRoute new_node(dest, entry.battery_level, current_time,
                                      entry.is_network_manager,
                                      entry.capabilities,
                                      entry.allocated_slots);
            new_node.next_hop = source;
            new_node.hop_count = actual_hop_count;
            new_node.link_quality = actual_link_quality;
            new_node.is_active = true;

            network_nodes_.push_back(new_node);
            routing_changed = true;
        }
    }

    // If routing changed significantly or nodes were updated, update network topology
    if (routing_changed || node_updated) {
        UpdateNetworkTopology();
    }

    return Result::Success();
}

Result NetworkService::SendRoutingTableUpdate() {
    // Create a routing table message
    auto message = CreateRoutingTableMessage();
    if (!message) {
        return Result(LoraMesherErrorCode::kMemoryError,
                      "Failed to create routing table message");
    }

    // Add the message to the control message queue
    message_queue_service_->AddMessageToQueue(
        SlotAllocation::SlotType::CONTROL_TX, std::move(message));

    LOG_DEBUG("Routing table update message queued for transmission");
    return Result::Success();
}

AddressType NetworkService::FindNextHop(AddressType destination) const {
    // If destination is ourselves, return our address
    if (destination == node_address_) {
        return node_address_;
    }

    std::lock_guard<std::mutex> lock(network_mutex_);

    // Look for direct route to destination (active routes only)
    for (const auto& node : network_nodes_) {
        if (node.address == destination && node.is_active) {
            return node.next_hop;
        }
    }

    // If no direct route, find best route by hop count
    AddressType best_next_hop = 0;
    uint8_t best_hop_count = config_.max_hops + 1;  // Higher than max allowed

    for (const auto& node : network_nodes_) {
        if (node.address == destination && node.hop_count < best_hop_count &&
            node.is_active) {
            best_hop_count = node.hop_count;
            best_next_hop = node.next_hop;
        }
    }

    return best_next_hop;  // Returns 0 if no route found
}

bool NetworkService::UpdateRouteEntry(AddressType source,
                                      AddressType destination,
                                      uint8_t hop_count, uint8_t link_quality,
                                      uint8_t allocated_slots) {
    // Calculate the actual hop count from this node
    // (source's hop count + 1 hop through source)
    uint8_t actual_hop_count = hop_count + 1;

    // Calculate the actual link quality
    // (minimum of reported quality and our link to source)
    uint8_t source_link_quality = CalculateLinkQuality(source);
    uint8_t actual_link_quality = std::min(link_quality, source_link_quality);

    // Don't consider routes that exceed max hops
    if (actual_hop_count > config_.max_hops) {
        return false;
    }

    std::lock_guard<std::mutex> lock(network_mutex_);

    uint32_t current_time = time_provider_->GetCurrentTime();
    bool route_changed = false;

    // Check if this node already exists in our network
    auto node_it = FindNode(destination);
    if (node_it != network_nodes_.end()) {
        // Node exists, check if route is better
        bool route_is_better = false;

        // First, prefer active routes over inactive ones
        if (!node_it->is_active) {
            route_is_better = true;
        }
        // Then, prefer routes with fewer hops
        else if (actual_hop_count < node_it->hop_count) {
            route_is_better = true;
        }
        // If hop counts are equal, prefer better link quality
        else if (actual_hop_count == node_it->hop_count &&
                 actual_link_quality > node_it->link_quality) {
            route_is_better = true;
        }

        if (route_is_better) {
            // Track if next hop changed
            bool next_hop_changed = (node_it->next_hop != source);

            // Update routing information
            node_it->UpdateRouteInfo(source, actual_hop_count,
                                     actual_link_quality, current_time);

            // Update allocated slots
            if (node_it->allocated_slots != allocated_slots) {
                node_it->allocated_slots = allocated_slots;
            }

            route_changed = next_hop_changed;

            // Call the route update callback if provided
            if (route_update_callback_) {
                route_update_callback_(true, destination, source,
                                       actual_hop_count);
            }
        } else {
            // Even if the route isn't better, update the allocated slots
            // This ensures slot allocation info is consistent
            bool slots_changed = (node_it->allocated_slots != allocated_slots);
            if (slots_changed) {
                node_it->allocated_slots = allocated_slots;
                node_it->last_updated = current_time;
                route_changed = true;
            }
        }
    } else {
        // Node doesn't exist, add new node with routing info
        if (WouldExceedLimit()) {
            if (!RemoveOldestNode()) {
                LOG_WARNING("Cannot add node 0x%04X: network full",
                            destination);
                return false;
            }
        }

        // Create new node with routing information
        NetworkNodeRoute new_node;
        new_node.address = destination;
        new_node.next_hop = source;
        new_node.hop_count = actual_hop_count;
        new_node.link_quality = actual_link_quality;
        new_node.last_updated = current_time;
        new_node.last_seen = current_time;
        new_node.is_active = true;
        new_node.allocated_slots = allocated_slots;

        network_nodes_.push_back(new_node);
        route_changed = true;

        LOG_INFO("Added node 0x%04X with route via 0x%04X, hop count %d",
                 destination, source, actual_hop_count);

        // Call the callback if provided
        if (route_update_callback_) {
            route_update_callback_(true, destination, source, actual_hop_count);
        }
    }

    return route_changed;
}

void NetworkService::SetRouteUpdateCallback(RouteUpdateCallback callback) {
    route_update_callback_ = callback;
}

Result NetworkService::StartDiscovery() {
    // Reset discovery state
    network_found_ = false;
    network_creator_ = false;

    // Set protocol state to discovery
    SetState(ProtocolState::DISCOVERY);

    // Reset discovery timer
    uint32_t discovery_start_time = time_provider_->GetCurrentTime();
    uint32_t discovery_end_time =
        discovery_start_time + config_.discovery_timeout_ms;

    LOG_INFO("Starting network discovery, timeout: %d ms",
             config_.discovery_timeout_ms);

    // Protocol specific discovery logic would go here
    // For example, send discovery messages and wait for responses

    // For now, just check if discovery timeout has elapsed
    uint32_t current_time = time_provider_->GetCurrentTime();
    while (current_time < discovery_end_time && !network_found_) {
        // Poll for messages (would be handled by ProcessReceivedMessage)

        // Update current time
        current_time = time_provider_->GetCurrentTime();

        // Yield to other tasks (implementation dependent)
        // For example: std::this_thread::yield();
    }

    // If no network found, create a new one
    if (!network_found_) {
        LOG_INFO("No network found during discovery, creating new network");
        return CreateNetwork();
    } else {
        LOG_INFO("Network found during discovery, joining");
        return JoinNetwork(network_manager_);
    }
}

bool NetworkService::IsNetworkFound() const {
    return network_found_;
}

bool NetworkService::IsNetworkCreator() const {
    return network_creator_;
}

Result NetworkService::ProcessReceivedMessage(const BaseMessage& message) {
    // Process based on message type
    switch (message.GetType()) {
        case MessageType::ROUTE_TABLE:
            return ProcessRoutingTableMessage(message);

        case MessageType::JOIN_REQUEST:
            // Handle join request - implementation dependent
            // return ProcessJoinRequest(message);
            break;

        case MessageType::JOIN_RESPONSE:
            // Handle join response - implementation dependent
            // return ProcessJoinResponse(message);
            break;

        case MessageType::SLOT_REQUEST:
            // Handle slot request - implementation dependent
            // return ProcessSlotRequest(message);
            break;

        case MessageType::SLOT_ALLOCATION:
            // Handle slot allocation - implementation dependent
            // return ProcessSlotAllocation(message);
            break;

        case MessageType::DATA_MSG:
            // Handle data message - implementation dependent
            // return ProcessDataMessage(message);
            break;

        default:
            LOG_WARNING("Unknown message type: %d",
                        static_cast<int>(message.GetType()));
            break;
    }

    return Result::Success();
}

Result NetworkService::NotifySuperframeOfNetworkChanges() {
    // Check if superframe service is available
    if (!superframe_service_) {
        return Result::Success();  // No superframe service, nothing to do
    }

    // Notify superframe of network changes
    // This would integrate with the ISuperframeService API
    // For example:
    // return superframe_service_->UpdateSlotsBasedOnNetwork(network_nodes_);

    return Result::Success();
}

INetworkService::ProtocolState NetworkService::GetState() const {
    return state_;
}

void NetworkService::SetState(ProtocolState state) {
    state_ = state;
    LOG_INFO("Network service state changed to %d", static_cast<int>(state));
}

AddressType NetworkService::GetNetworkManagerAddress() const {
    return network_manager_;
}

void NetworkService::SetNetworkManager(AddressType manager_address) {
    std::lock_guard<std::mutex> lock(network_mutex_);

    if (network_manager_ != manager_address) {
        network_manager_ = manager_address;
        LOG_INFO("Network manager set to 0x%04X", manager_address);

        // Update network manager status for nodes
        for (auto& node : network_nodes_) {
            if (node.address == manager_address) {
                node.is_network_manager = true;
            } else if (node.is_network_manager) {
                node.is_network_manager = false;
            }
        }
    }
}

Result NetworkService::Configure(const NetworkConfig& config) {
    // Validate configuration
    if (config.max_hops == 0 || config.max_hops > 255) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Invalid max_hops value");
    }

    if (config.node_address == 0) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Invalid node_address (0)");
    }

    // Apply configuration
    config_ = config;
    node_address_ = config.node_address;

    LOG_INFO("Network service configured with node address 0x%04X",
             node_address_);

    return Result::Success();
}

const INetworkService::NetworkConfig& NetworkService::GetConfig() const {
    return config_;
}

uint8_t NetworkService::CalculateLinkQuality(AddressType node_address) const {
    std::lock_guard<std::mutex> lock(network_mutex_);

    // Find node
    auto node_it = FindNode(node_address);
    if (node_it != network_nodes_.end() && node_it->IsDirectNeighbor()) {
        // Return link quality for direct neighbors
        return node_it->link_quality;
    }

    // Default link quality for unknown nodes
    return 50;  // 50% quality
}

std::unique_ptr<BaseMessage> NetworkService::CreateRoutingTableMessage(
    AddressType destination) {
    std::lock_guard<std::mutex> lock(network_mutex_);

    // Create entries vector from our NetworkNodeRoute objects
    // This assumes RoutingTableMessage is updated to work with NetworkNodeRoute
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute> entries_to_share;

    // Create map of link qualities to include in message
    std::unordered_map<AddressType, uint8_t> link_qualities;

    for (const auto& node : network_nodes_) {
        if (node.is_active) {
            // For active nodes, include them in routing table
            entries_to_share.push_back(node);

            // If this is a direct neighbor, include our link quality to them
            if (node.IsDirectNeighbor()) {
                link_qualities[node.address] = node.GetLinkQuality();
            }
        }
    }

    // Increment table version
    table_version_ = (table_version_ + 1) % 256;

    // Create routing table message with our network nodes and link qualities
    auto routing_table_msg = RoutingTableMessage::Create(
        destination, node_address_, network_manager_, table_version_,
        entries_to_share);

    if (!routing_table_msg) {
        LOG_ERROR("Failed to create routing table message");
        return nullptr;
    }

    // Return as unique pointer
    return std::make_unique<BaseMessage>(routing_table_msg->ToBaseMessage());
}

bool NetworkService::AddDirectRoute(AddressType neighbor_address,
                                    uint8_t link_quality) {
    // Don't add route to ourselves
    if (neighbor_address == node_address_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(network_mutex_);

    uint32_t current_time = time_provider_->GetCurrentTime();
    auto node_it = FindNode(neighbor_address);
    bool route_changed = false;

    if (node_it != network_nodes_.end()) {
        // Update existing node
        bool was_active = node_it->is_active;
        bool hop_changed = (node_it->hop_count != 1);
        bool next_hop_changed = (node_it->next_hop != neighbor_address);
        bool quality_changed = (node_it->link_quality != link_quality);

        // Update routing information
        node_it->hop_count = 1;                // Direct route
        node_it->next_hop = neighbor_address;  // Next hop is destination
        node_it->link_quality = link_quality;
        node_it->last_updated = current_time;
        node_it->last_seen = current_time;
        node_it->is_active = true;

        route_changed =
            !was_active || hop_changed || next_hop_changed || quality_changed;

        if (route_changed && route_update_callback_) {
            route_update_callback_(true, neighbor_address, neighbor_address, 1);
        }
    } else {
        // Add new node with direct route
        if (WouldExceedLimit()) {
            if (!RemoveOldestNode()) {
                LOG_WARNING("Cannot add node 0x%04X: network full",
                            neighbor_address);
                return false;
            }
        }

        // Create new node with routing information
        NetworkNodeRoute new_node;
        new_node.address = neighbor_address;
        new_node.next_hop = neighbor_address;  // Direct neighbor
        new_node.hop_count = 1;                // Direct route
        new_node.link_quality = link_quality;
        new_node.last_updated = current_time;
        new_node.last_seen = current_time;
        new_node.is_active = true;

        network_nodes_.push_back(new_node);
        route_changed = true;

        LOG_INFO("Added direct route to node 0x%04X, link quality %d",
                 neighbor_address, link_quality);

        if (route_update_callback_) {
            route_update_callback_(true, neighbor_address, neighbor_address, 1);
        }
    }

    if (route_changed) {
        UpdateNetworkTopology();
    }

    return route_changed;
}

Result NetworkService::JoinNetwork(AddressType manager_address) {
    // Set network manager
    SetNetworkManager(manager_address);

    // Set protocol state
    SetState(ProtocolState::JOINING);

    // Update discovery state
    network_found_ = true;
    network_creator_ = false;

    // Set synchronized flag
    is_synchronized_ = true;
    last_sync_time_ = time_provider_->GetCurrentTime();

    LOG_INFO("Joining network with manager 0x%04X", manager_address);

    // Additional join logic would be implemented here
    // For example, send join request message

    // Set state to normal operation once joined
    SetState(ProtocolState::NORMAL_OPERATION);

    return Result::Success();
}

Result NetworkService::CreateNetwork() {
    // Set ourselves as network manager
    SetNetworkManager(node_address_);

    // Set protocol state
    SetState(ProtocolState::NETWORK_MANAGER);

    // Update discovery state
    network_found_ = true;
    network_creator_ = true;

    // Set synchronized flag
    is_synchronized_ = true;
    last_sync_time_ = time_provider_->GetCurrentTime();

    LOG_INFO("Creating new network as manager 0x%04X", node_address_);

    // Notify superframe of network creation
    if (superframe_service_) {
        NotifySuperframeOfNetworkChanges();
    }

    return Result::Success();
}

std::vector<NetworkNodeRoute>::iterator NetworkService::FindNode(
    AddressType node_address) {
    return std::find_if(network_nodes_.begin(), network_nodes_.end(),
                        [node_address](const NetworkNodeRoute& node) {
                            return node.address == node_address;
                        });
}

std::vector<NetworkNodeRoute>::const_iterator NetworkService::FindNode(
    AddressType node_address) const {
    return std::find_if(network_nodes_.begin(), network_nodes_.end(),
                        [node_address](const NetworkNodeRoute& node) {
                            return node.address == node_address;
                        });
}

bool NetworkService::WouldExceedLimit() const {
    return config_.max_network_nodes > 0 &&
           network_nodes_.size() >= config_.max_network_nodes;
}

bool NetworkService::RemoveOldestNode() {
    if (network_nodes_.empty()) {
        return false;
    }

    // Find node with oldest last_seen time
    auto oldest_it = std::min_element(
        network_nodes_.begin(), network_nodes_.end(),
        [](const NetworkNodeRoute& a, const NetworkNodeRoute& b) {
            return a.last_seen < b.last_seen;
        });

    if (oldest_it != network_nodes_.end()) {
        LOG_INFO("Removing oldest node 0x%04X to make space",
                 oldest_it->address);

        // If this was the network manager, we have a problem
        if (oldest_it->is_network_manager) {
            LOG_WARNING("Removing network manager 0x%04X!", oldest_it->address);
        }

        // Call route update callback for removed node
        if (route_update_callback_ && oldest_it->is_active) {
            route_update_callback_(false, oldest_it->address, 0, 0);
        }

        network_nodes_.erase(oldest_it);
        return true;
    }

    return false;
}

void NetworkService::ScheduleRoutingMessageExpectations() {
    std::lock_guard<std::mutex> lock(network_mutex_);

    // For each direct neighbor, expect a routing message
    for (auto& node : network_nodes_) {
        if (node.IsDirectNeighbor()) {
            node.ExpectRoutingMessage();
        }
    }
}

void NetworkService::ResetLinkQualityStats() {
    std::lock_guard<std::mutex> lock(network_mutex_);

    for (auto& node : network_nodes_) {
        node.ResetLinkStats();
    }
}

bool NetworkService::UpdateNetworkTopology(bool notify_superframe) {
    // This would implement network topology updates based on current routes
    // For example, recalculate routes, update metrics, etc.

    // Notify superframe of network changes if requested
    if (notify_superframe) {
        NotifySuperframeOfNetworkChanges();
    }

    return true;
}

uint8_t NetworkService::LinkQualityMetrics::CalculateCombinedQuality() const {
    // Weight the different factors (adjustable)
    constexpr uint16_t RECEPTION_WEIGHT = 50;  // 50%
    constexpr uint16_t SIGNAL_WEIGHT = 30;     // 30%
    constexpr uint16_t STABILITY_WEIGHT = 20;  // 20%

    // Calculate weighted average
    uint16_t combined =
        (reception_ratio * RECEPTION_WEIGHT + signal_strength * SIGNAL_WEIGHT +
         stability * STABILITY_WEIGHT) /
        100;

    return static_cast<uint8_t>(std::min(combined, static_cast<uint16_t>(255)));
}

uint8_t NetworkService::CalculateComprehensiveLinkQuality(
    AddressType node_address) {
    std::lock_guard<std::mutex> lock(network_mutex_);

    auto node_it = FindNode(node_address);
    if (node_it == network_nodes_.end()) {
        return 0;  // Node not found
    }

    // Get base reception statistics
    uint8_t reception_ratio = node_it->link_stats.CalculateQuality();

    // Could be enhanced with signal strength from radio hardware
    uint8_t signal_strength = 200;  // Placeholder, ideally from hardware

    // Calculate stability based on consistency of messages
    uint8_t stability = CalculateLinkStability(*node_it);

    // Combine metrics
    LinkQualityMetrics metrics = {.reception_ratio = reception_ratio,
                                  .signal_strength = signal_strength,
                                  .stability = stability};

    return metrics.CalculateCombinedQuality();
}

uint8_t NetworkService::CalculateLinkStability(const NetworkNodeRoute& node) {
    // Current implementation uses a simple metric based on
    // how long we've known this node and consistent communication

    uint32_t current_time = time_provider_->GetCurrentTime();

    // Time-based factor: newer links are less stable
    uint32_t node_age_ms = current_time - node.last_updated;
    uint8_t age_factor =
        std::min(static_cast<uint32_t>(255),
                 node_age_ms / (60 * 1000));  // Max after 1 minute

    // Message consistency factor
    uint8_t consistency_factor = 0;
    if (node.link_stats.messages_expected > 0) {
        // Variance in reception rate affects stability
        uint32_t expected = node.link_stats.messages_expected;
        uint32_t received = node.link_stats.messages_received;

        // Higher variance = lower consistency
        if (expected >= received) {
            consistency_factor = (255 * received) / expected;
        } else {
            consistency_factor = 255;  // Should not happen, but safe handling
        }
    }

    // Combine factors (equal weight)
    return (age_factor + consistency_factor) / 2;
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher