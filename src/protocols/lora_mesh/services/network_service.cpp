/**
 * @file network_service.cpp
 * @brief Implementation of unified network service combining node management, routing, and discovery
 */

#include "network_service.hpp"
#include <algorithm>
#include <numeric>

#include "os/os_port.hpp"

namespace {
using namespace loramesher::types::protocols::lora_mesh;
using JoinResponseStatus = loramesher::JoinResponseHeader::ResponseStatus;
}  // namespace

namespace loramesher {
namespace protocols {
namespace lora_mesh {

NetworkService::NetworkService(
    AddressType node_address,
    std::shared_ptr<IMessageQueueService> message_queue_service,
    std::shared_ptr<ISuperframeService> superframe_service)
    : node_address_(node_address),
      message_queue_service_(message_queue_service),
      superframe_service_(superframe_service),
      state_(ProtocolState::INITIALIZING),
      network_manager_(0),
      network_found_(false),
      network_creator_(false),
      is_synchronized_(false),
      last_sync_time_(0),
      table_version_(0),
      discovery_start_time_(0) {

    if (!message_queue_service_) {
        LOG_ERROR("Message queue service is required");
    }

    // Initialize configuration with defaults
    config_.node_address = node_address;
    node_address_ = node_address;
}

bool NetworkService::UpdateNetworkNode(AddressType node_address,
                                       uint8_t battery_level,
                                       bool is_network_manager,
                                       uint8_t allocated_data_slots,
                                       uint8_t capabilities) {
    // Don't track our own node
    if (node_address == node_address_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(network_mutex_);

    uint32_t current_time = GetRTOS().getTickCount();
    auto node_it = FindNode(node_address);

    if (node_it != network_nodes_.end()) {
        // Update existing node
        bool changed = node_it->UpdateNodeInfo(
            battery_level, is_network_manager, capabilities,
            allocated_data_slots, current_time);

        LOG_DEBUG("Updated node 0x%04X in network", node_address);

        if (changed) {
            LOG_INFO(
                "Node 0x%04X updated: battery=%d, manager=%d, "
                "capabilities=0x%02X, data_slots=%d",
                node_address, battery_level, is_network_manager, capabilities,
                allocated_data_slots);

            // If node became network manager, update network manager
            if (is_network_manager) {
                network_manager_ = node_address;
                LOG_INFO("Updated network manager to 0x%04X", node_address);
            }

            // Trigger superframe update if needed
            if (superframe_service_ &&
                (is_network_manager || allocated_data_slots > 0)) {
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
                                  allocated_data_slots);

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
            (is_network_manager || allocated_data_slots > 0)) {
            NotifySuperframeOfNetworkChanges();
        }

        return true;
    }
}

bool NetworkService::UpdateNetwork(uint8_t allocated_control_slots,
                                   uint8_t allocated_discovery_slots) {
    std::lock_guard<std::mutex> lock(network_mutex_);
    bool updated = false;
    if (allocated_control_slots > 0) {
        config_.default_control_slots = allocated_control_slots;
        updated = true;
    }
    if (allocated_discovery_slots > 0) {
        config_.default_discovery_slots = allocated_discovery_slots;
        updated = true;
    }

    return updated;
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

    uint32_t current_time = GetRTOS().getTickCount();
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
    auto current_time = GetRTOS().getTickCount();

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

            // TODO: Network quality, not hops.
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
            uint8_t battery = 100;
            NetworkNodeRoute new_node(source, battery, current_time);
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
                if (entry.allocated_data_slots > 0) {
                    node_it->routing_entry.allocated_data_slots =
                        entry.allocated_data_slots;
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
                                      uint8_t allocated_data_slots) {
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

    uint32_t current_time = GetRTOS().getTickCount();
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

            if (allocated_data_slots !=
                node_it->routing_entry.allocated_data_slots) {
                node_it->routing_entry.allocated_data_slots =
                    allocated_data_slots;
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
                                  actual_link_quality, current_time,
                                  allocated_data_slots);

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

Result NetworkService::StartDiscovery(uint32_t discovery_timeout_ms) {
    // Reset discovery state
    network_found_ = false;
    network_creator_ = false;

    // Set protocol state
    SetState(ProtocolState::DISCOVERY);

    // Set the slot allocation for discovery
    SetDiscoverySlots();

    // Record discovery start time
    discovery_start_time_ = GetRTOS().getTickCount();

    LOG_INFO("Starting network discovery, timeout: %d ms, current time: %d ms",
             discovery_timeout_ms, discovery_start_time_);

    // Start discovery process
    return PerformDiscovery(discovery_timeout_ms);
}

Result NetworkService::StartJoining(AddressType manager_address,
                                    uint32_t join_timeout_ms) {
    // Check if already in a network
    if (network_found_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Already in a network, cannot join");
    }
    // Set protocol state to JOINING
    SetState(ProtocolState::JOINING);
    network_found_ = true;
    network_creator_ = false;

    // Set slot allocation for joining
    UpdateSlotTable();

    // Join the network
    return SendJoinRequest(network_manager_, config_.default_data_slots);
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
            return ProcessJoinRequest(message);

        case MessageType::JOIN_RESPONSE:
            return ProcessJoinResponse(message);

        case MessageType::SLOT_REQUEST:
            return ProcessSlotRequest(message);

        case MessageType::SLOT_ALLOCATION:
            return ProcessSlotAllocation(message);

        case MessageType::DATA_MSG:
            // Data messages handled by upper layers
            LOG_DEBUG("Received DATA_MSG from 0x%04X", message.GetSource());
            // TODO: Forward to application layer
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

    // Generate the current
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

    // Add link qualities for direct neighbors
    for (const auto& node : network_nodes_) {
        if (node.IsDirectNeighbor()) {
            routing_msg.SetLinkQualityFor(node.routing_entry.destination,
                                          node.GetLinkQuality());
        }
    }

    return std::make_unique<BaseMessage>(routing_msg.ToBaseMessage());
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
    last_sync_time_ = GetRTOS().getTickCount();

    LOG_INFO("Joining network with manager 0x%04X", manager_address);

    // Send join request
    return SendJoinRequest(manager_address, config_.default_data_slots);
}

Result NetworkService::SlotTableToSuperframe() {
    if (!superframe_service_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Superframe service not available");
    }

    // TODO: IMPLEMENT THISSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS
    // auto new_superframe = superframe_service_->CreateSuperframe();
    // if (!new_superframe) {
    //     return Result(LoraMesherErrorCode::kMemoryError,
    //                   "Failed to create superframe");
    // }

    // // Notify superframe service
    // return superframe_service_->UpdateSlotTable(std::move(slot_table));
    return Result(LoraMesherErrorCode::kNotImplemented,
                  "Slot table to superframe conversion not implemented yet");
}

Result NetworkService::CreateNetwork() {
    // Pause supeframe_service
    Result result = superframe_service_->StopSuperframe();
    if (!result) {
        LOG_ERROR("Failed to stop superframe while creating network");
        return result;
    }

    // Set ourselves as network manager
    SetNetworkManager(node_address_);

    // Update state
    SetState(ProtocolState::NETWORK_MANAGER);
    network_found_ = true;
    network_creator_ = true;

    // Mark as synchronized
    is_synchronized_ = true;
    last_sync_time_ = GetRTOS().getTickCount();

    LOG_INFO("Created new network as manager 0x%04X", node_address_);

    // Add the network manager node
    NetworkNodeRoute manager_node(node_address_, 100, last_sync_time_, true, 0,
                                  config_.default_data_slots);
    network_nodes_.push_back(manager_node);
    LOG_INFO("Added network manager node 0x%04X", node_address_);

    // Initialize slot table as network manager
    result = UpdateSlotTable();
    if (!result) {
        LOG_ERROR("Failed to update slot table");
        return result;
    }

    // Notify superframe if available
    if (superframe_service_) {
        superframe_service_->SetSynchronized(true);
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
    //TODO: Could implement additional topology analysis here
    std::lock_guard<std::mutex> lock(network_mutex_);
    // Check if we have any nodes

    // Remove inactive nodes
    // size_t removed_count = RemoveInactiveNodes();
    // if (removed_count > 0) {
    //     LOG_INFO("Removed %zu inactive nodes from network", removed_count);
    // }

    // // Update routing entries
    // for (auto& node : network_nodes_) {
    //     node.UpdateRoutingEntries();
    // }

    // Update slot table
    // if (!slot_table_service_) {
    //     LOG_ERROR("Slot table service not available");
    //     return false;
    // }

    // // Notify superframe if requested
    // if (notify_superframe && superframe_service_) {
    //     NotifySuperframeOfNetworkChanges();
    // }

    // TODO: IMPLEMENT THISSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS

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
    uint32_t current_time = GetRTOS().getTickCount();

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

// Join management implementations

Result NetworkService::SendJoinRequest(AddressType manager_address,
                                       uint8_t requested_slots) {
    // Determine node capabilities
    uint8_t capabilities = 0;

    // Set router capability if we have routes
    if (!network_nodes_.empty()) {
        capabilities |= 0x01;  // ROUTER capability
    }

    // Battery level (default to 100%)
    uint8_t battery_level = 100;

    // Create join request message
    auto join_request =
        JoinRequestMessage::Create(manager_address, node_address_, capabilities,
                                   battery_level, requested_slots);

    if (!join_request) {
        return Result(LoraMesherErrorCode::kMemoryError,
                      "Failed to create join request message");
    }

    // Queue message for transmission
    auto base_msg =
        std::make_unique<BaseMessage>(join_request->ToBaseMessage());
    message_queue_service_->AddMessageToQueue(
        SlotAllocation::SlotType::CONTROL_TX, std::move(base_msg));

    LOG_INFO("Join request queued for manager 0x%04X, requesting %d slots",
             manager_address, requested_slots);

    return Result::Success();
}

Result NetworkService::ProcessJoinRequest(const BaseMessage& message) {
    // Only network manager processes join requests
    if (network_manager_ != node_address_) {
        LOG_DEBUG("Ignoring join request - not network manager");
        return Result::Success();
    }

    auto join_request_opt =
        JoinRequestMessage::CreateFromSerialized(*message.Serialize());

    if (!join_request_opt) {
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to deserialize join request");
    }

    auto source = message.GetSource();
    auto capabilities = join_request_opt->GetCapabilities();
    auto battery_level = join_request_opt->GetBatteryLevel();
    auto requested_slots = join_request_opt->GetRequestedSlots();

    LOG_INFO("Join request from 0x%04X: caps=0x%02X, battery=%d%%, slots=%d",
             source, capabilities, battery_level, requested_slots);

    // Determine if node should be accepted
    auto [accepted, allocated_slots] =
        ShouldAcceptJoin(source, capabilities, requested_slots);

    // Update node information
    if (accepted) {
        //TODO: Update Network node
        LOG_ERROR(
            "UpdateNetworkNode not implemented yet, this is a placeholder");
        // UpdateNetworkNode(source, battery_level, false, capabilities,
        //                   allocated_slots);
    }

    // Send response
    JoinResponseStatus status =
        accepted ? JoinResponseStatus::ACCEPTED : JoinResponseStatus::REJECTED;

    Result result = SendJoinResponse(source, status, allocated_slots);
    if (!result) {
        return result;
    }

    // Update network if accepted
    if (accepted) {
        LOG_INFO("Node 0x%04X accepted with %d slots", source, allocated_slots);
        UpdateSlotTable();
        BroadcastSlotAllocation();
    }

    return Result::Success();
}

Result NetworkService::ProcessJoinResponse(const BaseMessage& message) {
    // Only process in joining state
    if (state_ != ProtocolState::JOINING) {
        LOG_DEBUG("Ignoring join response - not in joining state");
        return Result::Success();
    }

    auto join_response_opt =
        JoinResponseMessage::CreateFromSerialized(*message.Serialize());

    if (!join_response_opt) {
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to deserialize join response");
    }

    auto status = join_response_opt->GetStatus();
    auto network_id = join_response_opt->GetNetworkId();
    auto allocated_slots = join_response_opt->GetAllocatedSlots();
    auto source = message.GetSource();

    LOG_INFO("Join response from 0x%04X: status=%d, network=0x%04X, slots=%d",
             source, static_cast<int>(status), network_id, allocated_slots);

    if (status == JoinResponseStatus::ACCEPTED) {
        // Update network state
        SetNetworkManager(source);
        is_synchronized_ = true;
        last_sync_time_ = GetRTOS().getTickCount();

        // Initialize slot table
        Result result = UpdateSlotTable();
        if (!result) {
            return result;
        }

        // Move to normal operation
        SetState(ProtocolState::NORMAL_OPERATION);

        // Notify superframe service
        if (superframe_service_) {
            NotifySuperframeOfNetworkChanges();
        }

        LOG_INFO("Successfully joined network 0x%04X", network_id);
    } else {
        LOG_WARNING("Join rejected with status %d", static_cast<int>(status));

        // Return to discovery
        SetState(ProtocolState::DISCOVERY);
    }

    return Result::Success();
}

Result NetworkService::SendJoinResponse(AddressType dest,
                                        JoinResponseStatus status,
                                        uint8_t allocated_slots) {
    // Only network manager sends join responses
    if (network_manager_ != node_address_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Only network manager can send join responses");
    }

    // Create join response
    auto join_response =
        JoinResponseMessage::Create(dest, node_address_,
                                    network_manager_,  // Network ID
                                    allocated_slots, status);

    if (!join_response) {
        return Result(LoraMesherErrorCode::kMemoryError,
                      "Failed to create join response");
    }

    // Queue for transmission
    auto base_msg =
        std::make_unique<BaseMessage>(join_response->ToBaseMessage());
    message_queue_service_->AddMessageToQueue(
        SlotAllocation::SlotType::CONTROL_TX, std::move(base_msg));

    LOG_INFO("Join response queued for 0x%04X: status=%d, slots=%d", dest,
             static_cast<int>(status), allocated_slots);

    return Result::Success();
}

// Slot management implementations

Result NetworkService::ProcessSlotRequest(const BaseMessage& message) {
    // Only network manager processes slot requests
    if (network_manager_ != node_address_) {
        LOG_DEBUG("Ignoring slot request - not network manager");
        return Result::Success();
    }

    auto slot_request_opt =
        SlotRequestMessage::CreateFromSerialized(*message.Serialize());

    if (!slot_request_opt) {
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to deserialize slot request");
    }

    auto source = message.GetSource();
    auto requested_slots = slot_request_opt->GetRequestedSlots();

    LOG_INFO("Slot request from 0x%04X: %d slots", source, requested_slots);

    // Check if node is known
    bool node_exists = false;
    {
        std::lock_guard<std::mutex> lock(network_mutex_);
        node_exists = (FindNode(source) != network_nodes_.end());
    }

    if (!node_exists) {
        LOG_WARNING("Slot request from unknown node 0x%04X", source);
        return Result::Success();
    }

    // Determine allocation
    uint8_t available_slots =
        config_.max_network_nodes - GetAllocatedDataSlots();
    uint8_t allocated_slots = std::min(requested_slots, available_slots);

    if (allocated_slots > 0) {
        // Update node with new allocation
        UpdateNetworkNode(source, 100, false, 0, allocated_slots);

        // Update network allocation
        Result result = UpdateSlotTable();
        if (!result) {
            return result;
        }
        BroadcastSlotAllocation();

        LOG_INFO("Allocated %d slots to node 0x%04X", allocated_slots, source);
    } else {
        LOG_WARNING("No slots available for node 0x%04X", source);
    }

    return Result::Success();
}

Result NetworkService::ProcessSlotAllocation(const BaseMessage& message) {
    // Only accept from network manager
    if (message.GetSource() != network_manager_) {
        LOG_WARNING("Ignoring slot allocation from non-manager 0x%04X",
                    message.GetSource());
        return Result::Success();
    }

    auto slot_alloc_opt =
        SlotAllocationMessage::CreateFromSerialized(*message.Serialize());

    if (!slot_alloc_opt) {
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to deserialize slot allocation");
    }

    auto network_id = slot_alloc_opt->GetNetworkId();
    auto allocated_slots = slot_alloc_opt->GetAllocatedSlots();
    auto total_nodes = slot_alloc_opt->GetTotalNodes();

    LOG_INFO("Slot allocation: network=0x%04X, slots=%d, nodes=%d", network_id,
             allocated_slots, total_nodes);

    // Reinitialize slot table
    Result result = UpdateSlotTable();
    if (!result) {
        return result;
    }

    // Update synchronization
    is_synchronized_ = true;
    last_sync_time_ = GetRTOS().getTickCount();

    return Result::Success();
}

Result NetworkService::SendSlotRequest(uint8_t num_slots) {
    // Create slot request
    auto slot_request =
        SlotRequestMessage::Create(network_manager_, node_address_, num_slots);

    if (!slot_request) {
        return Result(LoraMesherErrorCode::kMemoryError,
                      "Failed to create slot request");
    }

    // Queue for transmission
    auto base_msg =
        std::make_unique<BaseMessage>(slot_request->ToBaseMessage());
    message_queue_service_->AddMessageToQueue(
        SlotAllocation::SlotType::CONTROL_TX, std::move(base_msg));

    LOG_INFO("Slot request queued for %d slots", num_slots);

    return Result::Success();
}

Result NetworkService::UpdateSlotTable() {
    // Clear existing table
    slot_table_.clear();

    // Get the total data slots allocated inside the network_nodes
    uint8_t total_data_slots = network_nodes_.size() > 0
                                   ? GetAllocatedDataSlots()
                                   : config_.default_data_slots;

    allocated_control_slots_ = allocated_discovery_slots_ =
        network_nodes_.size();

    // Get the total number of slots
    uint8_t total_superframe_slots = allocated_control_slots_ +
                                     allocated_discovery_slots_ +
                                     total_data_slots;

    LOG_DEBUG("Total slots in the superframes %d", total_superframe_slots);
    LOG_DEBUG("Control slots %d, discovery_slots %d, data_slots %d",
              allocated_control_slots_, allocated_discovery_slots_,
              total_data_slots);

    slot_table_.resize(total_superframe_slots);

    // Allocate the control slots in order of network_nodes
    for (size_t i = 0; i < network_nodes_.size(); i++) {
        auto node = network_nodes_[i];
        AddressType addr = node.GetAddress();

        auto& slot_allocation = slot_table_[i];
        slot_allocation.slot_number = i;
        slot_allocation.target_address = addr;

        if (addr != node_address_) {
            slot_allocation.type = SlotAllocation::SlotType::CONTROL_RX;
        } else {
            if (node.is_active) {
                slot_allocation.type = SlotAllocation::SlotType::CONTROL_TX;
            }
        }
    }

    // Allocate the data slots in order of network_nodes
    size_t slot_data_index = allocated_control_slots_;

    for (size_t i = 0; i < network_nodes_.size(); i++) {
        auto& node = network_nodes_[i];
        AddressType addr = node.GetAddress();
        uint8_t slot_data_number = node.GetAllocatedDataSlots();
        for (size_t j = 0; j < slot_data_number; j++) {
            uint8_t slot_index = slot_data_index + j;
            if (slot_index > slot_table_.size()) {
                LOG_WARNING(
                    "Slot index %d out of bounds for node 0x%04X, skipping",
                    slot_index, addr);
                continue;
            }

            // Use reference to modify actual vector element
            auto& slot = slot_table_[slot_index];

            slot.slot_number = slot_index;
            slot.target_address = addr;
            if (addr == node_address_) {
                // If this is our own slot, we can use it for TX
                slot.type = SlotAllocation::SlotType::TX;
            } else if (node.IsDirectNeighbor()) {
                // For other nodes, we use RX
                slot.type = SlotAllocation::SlotType::RX;
            } else {
                slot.type = SlotAllocation::SlotType::SLEEP;
            }
        }
        slot_data_index += slot_data_number;
    }

    // Allocate discovery slots
    size_t discovery_slot_index = slot_data_index;
    for (size_t i = 0; i < allocated_discovery_slots_; i++) {
        if (discovery_slot_index >= slot_table_.size()) {
            LOG_WARNING("Discovery slot index %d out of bounds, skipping",
                        discovery_slot_index);
            continue;
        }

        // Use reference to modify actual vector element
        auto& slot = slot_table_[discovery_slot_index];

        slot.slot_number = discovery_slot_index;
        slot.target_address = 0;  // Discovery
        slot.type = SlotAllocation::SlotType::DISCOVERY_RX;

        discovery_slot_index++;
    }

    if (!superframe_service_) {
        LOG_ERROR("Superframe service not available, cannot update slot table");
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Superframe service not available");
    }

    // Notify superframe service of new slot table
    Result result =
        superframe_service_->UpdateSuperframeConfig(slot_table_.size());
    if (!result) {
        LOG_ERROR("Failed to update superframe service with new slot table");
        return result;
    }

    return Result::Success();
}

Result NetworkService::SetDiscoverySlots() {
    // Clear existing discovery slots
    allocated_discovery_slots_ =
        ISuperframeService::DEFAULT_DISCOVERY_SLOT_COUNT;

    slot_table_.clear();
    slot_table_.resize(allocated_discovery_slots_);
    for (size_t i = 0; i < allocated_discovery_slots_; i++) {
        SlotAllocation slot;
        slot.slot_number = i;
        slot.target_address =
            INetworkService::kBroadcastAddress;  // Discovery to boradcast
        slot.type = SlotAllocation::SlotType::DISCOVERY_RX;

        slot_table_[i] = slot;
    }

    LOG_INFO("Updated discovery slots to %d", allocated_discovery_slots_);
    return Result::Success();
}

Result NetworkService::BroadcastSlotAllocation() {
    // TODO: IMPLEMENT THISSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS
    // Only network manager broadcasts
    // if (network_manager_ != node_address_) {
    //     return Result(LoraMesherErrorCode::kInvalidState,
    //                   "Only network manager can broadcast allocation");
    // }

    // std::lock_guard<std::mutex> lock(network_mutex_);

    // // Get total active nodes
    // uint8_t total_nodes = static_cast<uint8_t>(network_nodes_.size() + 1);

    // // Send allocation to each node
    // for (const auto& node : network_nodes_) {
    //     if (!node.is_active)
    //         continue;

    //     // Create slot allocation message
    //     auto slot_alloc = SlotAllocationMessage::Create(
    //         node.routing_entry.destination, node_address_, network_manager_,
    //         node.routing_entry.allocated_slots, total_nodes);

    //     if (!slot_alloc) {
    //         LOG_ERROR("Failed to create slot allocation for 0x%04X",
    //                   node.routing_entry.destination);
    //         continue;
    //     }

    //     // Queue for transmission
    //     auto base_msg =
    //         std::make_unique<BaseMessage>(slot_alloc->ToBaseMessage());
    //     message_queue_service_->AddMessageToQueue(
    //         SlotAllocation::SlotType::CONTROL_TX, std::move(base_msg));
    // }

    // LOG_INFO("Broadcast slot allocation to %d nodes", total_nodes - 1);

    // return Result::Success();
    return Result(LoraMesherErrorCode::kNotImplemented,
                  "Broadcast slot allocation not implemented yet");
}

// Discovery implementation

Result NetworkService::PerformDiscovery(uint32_t timeout_ms) {
    uint32_t current_time = GetRTOS().getTickCount();
    uint32_t end_time = discovery_start_time_ + timeout_ms;

    // Check if we've already discovered a network
    {
        std::unique_lock<std::mutex> lock(network_mutex_);
        for (const auto& node : network_nodes_) {
            if (node.is_network_manager && node.is_active) {
                network_manager_ = node.routing_entry.destination;
                LOG_INFO("Found existing network with manager 0x%04X",
                         network_manager_);

                lock.unlock();
                return StartJoining(network_manager_, GetJoinTimeout());
            }
        }
        lock.unlock();
    }

    // Check if discovery timeout has elapsed
    if (current_time >= end_time) {
        LOG_INFO("Discovery timeout - creating new network");
        return CreateNetwork();
    }

    // Still discovering - this will be called again
    return Result::Success();
}

Result NetworkService::PerformJoining(uint32_t timeout_ms) {
    uint32_t current_time = GetRTOS().getTickCount();
    uint32_t end_time = GetJoinTimeout() + timeout_ms;

    // Check if we've already discovered a network
    {
        std::unique_lock<std::mutex> lock(network_mutex_);
        for (const auto& node : network_nodes_) {
            if (node.is_network_manager && node.is_active) {
                network_manager_ = node.routing_entry.destination;
                LOG_INFO("Found existing network with manager 0x%04X",
                         network_manager_);

                lock.unlock();
                return StartJoining(network_manager_, GetJoinTimeout());
            }
        }
        lock.unlock();
    }

    // Check if discovery timeout has elapsed
    if (current_time >= end_time) {
        LOG_INFO("Discovery timeout - creating new network");
        return CreateNetwork();
    }

    // Still discovering - this will be called again
    return Result::Success();
}

// Helper method implementations

std::pair<bool, uint8_t> NetworkService::ShouldAcceptJoin(
    AddressType node_address, uint8_t capabilities, uint8_t requested_slots) {

    // Check network capacity
    if (network_nodes_.size() >= config_.max_network_nodes) {
        LOG_WARNING("Network at capacity, rejecting node 0x%04X", node_address);
        return {false, 0};
    }

    // Check available slots
    uint8_t available_slots =
        config_.max_network_nodes - GetAllocatedDataSlots();
    if (available_slots == 0) {
        LOG_WARNING("No slots available, rejecting node 0x%04X", node_address);
        return {false, 0};
    }

    // Allocate requested slots or maximum available
    uint8_t allocated_slots = std::min(requested_slots, available_slots);

    LOG_INFO("Accepting node 0x%04X with %d slots (requested %d)", node_address,
             allocated_slots, requested_slots);

    return {true, allocated_slots};
}

void NetworkService::AllocateDataSlotsBasedOnRouting(
    bool is_network_manager, uint16_t available_data_slots) {

    // const auto& superframe = superframe_service_->GetSuperframeConfig();
    // uint16_t slot_index = 0;

    // // Find available slots and allocate based on routing
    // for (uint16_t i = 0;
    //      i < superframe.total_slots && slot_index < available_data_slots; i++) {
    //     if (slot_table_[i].type == SlotAllocation::SlotType::SLEEP) {
    //         // Allocate as data slot
    //         if (is_network_manager) {
    //             // Network manager uses some slots for TX
    //             slot_table_[i].type = (slot_index % 2 == 0)
    //                                       ? SlotAllocation::SlotType::TX
    //                                       : SlotAllocation::SlotType::RX;
    //         } else {
    //             // Regular nodes mostly receive
    //             slot_table_[i].type = (slot_index % 4 == 0)
    //                                       ? SlotAllocation::SlotType::TX
    //                                       : SlotAllocation::SlotType::RX;
    //         }
    //         slot_index++;
    //     }
    // }

    // TODO: IMPLEMENT THISSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS
}

uint16_t NetworkService::FindNextAvailableSlot(uint16_t start_slot) {
    for (uint16_t i = start_slot; i < slot_table_.size(); i++) {
        if (slot_table_[i].type == SlotAllocation::SlotType::SLEEP) {
            return i;
        }
    }

    // Wrap around search
    for (uint16_t i = 0; i < start_slot; i++) {
        if (slot_table_[i].type == SlotAllocation::SlotType::SLEEP) {
            return i;
        }
    }

    return UINT16_MAX;  // No available slot
}

uint8_t NetworkService::GetAllocatedDataSlots() const {
    std::lock_guard<std::mutex> lock(network_mutex_);

    uint8_t total_allocated = 0;
    for (const auto& node : network_nodes_) {
        if (node.is_active) {
            total_allocated += node.GetAllocatedDataSlots();
        }
    }

    return total_allocated;
}

uint32_t NetworkService::GetJoinTimeout() {
    uint32_t timeout_ms = 0;

    std::lock_guard<std::mutex> lock(network_mutex_);

    for (const auto& node : network_nodes_) {
        timeout_ms += node.GetAllocatedDataSlots() *
                      superframe_service_->GetSlotDuration();
    }

    // TODO: Change this to use the actual Control, discovery and Sleep durations
    timeout_ms += network_nodes_.size() * 3;  // Add Control and Sleep duration.
    return timeout_ms;
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher