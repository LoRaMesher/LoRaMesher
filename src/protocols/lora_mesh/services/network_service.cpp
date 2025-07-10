/**
 * @file network_service.cpp
 * @brief Implementation of unified network service combining node management, routing, and discovery
 */

#include "network_service.hpp"
#include <algorithm>
#include <cmath>
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
      discovery_start_time_(0),
      joining_start_time_(0) {

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
    // Don't track our own node unless we're in NORMAL_OPERATION state
    // (which means we've joined a network and need TX/CONTROL_TX slots)
    if (node_address == node_address_) {
        if (state_ != ProtocolState::NORMAL_OPERATION &&
            state_ != ProtocolState::NETWORK_MANAGER) {
            return false;
        }
        // Allow adding local node when in operational states
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

Result NetworkService::ProcessRoutingTableMessage(const BaseMessage& message,
                                                  int32_t reception_timestamp) {
    // Deserialize routing table message
    RoutingTableMessage routing_msg(message);

    auto source = message.GetSource();
    auto network_manager = routing_msg.GetNetworkManager();
    auto table_version = routing_msg.GetTableVersion();
    auto entries = routing_msg.GetEntries();

    LOG_INFO(
        "Received routing table update from 0x%04X: version %d, %zu entries at "
        "timestamp %d",
        source, table_version, entries.size(), reception_timestamp);

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
        last_sync_time_ = reception_timestamp;
    }

    // First, handle the source node as a direct neighbor
    {
        std::lock_guard<std::mutex> lock(network_mutex_);
        auto source_node_it = FindNode(source);

        if (source_node_it != network_nodes_.end()) {
            // Update existing source node
            source_node_it->ReceivedRoutingMessage(
                routing_msg.GetLinkQualityFor(node_address_),
                reception_timestamp);

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
            NetworkNodeRoute new_node(source, battery, reception_timestamp);
            new_node.next_hop = source;
            new_node.routing_entry.hop_count = 1;
            new_node.routing_entry.link_quality = 128;  // Initial quality
            new_node.is_active = true;
            new_node.is_network_manager = (source == network_manager);

            // Register the received message
            new_node.ReceivedRoutingMessage(
                routing_msg.GetLinkQualityFor(node_address_),
                reception_timestamp);

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
                                             actual_link_quality,
                                             reception_timestamp);

            if (!node_it->is_active ||
                potential_route.IsBetterRouteThan(*node_it)) {
                bool changed = node_it->UpdateFromRoutingTableEntry(
                    entry, source, reception_timestamp);

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
            new_node.last_updated = reception_timestamp;
            new_node.last_seen = reception_timestamp;
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

Result NetworkService::StartJoining(AddressType /* manager_address */,
                                    uint32_t join_timeout_ms) {
    // Check if already successfully joined a network (in normal operation)
    if (GetState() == ProtocolState::NORMAL_OPERATION) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Already in a network, cannot join");
    }
    // Set protocol state to JOINING
    SetState(ProtocolState::JOINING);
    network_found_ = true;
    network_creator_ = false;

    // Record discovery start time
    joining_start_time_ = GetRTOS().getTickCount();

    LOG_INFO("Starting network joining, timeout: %d ms, current time: %d ms",
             join_timeout_ms, joining_start_time_);

    // Set power-efficient slot allocation for joining
    Result slot_result = SetJoiningSlots();
    if (!slot_result) {
        LOG_ERROR("Failed to set joining slots: %s",
                  slot_result.GetErrorMessage().c_str());
        return slot_result;
    }

    // Join the network
    return SendJoinRequest(network_manager_, config_.default_data_slots);
}

bool NetworkService::IsNetworkFound() const {
    return network_found_;
}

bool NetworkService::IsNetworkCreator() const {
    return network_creator_;
}

Result NetworkService::ProcessReceivedMessage(const BaseMessage& message,
                                              int32_t reception_timestamp) {
    LOG_INFO(
        "*** RECEIVED MESSAGE: type %d from 0x%04X to 0x%04X (my state: %d, "
        "timestamp: %d) ***",
        static_cast<int>(message.GetType()), message.GetSource(),
        message.GetDestination(), static_cast<int>(state_),
        reception_timestamp);
    // LOG_DEBUG(
    //     "Processing received message type %d from 0x%04X to 0x%04X at "
    //     "timestamp %d",
    //     static_cast<int>(message.GetType()), message.GetSource(),
    //     message.GetDestination(), reception_timestamp);

    // Route message to appropriate handler based on type
    switch (message.GetType()) {
        case MessageType::ROUTE_TABLE:
            return ProcessRoutingTableMessage(message, reception_timestamp);

        case MessageType::JOIN_REQUEST:
            return ProcessJoinRequest(message, reception_timestamp);

        case MessageType::JOIN_RESPONSE:
            return ProcessJoinResponse(message, reception_timestamp);

        case MessageType::SLOT_REQUEST:
            return ProcessSlotRequest(message, reception_timestamp);

        case MessageType::SLOT_ALLOCATION:
            return ProcessSlotAllocation(message, reception_timestamp);

        case MessageType::SYNC_BEACON:
            return ProcessSyncBeacon(message, reception_timestamp);

        case MessageType::DATA_MSG:
            // Data messages handled by upper layers
            LOG_DEBUG("Received DATA_MSG from 0x%04X at timestamp %d",
                      message.GetSource(), reception_timestamp);
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
    if (config.max_hops == 0) {
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
    Result result = UpdateSlotTable();
    if (!result) {
        LOG_ERROR("Failed to update slot table");
        return result;
    }

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

bool NetworkService::UpdateNetworkTopology(bool /* notify_superframe */) {
    //TODO: Could implement additional topology analysis here
    // std::lock_guard<std::mutex> lock(network_mutex_);
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

    // Queue message for transmission using DISCOVERY_TX slot for proper timing
    auto base_msg =
        std::make_unique<BaseMessage>(join_request->ToBaseMessage());
    message_queue_service_->AddMessageToQueue(
        SlotAllocation::SlotType::DISCOVERY_TX, std::move(base_msg));

    LOG_INFO("Join request queued for manager 0x%04X, requesting %d slots",
             manager_address, requested_slots);

    // Debug: Log current slot information for timing verification
    LOG_DEBUG(
        "Join request - Current state: %d, Network manager: 0x%04X, Message "
        "type: %d",
        static_cast<int>(state_), network_manager_,
        static_cast<int>(join_request->ToBaseMessage().GetType()));

    return Result::Success();
}

Result NetworkService::ProcessJoinRequest(const BaseMessage& message,
                                          int32_t /* reception_timestamp */) {
    LOG_INFO(
        "*** PROCESSING JOIN_REQUEST from 0x%04X (state: %d, network_manager: "
        "0x%04X) ***",
        message.GetSource(), static_cast<int>(state_), network_manager_);
    LOG_DEBUG("Processing JOIN_REQUEST from 0x%04X", message.GetSource());

    auto join_request_opt =
        JoinRequestMessage::CreateFromSerialized(*message.Serialize());

    if (!join_request_opt) {
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to deserialize join request");
    }

    auto source = message.GetSource();
    auto next_hop = join_request_opt->GetHeader().GetNextHop();

    // If we're not the network manager and not the intended next hop, forward the message
    if (network_manager_ != node_address_ && next_hop != node_address_) {
        LOG_DEBUG("Forwarding join request from 0x%04X (not for us)", source);
        return ForwardJoinRequest(*join_request_opt);
    }

    // If we're not the network manager but we are the next hop, ignore
    if (network_manager_ != node_address_) {
        LOG_DEBUG("Ignoring join request - not network manager");
        return Result::Success();
    }

    // Now we're the network manager, process the join request
    auto capabilities = join_request_opt->GetCapabilities();
    auto battery_level = join_request_opt->GetBatteryLevel();
    auto requested_slots = join_request_opt->GetRequestedSlots();

    LOG_INFO("Join request from 0x%04X: caps=0x%02X, battery=%d%%, slots=%d",
             source, capabilities, battery_level, requested_slots);

    // Check if a join is already pending for this superframe
    if (pending_join_request_) {
        LOG_INFO(
            "Join request from 0x%04X rejected - join already pending this "
            "superframe",
            source);
        return SendJoinResponse(source, JoinResponseHeader::RETRY_LATER, 0);
    }

    // Determine if node should be accepted
    auto [accepted, allocated_slots] =
        ShouldAcceptJoin(source, capabilities, requested_slots);

    if (!accepted) {
        LOG_INFO("Join request from 0x%04X rejected - network constraints",
                 source);
        return SendJoinResponse(source, JoinResponseHeader::REJECTED, 0);
    }

    // Buffer the join request for next superframe boundary
    pending_join_request_ = true;
    pending_join_data_ = *join_request_opt;

    LOG_INFO(
        "Join request from 0x%04X buffered for next superframe, allocated %d "
        "slots",
        source, allocated_slots);

    // Send immediate acceptance response
    Result result =
        SendJoinResponse(source, JoinResponseHeader::ACCEPTED, allocated_slots);
    if (!result) {
        // Clear pending join if response fails
        pending_join_request_ = false;
        pending_join_data_.reset();
        return result;
    }

    return Result::Success();
}

Result NetworkService::ProcessJoinResponse(const BaseMessage& message,
                                           int32_t /* reception_timestamp */) {
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

        // Move to normal operation first so UpdateNetworkNode allows adding local node
        SetState(ProtocolState::NORMAL_OPERATION);

        // Add ourselves to the network nodes so we get TX and CONTROL_TX slots
        UpdateNetworkNode(node_address_, 100, false, allocated_slots);
        LOG_INFO("Added local node 0x%04X to network for slot allocation",
                 node_address_);

        // Initialize slot table
        Result result = UpdateSlotTable();
        if (!result) {
            return result;
        }

        // Already set to NORMAL_OPERATION above

        // Notify superframe service
        if (superframe_service_) {
            NotifySuperframeOfNetworkChanges();
        }

        LOG_INFO("Successfully joined network 0x%04X", network_id);
    } else if (status == JoinResponseStatus::RETRY_LATER) {
        LOG_INFO("Join request temporarily rejected, retrying after delay");

        // Calculate retry delay based on configuration
        // Approximate superframe duration if service is available
        uint32_t estimated_superframe_duration = 20000;  // Default fallback
        if (superframe_service_) {
            // Estimate based on slot count and duration
            estimated_superframe_duration =
                slot_table_.size() * superframe_service_->GetSlotDuration();
        }
        uint32_t base_delay_ms =
            config_.retry_delay_superframes * estimated_superframe_duration;

        // TODO: Implement retry scheduling mechanism
        // For now, just return to discovery which will eventually retry
        LOG_DEBUG(
            "Retry delay calculated as %u ms, returning to discovery for now",
            base_delay_ms);
        SetState(ProtocolState::DISCOVERY);
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
        SlotAllocation::SlotType::DISCOVERY_TX, std::move(base_msg));

    LOG_INFO("Join response queued for 0x%04X: status=%d, slots=%d", dest,
             static_cast<int>(status), allocated_slots);

    return Result::Success();
}

// Slot management implementations

Result NetworkService::ProcessSlotRequest(const BaseMessage& message,
                                          int32_t /* reception_timestamp */) {
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

Result NetworkService::ProcessSlotAllocation(
    const BaseMessage& message, int32_t /* reception_timestamp */) {
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

    // Use max_hops from received sync beacons
    int max_hops_count = network_max_hops_;

    // Fix slot allocation logic - control and discovery should be calculated separately
    allocated_control_slots_ = network_nodes_.size();
    allocated_discovery_slots_ =
        std::min(max_hops_count,
                 std::max(2, (int)std::ceil(network_nodes_.size() / 3.0)));

    // Add sync beacon slots (1 per hop layer, up to max hops)
    uint8_t sync_beacon_slots =
        std::min(max_hops_count, std::max(1, (int)network_nodes_.size()));

    // Calculate active slots (non-sleep)
    uint8_t total_active_slots = sync_beacon_slots + allocated_control_slots_ +
                                 allocated_discovery_slots_ + total_data_slots;

    // Calculate power-optimized superframe size for 30% duty cycle (70% sleep)
    const float TARGET_DUTY_CYCLE = 0.3f;
    uint8_t total_superframe_slots =
        std::max((uint8_t)std::ceil(total_active_slots / TARGET_DUTY_CYCLE),
                 (uint8_t)(total_active_slots * 2));  // Minimum 50% sleep

    uint8_t sleep_slots = total_superframe_slots - total_active_slots;
    float actual_duty_cycle =
        (float)total_active_slots / total_superframe_slots;

    LOG_DEBUG("Total slots in the superframes %d", total_superframe_slots);
    LOG_DEBUG("Active slots %d: sync %d, control %d, discovery %d, data %d",
              total_active_slots, sync_beacon_slots, allocated_control_slots_,
              allocated_discovery_slots_, total_data_slots);
    LOG_DEBUG("SLEEP slots %d (%.1f%% duty cycle)", sleep_slots,
              actual_duty_cycle * 100.0f);

    slot_table_.resize(total_superframe_slots);

    // Allocate sync beacon slots first (hop-layered allocation)
    size_t slot_index = 0;

    // Determine our hop distance from Network Manager
    uint8_t our_hop_distance = 0;
    {
        std::lock_guard<std::mutex> lock(network_mutex_);
        auto nm_node_it = FindNode(network_manager_);
        if (nm_node_it != network_nodes_.end()) {
            our_hop_distance = nm_node_it->routing_entry.hop_count;
        } else if (network_manager_ == node_address_) {
            our_hop_distance = 0;  // We are the Network Manager
        } else {
            our_hop_distance = 1;  // Default assumption
        }
    }

    // Allocate sync beacon slots based on hop-layered forwarding strategy
    for (size_t hop_layer = 0;
         hop_layer < sync_beacon_slots && slot_index < total_superframe_slots;
         hop_layer++) {
        SlotAllocation sync_slot;
        sync_slot.slot_number = slot_index++;
        sync_slot.target_address = 0xFFFF;  // Broadcast

        if (hop_layer == 0) {
            // Slot 0: Network Manager original transmission
            if (state_ == ProtocolState::NETWORK_MANAGER &&
                network_manager_ == node_address_) {
                sync_slot.type = SlotAllocation::SlotType::SYNC_BEACON_TX;
                LOG_DEBUG(
                    "Allocated slot %zu as SYNC_BEACON_TX for Network Manager "
                    "(hop 0)",
                    hop_layer);
            } else {
                sync_slot.type = SlotAllocation::SlotType::SYNC_BEACON_RX;
                LOG_DEBUG(
                    "Allocated slot %zu as SYNC_BEACON_RX for node (hop 0)",
                    hop_layer);
            }
        } else {
            // Slots 1+: Hop-layered forwarding
            if (our_hop_distance == hop_layer) {
                // This node forwards beacons from previous hop layer
                sync_slot.type = SlotAllocation::SlotType::SYNC_BEACON_TX;
                LOG_DEBUG(
                    "Allocated slot %zu as SYNC_BEACON_TX for hop %d "
                    "forwarding",
                    hop_layer, our_hop_distance);
            } else if (our_hop_distance == hop_layer + 1) {
                // This node receives beacons from this hop layer
                sync_slot.type = SlotAllocation::SlotType::SYNC_BEACON_RX;
                LOG_DEBUG(
                    "Allocated slot %zu as SYNC_BEACON_RX for hop %d reception",
                    hop_layer, our_hop_distance);
            } else {
                // This node sleeps during this hop layer
                sync_slot.type = SlotAllocation::SlotType::SLEEP;
                LOG_DEBUG(
                    "Allocated slot %zu as SLEEP for hop %d (not relevant)",
                    hop_layer, our_hop_distance);
            }
        }

        slot_table_[slot_index - 1] = sync_slot;
    }

    // Allocate control slots with deterministic address-based ordering
    // This ensures all nodes in the network calculate the same slot allocation order
    std::vector<NetworkNodeRoute> ordered_nodes = network_nodes_;
    std::sort(ordered_nodes.begin(), ordered_nodes.end(),
              [](const NetworkNodeRoute& a, const NetworkNodeRoute& b) {
                  // Primary: Network Manager transmits first (has most complete routing info)
                  if (a.is_network_manager != b.is_network_manager) {
                      return a.is_network_manager > b.is_network_manager;
                  }
                  // Secondary: address-based deterministic ordering (same across all nodes)
                  return a.routing_entry.destination <
                         b.routing_entry.destination;
              });

    for (size_t i = 0;
         i < ordered_nodes.size() && slot_index < total_superframe_slots; i++) {
        const auto& node = ordered_nodes[i];
        AddressType addr = node.GetAddress();

        auto& slot_allocation = slot_table_[slot_index];
        slot_allocation.slot_number = slot_index;
        slot_allocation.target_address = addr;

        if (addr == node_address_) {
            // This node's control TX slot for routing table transmission
            if (node.is_active) {
                slot_allocation.type = SlotAllocation::SlotType::CONTROL_TX;
                LOG_DEBUG(
                    "Allocated CONTROL_TX slot %zu for local node 0x%04X "
                    "(NM=%d)",
                    slot_index, addr, node.is_network_manager);
            }
        } else {
            slot_allocation.type = SlotAllocation::SlotType::CONTROL_RX;
            LOG_DEBUG("Allocated CONTROL_RX slot %zu for node 0x%04X (NM=%d)",
                      slot_index, addr, node.is_network_manager);
        }
        slot_index++;
    }

    // Allocate the data slots after control slots
    size_t slot_data_index = slot_index;

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

    // Allocate SLEEP slots for power efficiency
    size_t sleep_slot_index = discovery_slot_index;
    for (size_t i = 0; i < sleep_slots; i++) {
        size_t slot_index = sleep_slot_index + i;
        if (slot_index >= slot_table_.size()) {
            LOG_WARNING("SLEEP slot index %zu out of bounds, skipping",
                        slot_index);
            continue;
        }

        auto& slot = slot_table_[slot_index];
        slot.slot_number = slot_index;
        slot.target_address = 0;  // Not applicable for sleep
        slot.type = SlotAllocation::SlotType::SLEEP;
    }

    LOG_INFO(
        "Updated slot table: %d total (%d active: %d sync + %d ctrl + %d disc "
        "+ %d data, %d sleep, %.1f%% duty cycle)",
        total_superframe_slots, total_active_slots, sync_beacon_slots,
        allocated_control_slots_, allocated_discovery_slots_, total_data_slots,
        sleep_slots, actual_duty_cycle * 100.0f);

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
            INetworkService::kBroadcastAddress;  // Discovery to broadcast
        slot.type = SlotAllocation::SlotType::DISCOVERY_RX;

        slot_table_[i] = slot;
    }

    LOG_INFO("Updated discovery slots to %d", allocated_discovery_slots_);
    return Result::Success();
}

Result NetworkService::SetJoiningSlots() {
    // Use the same slot structure as the network but only listen to necessary slots
    // This ensures synchronization with the network manager's timing

    // First, use the normal slot allocation algorithm to get the network structure
    Result result = UpdateSlotTable();
    if (!result.IsSuccess()) {
        LOG_ERROR("Failed to update slot table for joining: %s",
                  result.GetErrorMessage().c_str());
        return result;
    }

    // Modify slots for joining behavior:
    // - Convert most slots to SLEEP for power efficiency
    // - Keep essential CONTROL_RX slots for routing table updates
    // - Keep DISCOVERY_RX slots for receiving join responses
    // - Add DISCOVERY_TX slot for join requests

    size_t discovery_tx_added = 0;
    size_t active_slots = 0;

    for (auto& slot : slot_table_) {
        switch (slot.type) {
            case SlotAllocation::SlotType::SYNC_BEACON_RX:
            case SlotAllocation::SlotType::SYNC_BEACON_TX:
                // Keep sync beacon slots active for synchronization
                active_slots++;
                break;

            case SlotAllocation::SlotType::CONTROL_RX:
                // Keep control RX slots for join responses and network monitoring
                active_slots++;
                break;

            case SlotAllocation::SlotType::CONTROL_TX:
                // Convert TX slots into RX slots, we want to still listen TX slots.
                slot.type = SlotAllocation::SlotType::CONTROL_RX;
                active_slots++;
                break;

            case SlotAllocation::SlotType::DISCOVERY_RX:
                // Keep discovery RX slots for network monitoring
                active_slots++;
                // Convert first discovery TX slot to send join requests to network manager
                if (discovery_tx_added == 0) {
                    LOG_DEBUG(
                        "Converting slot %d from DISCOVERY_RX to DISCOVERY_TX "
                        "for joining",
                        slot.slot_number);
                    slot.target_address = network_manager_;
                    slot.type = SlotAllocation::SlotType::DISCOVERY_TX;
                    discovery_tx_added++;
                } else {
                    LOG_DEBUG("Keeping slot %d as DISCOVERY_RX for joining",
                              slot.slot_number);
                }
                break;

            case SlotAllocation::SlotType::DISCOVERY_TX:
                // Keep discovery TX slots for waiting join response messages.
                active_slots++;
                break;

            case SlotAllocation::SlotType::TX:
            case SlotAllocation::SlotType::RX:
                // Convert data slots to sleep (no data transmission while joining)
                slot.type = SlotAllocation::SlotType::SLEEP;
                slot.target_address = 0;
                break;

            case SlotAllocation::SlotType::SLEEP:
                // Already sleep, no change
                break;
        }
    }

    float duty_cycle = (float)active_slots / slot_table_.size() * 100.0f;

    LOG_INFO(
        "Set joining slots: %zu active + %zu sleep = %zu total (%.1f%% duty "
        "cycle) - synchronized with network",
        active_slots, slot_table_.size() - active_slots, slot_table_.size(),
        duty_cycle);

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
    uint32_t end_time = joining_start_time_ + timeout_ms;

    // If we're already in JOINING state, just wait for join response or timeout
    if (GetState() == ProtocolState::JOINING) {
        // Check if join timeout has elapsed
        if (current_time >= end_time) {
            LOG_INFO("Join timeout - Fault recovery state");
            SetState(ProtocolState::FAULT_RECOVERY);
            return Result::Success();
        }
    }

    // Still waiting for join response - do nothing, let the protocol continue
    return Result::Success();
}

// Helper method implementations

std::pair<bool, uint8_t> NetworkService::ShouldAcceptJoin(
    AddressType node_address, uint8_t /* capabilities */,
    uint8_t requested_slots) {

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
    bool /* is_network_manager */, uint16_t /* available_data_slots */) {

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
    // TODO: Since we know the number of nodes and the superframe duration
    // Calculate it using 3 superframes
    return 60000;  // TODO: Change to an actual value or configurable.
}

Result NetworkService::ProcessSyncBeacon(const BaseMessage& message,
                                         int32_t reception_timestamp) {
    // Process sync beacons in discovery, joining, normal operation, and network manager states
    if (state_ != ProtocolState::DISCOVERY &&
        state_ != ProtocolState::NORMAL_OPERATION &&
        state_ != ProtocolState::JOINING &&
        state_ != ProtocolState::NETWORK_MANAGER) {
        LOG_DEBUG("Ignoring sync beacon in state %d", static_cast<int>(state_));
        return Result::Success();
    }

    // Deserialize sync beacon message
    auto sync_beacon_opt =
        SyncBeaconMessage::CreateFromSerialized(*message.Serialize());
    if (!sync_beacon_opt.has_value()) {
        LOG_ERROR("Failed to deserialize sync beacon message");
        return Result::Error(LoraMesherErrorCode::kSerializationError);
    }

    const auto& sync_beacon = sync_beacon_opt.value();

    uint32_t unsigned_reception_timestamp =
        static_cast<uint32_t>(reception_timestamp);

    LOG_INFO("Received sync beacon from 0x%04X, hop count %d at timestamp %u",
             sync_beacon.GetSource(), sync_beacon.GetHopCount(),
             unsigned_reception_timestamp);

    // Update network manager from the sync beacon header
    AddressType beacon_nm = sync_beacon.GetNetworkManager();
    if (network_manager_ != beacon_nm) {
        std::lock_guard<std::mutex> lock(network_mutex_);
        network_manager_ = beacon_nm;
        LOG_INFO("Updated network manager to 0x%04X from sync beacon",
                 beacon_nm);
    }

    // Special handling for DISCOVERY state: sync beacon indicates existing network
    if (state_ == ProtocolState::DISCOVERY) {
        auto network_id = sync_beacon.GetNetworkId();
        LOG_INFO("Discovery: Found existing network with id 0x%04X",
                 network_id);
        AddressType source = sync_beacon.GetSource();
        AddressType network_manager = sync_beacon.GetNetworkManager();
        // Add the source as a direct neighbor, but only mark as NM if it actually is
        bool is_network_manager = (source == network_manager);
        UpdateNetworkNode(source, 100, is_network_manager,
                          config_.default_data_slots);
        // Trigger transition to JOINING state
        LOG_INFO("Transitioning from DISCOVERY to JOINING for network 0x%04X",
                 network_id);
        // Get the actual Network Manager from the sync beacon header
        AddressType original_nm = network_manager;

        Result join_result = StartJoining(original_nm, GetJoinTimeout());
        if (!join_result.IsSuccess()) {
            LOG_ERROR("Failed to start joining process: %s",
                      join_result.GetErrorMessage().c_str());
        }
        return join_result;
    }

    // Store max_hops from the sync beacon for slot allocation calculations
    {
        std::lock_guard<std::mutex> lock(network_mutex_);
        uint8_t beacon_max_hops = sync_beacon.GetMaxHops();
        if (beacon_max_hops != network_max_hops_) {
            network_max_hops_ = beacon_max_hops;
            LOG_INFO("Updated network max_hops to %d from sync beacon",
                     network_max_hops_);
        }
    }

    // Update synchronization state
    is_synchronized_ = true;
    last_sync_time_ = unsigned_reception_timestamp;

    // Calculate original timing for synchronization using actual reception timestamp
    // Apply guard time and transmission delay compensation
    uint32_t guard_time_compensation = config_.guard_time_ms;
    uint32_t transmission_delay_compensation =
        sync_beacon.GetTotalSize() * 10;  // Rough estimate: 10ms per byte
    uint32_t total_delay_compensation =
        guard_time_compensation + transmission_delay_compensation;

    uint32_t estimated_nm_time =
        sync_beacon.CalculateOriginalTiming(unsigned_reception_timestamp) -
        total_delay_compensation;

    // Update superframe timing if we have a superframe service
    if (superframe_service_) {
        uint16_t superframe_duration = sync_beacon.GetSuperframeDuration();
        uint8_t total_slots = sync_beacon.GetTotalSlots();
        uint16_t slot_duration = sync_beacon.GetSlotDuration();

        LOG_DEBUG(
            "Sync beacon timing: duration %d ms, slots %d, slot_duration %d ms",
            superframe_duration, total_slots, slot_duration);

        // Update superframe configuration to match network manager
        Result config_result = superframe_service_->UpdateSuperframeConfig(
            total_slots, slot_duration, false);
        if (!config_result.IsSuccess()) {
            LOG_WARNING(
                "Failed to update superframe config from sync beacon: %s",
                config_result.GetErrorMessage().c_str());
        } else {
            LOG_DEBUG("Updated superframe config: %d slots, %d ms per slot",
                      total_slots, slot_duration);
        }

        // Calculate current slot at the Network Manager when the beacon was sent
        uint32_t nm_superframe_elapsed =
            (estimated_nm_time % superframe_duration);
        uint16_t nm_current_slot =
            static_cast<uint16_t>(nm_superframe_elapsed / slot_duration);

        // Synchronize our superframe timing with the Network Manager's timing
        Result sync_result = superframe_service_->SynchronizeWith(
            estimated_nm_time, nm_current_slot);
        if (!sync_result.IsSuccess()) {
            LOG_WARNING("Failed to synchronize superframe timing: %s",
                        sync_result.GetErrorMessage().c_str());
        } else {
            LOG_INFO(
                "Synchronized superframe with Network Manager timing (slot %d)",
                nm_current_slot);
        }
    }

    // Check if we should forward this beacon
    if (ShouldForwardSyncBeacon(sync_beacon)) {
        // Calculate processing delay (time from reception to forwarding)
        // Include guard time and transmission delay
        uint32_t guard_time_delay = config_.guard_time_ms;
        uint32_t transmission_delay =
            sync_beacon.GetTotalSize() * 10;  // Rough estimate: 10ms per byte
        uint32_t processing_delay = 10 + guard_time_delay + transmission_delay;

        Result forward_result =
            ForwardSyncBeacon(sync_beacon, processing_delay);
        if (!forward_result.IsSuccess()) {
            LOG_WARNING("Failed to forward sync beacon: %s",
                        forward_result.GetErrorMessage().c_str());
        }
    }

    return Result::Success();
}

Result NetworkService::SendSyncBeacon() {
    // Only network manager can send original sync beacons
    if (state_ != ProtocolState::NETWORK_MANAGER ||
        network_manager_ != node_address_) {
        LOG_ERROR("Only network manager can send sync beacons");
        return Result::Error(LoraMesherErrorCode::kInvalidState);
    }

    if (!superframe_service_) {
        LOG_ERROR("Superframe service required for sync beacon");
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    // Get actual total slots from the slot table
    uint16_t total_slots = static_cast<uint16_t>(slot_table_.size());
    if (total_slots == 0) {
        total_slots = 20;  // Fallback default
        LOG_WARNING("Slot table empty, using default total slots: %d",
                    total_slots);
    }

    // Create original sync beacon with actual parameters
    // Note: original_timestamp is not used for synchronization (see CalculateOriginalTiming)
    // Synchronization uses reception_time - propagation_delay instead
    auto sync_beacon_opt = SyncBeaconMessage::CreateOriginal(
        0xFFFF,         // Broadcast destination
        node_address_,  // Network manager as source
        1,              // TODO: Get actual network ID
        total_slots,    // Actual total slots from slot table
        static_cast<uint16_t>(superframe_service_->GetSlotDuration()),
        node_address_,  // Network manager address
        0,  // Original timestamp set to 0 (irrelevant for sync - propagation_delay is used)
        network_max_hops_);  // Use actual max hops from network

    if (!sync_beacon_opt.has_value()) {
        LOG_ERROR("Failed to create sync beacon message");
        return Result::Error(LoraMesherErrorCode::kConfigurationError);
    }

    // Convert to base message and queue for transmission
    BaseMessage base_msg = sync_beacon_opt.value().ToBaseMessage();

    // Add to sync beacon TX queue - protocol layer handles actual transmission
    auto base_msg_ptr = std::make_unique<BaseMessage>(std::move(base_msg));
    message_queue_service_->AddMessageToQueue(
        types::protocols::lora_mesh::SlotAllocation::SlotType::SYNC_BEACON_TX,
        std::move(base_msg_ptr));

    LOG_INFO("Queued sync beacon for transmission: %d total slots, %d max hops",
             total_slots, network_max_hops_);
    return Result::Success();
}

Result NetworkService::ForwardSyncBeacon(
    const SyncBeaconMessage& original_beacon, uint32_t processing_delay) {
    // Create forwarded beacon from the original
    auto forwarded_beacon_opt =
        original_beacon.CreateForwardedBeacon(node_address_, processing_delay);

    if (!forwarded_beacon_opt.has_value()) {
        LOG_ERROR("Failed to create forwarded sync beacon");
        return Result::Error(LoraMesherErrorCode::kConfigurationError);
    }

    // Convert to base message and queue for transmission
    BaseMessage base_msg = forwarded_beacon_opt.value().ToBaseMessage();

    // Add to sync beacon TX queue for forwarding
    auto base_msg_ptr = std::make_unique<BaseMessage>(std::move(base_msg));
    message_queue_service_->AddMessageToQueue(
        types::protocols::lora_mesh::SlotAllocation::SlotType::SYNC_BEACON_TX,
        std::move(base_msg_ptr));

    LOG_INFO("Queued forwarded sync beacon for transmission");

    LOG_INFO("Forwarded sync beacon, new hop count %d",
             forwarded_beacon_opt.value().GetHopCount());

    return Result::Success();
}

bool NetworkService::ShouldForwardSyncBeacon(const SyncBeaconMessage& beacon) {
    // Don't forward if we're the network manager (we generate original beacons)
    if (state_ == ProtocolState::NETWORK_MANAGER &&
        network_manager_ == node_address_) {
        return false;
    }

    // Don't forward if hop count would exceed maximum
    if (beacon.GetHopCount() >= beacon.GetMaxHops()) {
        LOG_DEBUG("Not forwarding: hop count %d >= max %d",
                  beacon.GetHopCount(), beacon.GetMaxHops());
        return false;
    }

    // Find our hop distance from the network manager
    uint8_t our_hop_distance = 0;
    {
        std::lock_guard<std::mutex> lock(network_mutex_);
        auto nm_node_it = FindNode(network_manager_);
        if (nm_node_it != network_nodes_.end()) {
            our_hop_distance = nm_node_it->routing_entry.hop_count;
        } else {
            // If we don't know our distance, assume we can forward
            our_hop_distance = 1;
        }
    }

    // Use hop-layered forwarding: only forward if beacon is from previous layer
    bool should_forward = beacon.ShouldBeForwardedBy(our_hop_distance);

    if (should_forward) {
        LOG_DEBUG("Will forward sync beacon: our distance %d, beacon hop %d",
                  our_hop_distance, beacon.GetHopCount());
    } else {
        LOG_DEBUG("Not forwarding: wrong hop layer (our: %d, beacon: %d)",
                  our_hop_distance, beacon.GetHopCount());
    }

    return should_forward;
}

Result NetworkService::HandleSuperframeStart() {
    // Only process if we're in active network states
    if (state_ != ProtocolState::NORMAL_OPERATION &&
        state_ != ProtocolState::NETWORK_MANAGER) {
        return Result::Success();  // Ignore in other states
    }

    // Sync beacon transmission is handled by slot-based processing
    // in ProcessSlotMessages() when SYNC_BEACON_TX slot is encountered.
    // This prevents duplicate transmissions at superframe start.
    if (state_ == ProtocolState::NETWORK_MANAGER &&
        network_manager_ == node_address_) {
        LOG_DEBUG(
            "Network Manager superframe start - sync beacon will be sent in "
            "slot 0");
    } else {
        // Regular nodes listen for sync beacons but don't send them
        LOG_DEBUG("Node listening for sync beacon");
    }

    return Result::Success();
}

Result NetworkService::ApplyPendingJoin() {
    // Only apply if we're the network manager and have a pending join
    if (state_ != ProtocolState::NETWORK_MANAGER ||
        network_manager_ != node_address_ || !pending_join_request_) {
        return Result::Success();
    }

    LOG_INFO(
        "Applying pending join request for node 0x%04X at superframe boundary",
        pending_join_data_->GetSource());

    // Extract join request information
    auto source = pending_join_data_->GetSource();
    auto capabilities = pending_join_data_->GetCapabilities();
    auto battery_level = pending_join_data_->GetBatteryLevel();
    auto requested_slots = pending_join_data_->GetRequestedSlots();

    // Determine allocated slots (re-evaluate to be safe)
    auto [accepted, allocated_slots] =
        ShouldAcceptJoin(source, capabilities, requested_slots);

    if (accepted) {
        // Update network state with the joining node
        LOG_DEBUG("Adding node 0x%04X to network with %d slots", source,
                  allocated_slots);
        UpdateNetworkNode(source, battery_level, false, allocated_slots,
                          capabilities);

        // Update slot allocation (this will be reflected in the sync beacon total_slots field)
        Result result = UpdateSlotTable();
        if (!result) {
            LOG_ERROR("Failed to update slot table for pending join: %s",
                      result.GetErrorMessage().c_str());
            pending_join_request_ = false;
            return result;
        }

        LOG_INFO("Node 0x%04X successfully added to network with %d slots",
                 source, allocated_slots);
    } else {
        LOG_WARNING(
            "Pending join for node 0x%04X no longer accepted due to network "
            "changes",
            source);
    }

    // Clear the pending join flag and data
    pending_join_request_ = false;
    pending_join_data_.reset();

    return Result::Success();
}

Result NetworkService::ForwardJoinRequest(
    const JoinRequestMessage& join_request) {
    // Schedule the next discovery slot for forwarding
    if (!ScheduleDiscoverySlotForwarding()) {
        LOG_WARNING(
            "Failed to schedule discovery slot for join request forwarding");
        return Result(LoraMesherErrorCode::kMemoryError,
                      "No available discovery slots for forwarding");
    }

    // Calculate next hop towards network manager
    AddressType next_hop =
        network_manager_;  // For now, direct to network manager
    // TODO: Implement proper routing to find best next hop

    // Create forwarded join request with updated next_hop
    auto forwarded_request = JoinRequestMessage::Create(
        join_request.GetDestination(),
        node_address_,  // We become the source for forwarding
        join_request.GetCapabilities(), join_request.GetBatteryLevel(),
        join_request.GetRequestedSlots(), {},  // No additional info
        next_hop                               // Set next hop for routing
    );

    if (!forwarded_request) {
        LOG_ERROR("Failed to create forwarded join request");
        return Result(LoraMesherErrorCode::kMemoryError,
                      "Failed to create forwarded join request");
    }

    // Queue for transmission in next available discovery slot
    auto base_msg =
        std::make_unique<BaseMessage>(forwarded_request->ToBaseMessage());
    message_queue_service_->AddMessageToQueue(
        SlotAllocation::SlotType::DISCOVERY_TX, std::move(base_msg));

    LOG_INFO(
        "Forwarded join request from 0x%04X to network manager 0x%04X via "
        "0x%04X",
        join_request.GetSource(), network_manager_, next_hop);

    return Result::Success();
}

bool NetworkService::ScheduleDiscoverySlotForwarding() {
    // Find the next DISCOVERY_RX slot and temporarily convert it to TX
    for (auto& slot : slot_table_) {
        if (slot.type == SlotAllocation::SlotType::DISCOVERY_RX) {
            // Temporarily convert this slot to TX for forwarding
            slot.type = SlotAllocation::SlotType::DISCOVERY_TX;
            slot.target_address = network_manager_;

            LOG_DEBUG("Scheduled discovery slot %d for forwarding to 0x%04X",
                      slot.slot_number, network_manager_);

            // TODO: Implement mechanism to revert slot back to RX after message is sent
            // This could be done through a callback from the message queue service
            // or by scheduling a timer to revert the slot after transmission

            return true;
        }
    }

    LOG_WARNING("No available DISCOVERY_RX slots found for forwarding");
    return false;
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher