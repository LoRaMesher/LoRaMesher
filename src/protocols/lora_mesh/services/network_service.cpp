/**
 * @file network_service.cpp
 * @brief Implementation of unified network service combining node management, routing, and discovery
 */

#include "network_service.hpp"
#include <algorithm>
#include <numeric>

namespace {
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
      superframe_service_(superframe_service),
      state_(ProtocolState::INITIALIZING),
      network_manager_(0),
      network_found_(false),
      network_creator_(false),
      is_synchronized_(false),
      last_sync_time_(0),
      table_version_(0) {

    if (!time_provider_) {
        // Create default time provider if none provided
        time_provider_ = std::make_shared<TimeProvider>();
    }

    if (!message_queue_service_) {
        LOG_ERROR("Message queue service is required");
    }

    // Initialize configuration with defaults
    config_.node_address = node_address;
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
            if (superframe_service_ &&
                (is_network_manager || allocated_slots > 0)) {
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

        // Create new node with NetworkNodeRoute
        NetworkNodeRoute new_node(node_address, battery_level, current_time,
                                  is_network_manager, capabilities,
                                  allocated_slots);

        // For new nodes, assume they're direct neighbors initially
        new_node.next_hop = node_address;
        new_node.routing_entry.hop_count = 1;
        new_node.is_active = true;

        network_nodes_.push_back(new_node);

        LOG_INFO("Added new node 0x%04X to network", node_address);

        // Update network manager if needed
        if (is_network_manager) {
            network_manager_ = node_address;
            LOG_INFO("Updated network manager to 0x%04X", node_address);
        }

        // Trigger superframe update if needed
        if (superframe_service_ &&
            (is_network_manager || allocated_slots > 0)) {
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

    // First, mark routes as inactive if they've timed out
    for (auto& node : network_nodes_) {
        if (node.IsExpired(current_time, config_.route_timeout_ms) &&
            node.is_active) {
            node.is_active = false;

            // Notify of route update
            if (route_update_callback_) {
                route_update_callback_(false, node.routing_entry.destination, 0,
                                       0);
            }

            topology_changed = true;
        }
    }

    // Remove nodes that have been inactive for too long
    auto new_end = std::remove_if(
        network_nodes_.begin(), network_nodes_.end(),
        [this, current_time](const NetworkNodeRoute& node) {
            return node.IsExpired(current_time, config_.node_timeout_ms);
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
    // Deserialize routing table message
    RoutingTableMessage routing_msg(message);

    auto source = message.GetSource();
    auto network_manager = routing_msg.GetNetworkManager();
    auto table_version = routing_msg.GetTableVersion();
    auto entries = routing_msg.GetEntries();
    auto current_time = time_provider_->GetCurrentTime();

    LOG_INFO(
        "Received routing table update from 0x%04X: version %d, %zu entries",
        source, table_version, entries.size());

    // Process routing entries
    bool routing_changed = false;
    bool node_updated = false;

    // Update network manager from routing message
    if (network_manager != network_manager_ && network_manager != 0) {
        std::lock_guard<std::mutex> lock(network_mutex_);
        network_manager_ = network_manager;
        LOG_INFO("Updated network manager to 0x%04X", network_manager);
        routing_changed = true;
    }

    // Update time synchronization if message is from network manager
    if (source == network_manager_) {
        is_synchronized_ = true;
        last_sync_time_ = current_time;
    }

    // First, handle the source node as a direct neighbor
    {
        std::lock_guard<std::mutex> lock(network_mutex_);
        auto source_node_it = FindNode(source);

        if (source_node_it != network_nodes_.end()) {
            // Update existing source node
            source_node_it->ReceivedRoutingMessage(
                routing_msg.GetLinkQualityFor(node_address_), current_time);

            // Ensure source is marked as direct neighbor
            if (source_node_it->routing_entry.hop_count != 1 ||
                source_node_it->next_hop != source) {
                source_node_it->next_hop = source;
                source_node_it->routing_entry.hop_count = 1;
                source_node_it->is_active = true;
                routing_changed = true;
            }

            // Update node info if source is network manager
            if (source == network_manager) {
                source_node_it->is_network_manager = true;
                node_updated = true;
            }
        } else {
            // Add source as new direct neighbor
            NetworkNodeRoute new_node(source, 100, current_time);
            new_node.next_hop = source;
            new_node.routing_entry.hop_count = 1;
            new_node.routing_entry.link_quality = 128;  // Initial quality
            new_node.is_active = true;
            new_node.is_network_manager = (source == network_manager);

            // Register the received message
            new_node.ReceivedRoutingMessage(
                routing_msg.GetLinkQualityFor(node_address_), current_time);

            network_nodes_.push_back(new_node);
            node_updated = true;
            routing_changed = true;

            LOG_INFO("Added new direct neighbor node 0x%04X", source);
        }
    }

    // Process each routing entry from the message
    for (const auto& entry : entries) {
        auto dest = entry.destination;

        // Skip entries for ourselves or invalid addresses
        if (dest == node_address_ || dest == 0) {
            continue;
        }

        // Calculate actual metrics through source
        uint8_t actual_hop_count = entry.hop_count + 1;

        // Get source node's link quality
        uint8_t source_link_quality = 128;  // Default
        {
            std::lock_guard<std::mutex> lock(network_mutex_);
            auto source_node = FindNode(source);
            if (source_node != network_nodes_.end()) {
                source_link_quality = source_node->GetLinkQuality();
            }
        }

        // Link quality is minimum of path quality
        uint8_t actual_link_quality =
            std::min(entry.link_quality, source_link_quality);

        // Don't consider routes that exceed max hops
        if (actual_hop_count > config_.max_hops) {
            continue;
        }

        std::lock_guard<std::mutex> lock(network_mutex_);

        // Check if this node already exists
        auto node_it = FindNode(dest);
        if (node_it != network_nodes_.end()) {
            // Update if this is a better route
            NetworkNodeRoute potential_route(dest, source, actual_hop_count,
                                             actual_link_quality, current_time);

            if (!node_it->is_active ||
                potential_route.IsBetterRouteThan(*node_it)) {
                bool changed = node_it->UpdateFromRoutingTableEntry(
                    entry, source, current_time);

                // Update additional node info if available
                if (entry.allocated_slots > 0) {
                    node_it->routing_entry.allocated_slots =
                        entry.allocated_slots;
                }

                routing_changed |= changed;

                if (changed && route_update_callback_) {
                    route_update_callback_(true, dest, source,
                                           actual_hop_count);
                }
            }
        } else {
            // Add new node
            if (WouldExceedLimit()) {
                if (!RemoveOldestNode()) {
                    continue;
                }
            }

            NetworkNodeRoute new_node;
            new_node.routing_entry = entry;
            new_node.next_hop = source;
            new_node.routing_entry.hop_count = actual_hop_count;
            new_node.routing_entry.link_quality = actual_link_quality;
            new_node.last_updated = current_time;
            new_node.last_seen = current_time;
            new_node.is_active = true;

            network_nodes_.push_back(new_node);
            routing_changed = true;

            LOG_INFO("Added node 0x%04X via 0x%04X, hop count %d", dest, source,
                     actual_hop_count);

            if (route_update_callback_) {
                route_update_callback_(true, dest, source, actual_hop_count);
            }
        }
    }

    // Update network topology if needed
    if (routing_changed || node_updated) {
        UpdateNetworkTopology();
    }

    return Result::Success();
}

Result NetworkService::SendRoutingTableUpdate() {
    // Create routing table message
    auto message = CreateRoutingTableMessage();
    if (!message) {
        return Result(LoraMesherErrorCode::kMemoryError,
                      "Failed to create routing table message");
    }

    // Queue message for transmission
    message_queue_service_->AddMessageToQueue(
        SlotAllocation::SlotType::CONTROL_TX, std::move(message));

    LOG_DEBUG("Routing table update message queued for transmission");
    return Result::Success();
}

AddressType NetworkService::FindNextHop(AddressType destination) const {
    // Self-addressed
    if (destination == node_address_) {
        return node_address_;
    }

    std::lock_guard<std::mutex> lock(network_mutex_);

    // Find best active route
    AddressType best_next_hop = 0;
    uint8_t best_hop_count = UINT8_MAX;
    uint8_t best_link_quality = 0;

    for (const auto& node : network_nodes_) {
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

bool NetworkService::UpdateRouteEntry(AddressType source,
                                      AddressType destination,
                                      uint8_t hop_count, uint8_t link_quality,
                                      uint8_t allocated_slots) {
    // Calculate actual metrics
    uint8_t actual_hop_count = hop_count + 1;

    // Get source link quality
    uint8_t source_link_quality = CalculateComprehensiveLinkQuality(source);
    uint8_t actual_link_quality = std::min(link_quality, source_link_quality);

    // Check hop limit
    if (actual_hop_count > config_.max_hops) {
        return false;
    }

    std::lock_guard<std::mutex> lock(network_mutex_);

    uint32_t current_time = time_provider_->GetCurrentTime();
    bool route_changed = false;

    // Find or create node
    auto node_it = FindNode(destination);
    if (node_it != network_nodes_.end()) {
        // Update existing node if route is better
        NetworkNodeRoute potential_route(destination, source, actual_hop_count,
                                         actual_link_quality, current_time);

        if (!node_it->is_active ||
            potential_route.IsBetterRouteThan(*node_it)) {
            route_changed = node_it->UpdateRouteInfo(
                source, actual_hop_count, actual_link_quality, current_time);

            if (allocated_slots != node_it->routing_entry.allocated_slots) {
                node_it->routing_entry.allocated_slots = allocated_slots;
                route_changed = true;
            }

            if (route_changed && route_update_callback_) {
                route_update_callback_(true, destination, source,
                                       actual_hop_count);
            }
        }
    } else {
        // Add new node
        if (WouldExceedLimit()) {
            if (!RemoveOldestNode()) {
                LOG_WARNING("Cannot add node 0x%04X: network full",
                            destination);
                return false;
            }
        }

        NetworkNodeRoute new_node(destination, source, actual_hop_count,
                                  actual_link_quality, current_time);
        new_node.routing_entry.allocated_slots = allocated_slots;

        network_nodes_.push_back(new_node);
        route_changed = true;

        LOG_INFO("Added node 0x%04X with route via 0x%04X, hop count %d",
                 destination, source, actual_hop_count);

        if (route_update_callback_) {
            route_update_callback_(true, destination, source, actual_hop_count);
        }
    }

    if (route_changed) {
        UpdateNetworkTopology();
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

    // Set protocol state
    SetState(ProtocolState::DISCOVERY);

    // Start discovery timer
    uint32_t discovery_start_time = time_provider_->GetCurrentTime();

    LOG_INFO("Starting network discovery, timeout: %d ms",
             config_.discovery_timeout_ms);

    // In a real implementation, this would:
    // 1. Listen for beacons/routing messages
    // 2. Check if any network managers exist
    // 3. Wait for discovery timeout

    // For now, we'll check if we already know about a network manager
    {
        std::lock_guard<std::mutex> lock(network_mutex_);
        for (const auto& node : network_nodes_) {
            if (node.is_network_manager && node.is_active) {
                network_found_ = true;
                network_manager_ = node.routing_entry.destination;
                break;
            }
        }
    }

    // If network found, join it
    if (network_found_) {
        LOG_INFO("Network found with manager 0x%04X, joining",
                 network_manager_);
        return JoinNetwork(network_manager_);
    }

    // Otherwise, create new network
    LOG_INFO("No network found, creating new network");
    return CreateNetwork();
}

bool NetworkService::IsNetworkFound() const {
    return network_found_;
}

bool NetworkService::IsNetworkCreator() const {
    return network_creator_;
}

Result NetworkService::ProcessReceivedMessage(const BaseMessage& message) {
    // Route message to appropriate handler based on type
    switch (message.GetType()) {
        case MessageType::ROUTE_TABLE:
            return ProcessRoutingTableMessage(message);

        case MessageType::JOIN_REQUEST:
            // TODO: Implement join request processing
            LOG_DEBUG("Received JOIN_REQUEST from 0x%04X", message.GetSource());
            break;

        case MessageType::JOIN_RESPONSE:
            // TODO: Implement join response processing
            LOG_DEBUG("Received JOIN_RESPONSE from 0x%04X",
                      message.GetSource());
            break;

        case MessageType::SLOT_REQUEST:
            // TODO: Implement slot request processing
            LOG_DEBUG("Received SLOT_REQUEST from 0x%04X", message.GetSource());
            break;

        case MessageType::SLOT_ALLOCATION:
            // TODO: Implement slot allocation processing
            LOG_DEBUG("Received SLOT_ALLOCATION from 0x%04X",
                      message.GetSource());
            break;

        case MessageType::DATA_MSG:
            // Data messages handled by upper layers
            LOG_DEBUG("Received DATA_MSG from 0x%04X", message.GetSource());
            break;

        default:
            LOG_WARNING("Unknown message type: %d",
                        static_cast<int>(message.GetType()));
            return Result(LoraMesherErrorCode::kInvalidParameter,
                          "Unknown message type");
    }

    return Result::Success();
}

Result NetworkService::NotifySuperframeOfNetworkChanges() {
    if (!superframe_service_) {
        return Result::Success();  // No superframe service
    }

    // TODO: Implement superframe notification
    // This would update slot allocations based on network topology

    LOG_DEBUG("Notifying superframe service of network changes");
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
            node.is_network_manager =
                (node.routing_entry.destination == manager_address);
        }
    }
}

Result NetworkService::Configure(const NetworkConfig& config) {
    // Validate configuration
    if (config.max_hops == 0 || config.max_hops > 255) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Invalid max_hops");
    }

    if (config.node_address == 0) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Invalid node address");
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

    auto node_it = FindNode(node_address);
    if (node_it != network_nodes_.end()) {
        return node_it->GetLinkQuality();
    }

    return 0;  // Unknown node
}

std::unique_ptr<BaseMessage> NetworkService::CreateRoutingTableMessage(
    AddressType destination) {
    std::lock_guard<std::mutex> lock(network_mutex_);

    // Create entries from active routes
    std::vector<RoutingTableEntry> entries;

    for (const auto& node : network_nodes_) {
        if (node.is_active && node.routing_entry.destination != node_address_) {
            entries.push_back(node.ToRoutingTableEntry());
        }
    }

    // Increment table version
    table_version_ = (table_version_ + 1) % 256;

    // Create message
    auto routing_msg_opt = RoutingTableMessage::Create(
        destination, node_address_, network_manager_, table_version_, entries);

    if (!routing_msg_opt) {
        LOG_ERROR("Failed to create routing table message");
        return nullptr;
    }

    RoutingTableMessage routing_msg = std::move(routing_msg_opt.value());

    Result result = Result::Success();

    // Add link qualities for direct neighbors
    for (const auto& node : network_nodes_) {
        if (node.IsDirectNeighbor()) {
            result = routing_msg.SetLinkQualityFor(
                node.routing_entry.destination, node.GetLinkQuality());
            if (!result) {
                LOG_ERROR("Failed to set link quality for node 0x%04X: %s",
                          node.routing_entry.destination,
                          result.GetErrorMessage().c_str());
            }
        }
    }

    return std::make_unique<BaseMessage>(routing_msg.ToBaseMessage());
}

bool NetworkService::AddDirectRoute(AddressType neighbor_address,
                                    uint8_t link_quality) {
    if (neighbor_address == node_address_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(network_mutex_);

    uint32_t current_time = time_provider_->GetCurrentTime();
    auto node_it = FindNode(neighbor_address);
    bool route_changed = false;

    if (node_it != network_nodes_.end()) {
        // Update existing node as direct neighbor
        route_changed = node_it->UpdateRouteInfo(neighbor_address, 1,
                                                 link_quality, current_time);

        if (route_changed && route_update_callback_) {
            route_update_callback_(true, neighbor_address, neighbor_address, 1);
        }
    } else {
        // Add new direct neighbor
        if (WouldExceedLimit()) {
            if (!RemoveOldestNode()) {
                LOG_WARNING("Cannot add node 0x%04X: network full",
                            neighbor_address);
                return false;
            }
        }

        NetworkNodeRoute new_node(neighbor_address, neighbor_address, 1,
                                  link_quality, current_time);

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

    // Update state
    SetState(ProtocolState::JOINING);
    network_found_ = true;
    network_creator_ = false;

    // Mark as synchronized
    is_synchronized_ = true;
    last_sync_time_ = time_provider_->GetCurrentTime();

    LOG_INFO("Joining network with manager 0x%04X", manager_address);

    // TODO: Send join request message

    // For now, move directly to normal operation
    SetState(ProtocolState::NORMAL_OPERATION);

    return Result::Success();
}

Result NetworkService::CreateNetwork() {
    // Set ourselves as network manager
    SetNetworkManager(node_address_);

    // Update state
    SetState(ProtocolState::NETWORK_MANAGER);
    network_found_ = true;
    network_creator_ = true;

    // Mark as synchronized
    is_synchronized_ = true;
    last_sync_time_ = time_provider_->GetCurrentTime();

    LOG_INFO("Created new network as manager 0x%04X", node_address_);

    // Notify superframe if available
    if (superframe_service_) {
        NotifySuperframeOfNetworkChanges();
    }

    return Result::Success();
}

std::vector<NetworkNodeRoute>::iterator NetworkService::FindNode(
    AddressType node_address) {
    return std::find_if(network_nodes_.begin(), network_nodes_.end(),
                        [node_address](const NetworkNodeRoute& node) {
                            return node.routing_entry.destination ==
                                   node_address;
                        });
}

std::vector<NetworkNodeRoute>::const_iterator NetworkService::FindNode(
    AddressType node_address) const {
    return std::find_if(network_nodes_.begin(), network_nodes_.end(),
                        [node_address](const NetworkNodeRoute& node) {
                            return node.routing_entry.destination ==
                                   node_address;
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

    // Find oldest non-manager node
    auto oldest_it = network_nodes_.end();
    uint32_t oldest_time = UINT32_MAX;

    for (auto it = network_nodes_.begin(); it != network_nodes_.end(); ++it) {
        // Don't remove network manager
        if (!it->is_network_manager && it->last_seen < oldest_time) {
            oldest_time = it->last_seen;
            oldest_it = it;
        }
    }

    if (oldest_it != network_nodes_.end()) {
        LOG_INFO("Removing oldest node 0x%04X to make space",
                 oldest_it->routing_entry.destination);

        if (route_update_callback_ && oldest_it->is_active) {
            route_update_callback_(false, oldest_it->routing_entry.destination,
                                   0, 0);
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
    // Could implement additional topology analysis here

    // Notify superframe if requested
    if (notify_superframe && superframe_service_) {
        NotifySuperframeOfNetworkChanges();
    }

    return true;
}

uint8_t NetworkService::LinkQualityMetrics::CalculateCombinedQuality() const {
    // Weighted average of metrics
    constexpr uint16_t RECEPTION_WEIGHT = 50;  // 50%
    constexpr uint16_t SIGNAL_WEIGHT = 30;     // 30%
    constexpr uint16_t STABILITY_WEIGHT = 20;  // 20%

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

    // Signal strength would come from radio hardware
    uint8_t signal_strength = 200;  // Placeholder

    // Calculate stability
    uint8_t stability = CalculateLinkStability(*node_it);

    // Combine metrics
    LinkQualityMetrics metrics = {reception_ratio, signal_strength, stability};

    return metrics.CalculateCombinedQuality();
}

uint8_t NetworkService::CalculateLinkStability(const NetworkNodeRoute& node) {
    uint32_t current_time = time_provider_->GetCurrentTime();

    // Time-based stability
    uint32_t node_age_ms = current_time - node.last_updated;
    uint8_t age_factor =
        std::min(static_cast<uint32_t>(255),
                 node_age_ms / (60 * 1000));  // Max after 1 minute

    // Message consistency
    uint8_t consistency_factor = 0;
    if (node.link_stats.messages_expected > 0) {
        uint32_t expected = node.link_stats.messages_expected;
        uint32_t received = node.link_stats.messages_received;

        if (expected >= received) {
            consistency_factor = (255 * received) / expected;
        } else {
            consistency_factor = 255;
        }
    }

    // Average of factors
    return (age_factor + consistency_factor) / 2;
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher