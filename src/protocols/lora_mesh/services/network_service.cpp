/**
 * @file network_service.cpp
 * @brief Implementation of unified network service combining node management, routing, and discovery
 */

#include "network_service.hpp"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <numeric>
#include <set>

#include "os/os_port.hpp"
#include "protocols/lora_mesh/interfaces/i_routing_table.hpp"
#include "protocols/lora_mesh/routing/distance_vector_routing_table.hpp"

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
    std::shared_ptr<ISuperframeService> superframe_service,
    std::shared_ptr<hardware::IHardwareManager> hardware_manager,
    std::unique_ptr<IRoutingTable> routing_table)
    : node_address_(node_address),
      message_queue_service_(message_queue_service),
      superframe_service_(superframe_service),
      hardware_manager_(hardware_manager),
      routing_table_(std::move(routing_table)),
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

    // Create default routing table if none provided
    if (!routing_table_) {
        routing_table_ = CreateDistanceVectorRoutingTable(node_address_);
        LOG_DEBUG(
            "Created default distance-vector routing table for node 0x%04X",
            node_address_);
    }

    // Set up routing table callback
    routing_table_->SetRouteUpdateCallback(
        [this](bool updated, AddressType dest, AddressType next_hop,
               uint8_t hops) {
            if (route_update_callback_) {
                route_update_callback_(updated, dest, next_hop, hops);
            }
        });

    // Initialize configuration with defaults
    config_.node_address = node_address;
    node_address_ = node_address;

    // slot_table_ is a fixed-size array; no reserve needed
}

bool NetworkService::UpdateNetworkNode(AddressType node_address,
                                       uint8_t battery_level,
                                       bool is_network_manager,
                                       uint8_t allocated_data_slots,
                                       uint8_t capabilities) {
    // Allow updating local node properties (especially capabilities) at any time
    // The local node won't be advertised to others (GetRoutingEntries excludes it)
    // This ensures SetNodeCapabilities() works regardless of network state

    std::lock_guard<std::mutex> lock(network_mutex_);

    uint32_t current_time = GetRTOS().getTickCount();

    // Try to update existing node first
    bool changed = routing_table_->UpdateNode(
        node_address, battery_level, is_network_manager, allocated_data_slots,
        capabilities, current_time);

    if (changed) {
        LOG_INFO(
            "Node 0x%04X updated: battery=%d, manager=%d, "
            "capabilities=0x%02X, data_slots=%d",
            node_address, battery_level, is_network_manager, capabilities,
            allocated_data_slots);

        slot_table_dirty_ = true;

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
    return routing_table_->IsNodePresent(node_address);
}

const std::vector<NetworkNodeRoute>& NetworkService::GetNetworkNodes() const {
    // Note: Caller must be careful with concurrent access
    return routing_table_->GetNodes();
}

std::vector<NetworkNodeRoute> NetworkService::GetNetworkNodesCopy() const {
    return routing_table_->GetNodesCopy();
}

size_t NetworkService::GetNetworkSize() const {
    std::lock_guard<std::mutex> lock(network_mutex_);
    return routing_table_->GetSize();
}

size_t NetworkService::RemoveInactiveNodes() {
    std::lock_guard<std::mutex> lock(network_mutex_);

    uint32_t current_time = GetRTOS().getTickCount();

    // Delegate to routing table implementation
    size_t nodes_removed = routing_table_->RemoveInactiveNodes(
        current_time, config_.route_timeout_ms, config_.node_timeout_ms);

    // Update topology if any nodes were removed
    if (nodes_removed > 0) {
        LOG_INFO("Removed %zu inactive nodes from routing table",
                 nodes_removed);
        slot_table_dirty_ = true;
        UpdateNetworkTopology();
    }

    return nodes_removed;
}

Result NetworkService::ProcessRoutingTableMessage(
    const BaseMessage& message, uint32_t reception_timestamp) {
    // Deserialize routing table message
    auto routing_msg_opt = RoutingTableMessage::CreateFromBaseMessage(message);
    if (!routing_msg_opt.has_value()) {
        LOG_ERROR("Failed to deserialize routing table from 0x%04X",
                  message.GetSource());
        return Result::Error(LoraMesherErrorCode::kSerializationError);
    }
    const auto& routing_msg = routing_msg_opt.value();

    auto source = message.GetSource();
    auto network_manager = routing_msg.GetNetworkManager();
    auto table_version = routing_msg.GetTableVersion();
    auto entries = routing_msg.GetEntries();
    uint8_t source_capabilities = routing_msg.GetSourceCapabilities();
    uint8_t source_allocated_data_slots =
        routing_msg.GetSourceAllocatedDataSlots();

    LOG_INFO(
        "Received routing table update from 0x%04X: version %d, %zu entries at "
        "timestamp %u (caps: 0x%02X, data_slots: %d)",
        source, table_version, entries.size(), reception_timestamp,
        source_capabilities, source_allocated_data_slots);

    bool routing_changed = false;

    // Update network manager from routing message only when this node has no
    // NM yet (network_manager_ == 0).  Once any non-zero value is set — either
    // from a SYNC_BEACON or from a previous routing table — SYNC_BEACON is the
    // authoritative source for all subsequent updates.  Accepting routing-table
    // NM updates in NORMAL_OPERATION, JOINING, or FAULT_RECOVERY would allow
    // cross-network tables (delivered after bridging two networks) to silently
    // corrupt network_manager_, causing JOIN_REQUEST to go to the wrong NM or
    // SYNC_BEACON_TX to flip to SYNC_BEACON_RX.
    if (network_manager_ == 0 && network_manager != 0) {
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

    // Get local link quality to the source (0 = peer doesn't list us as direct)
    uint8_t local_link_quality =
        routing_msg.GetReceptionQualityFor(node_address_);
    LOG_DEBUG("Remote link quality from 0x%04X for us (0x%04X): %d", source,
              node_address_, local_link_quality);

    // Delegate routing table processing to the routing table implementation
    bool routes_updated = routing_table_->ProcessRoutingTableMessage(
        source, entries, reception_timestamp, local_link_quality,
        config_.max_hops, source_capabilities, source_allocated_data_slots);

    routing_changed |= routes_updated;

    // Propagate control_slot_index from each received entry into the routing
    // table.  This lets any node reconstruct the full TDMA schedule if it
    // wins an election.
    for (const auto& entry : entries) {
        if (entry.control_slot_index != 0xFF) {
            routing_table_->SetControlSlotIndex(entry.destination,
                                                entry.control_slot_index);
        }
    }

    // Update network topology if needed
    if (routing_changed) {
        slot_table_dirty_ = true;
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

bool NetworkService::IsTDMANeighbor(AddressType address) const {
    for (size_t i = 0; i < slot_count_; ++i) {
        if (slot_table_[i].type == SlotAllocation::SlotType::RX &&
            slot_table_[i].target_address == address) {
            return true;
        }
    }
    return false;
}

AddressType NetworkService::FindNextHop(AddressType destination) const {
    std::lock_guard<std::mutex> lock(network_mutex_);

    AddressType best = routing_table_->FindNextHop(destination);

    // No slot table yet (discovery phase) — use routing table as-is
    if (slot_count_ == 0 || best == 0) {
        return best;
    }

    // Best next_hop is reachable: TDMA neighbor with confirmed bidirectional
    // link (or too few messages to determine directionality yet)
    if (IsTDMANeighbor(best) && !routing_table_->HasUnidirectionalRisk(best)) {
        return best;
    }

    // Best next_hop is NOT reachable — either not a TDMA neighbor (overheard
    // from outside the network) or confirmed unidirectional (they cannot
    // hear us). Find the best route among reachable next_hops.
    auto nodes = routing_table_->GetNodesCopy();
    AddressType fallback = 0;
    uint16_t best_cost = UINT16_MAX;
    uint8_t best_hops = UINT8_MAX;

    for (const auto& node : nodes) {
        if (node.routing_entry.destination == destination && node.is_active &&
            IsTDMANeighbor(node.next_hop) &&
            !routing_table_->HasUnidirectionalRisk(node.next_hop)) {
            uint16_t cost = types::protocols::lora_mesh::NetworkNodeRoute::
                CalculateRouteCost(node.routing_entry.hop_count,
                                   node.routing_entry.link_quality);
            if (cost < best_cost ||
                (cost == best_cost &&
                 node.routing_entry.hop_count < best_hops)) {
                best_cost = cost;
                best_hops = node.routing_entry.hop_count;
                fallback = node.next_hop;
            }
        }
    }

    if (fallback != 0) {
        LOG_WARNING(
            "Next hop 0x%04X for dest 0x%04X is unreachable "
            "(non-TDMA or unidirectional), using 0x%04X instead",
            best, destination, fallback);
        return fallback;
    }

    // No valid alternative exists — use the original route as last resort.
    LOG_WARNING(
        "Next hop 0x%04X for dest 0x%04X is unreachable "
        "and no valid alternative found, using as last resort",
        best, destination);
    return best;
}

bool NetworkService::UpdateRouteEntry(AddressType source,
                                      AddressType destination,
                                      uint8_t hop_count, uint8_t link_quality,
                                      uint8_t allocated_data_slots,
                                      uint8_t capabilities) {
    // Add 1 to hop count to account for the hop through this node
    uint8_t actual_hop_count = hop_count + 1;

    // Check hop limit
    if (actual_hop_count > config_.max_hops) {
        return false;
    }

    std::lock_guard<std::mutex> lock(network_mutex_);

    uint32_t current_time = GetRTOS().getTickCount();

    // Delegate to routing table implementation
    bool route_changed = routing_table_->UpdateRoute(
        source, destination, actual_hop_count, link_quality,
        allocated_data_slots, capabilities, current_time);

    if (route_changed) {
        UpdateNetworkTopology();
    }

    return route_changed;
}

void NetworkService::SetRouteUpdateCallback(RouteUpdateCallback callback) {
    route_update_callback_ = callback;
}

void NetworkService::SetDataReceivedCallback(DataReceivedCallback callback) {
    data_received_callback_ = callback;
}

void NetworkService::SetLocalNodeCapabilities(uint8_t capabilities) {
    std::lock_guard<std::mutex> lock(network_mutex_);

    // Check if capabilities actually changed
    if (local_capabilities_ == capabilities) {
        return;  // No change
    }

    // Store local capabilities in dedicated field
    local_capabilities_ = capabilities;

    LOG_INFO("Updated local node capabilities to 0x%02X", capabilities);
}

uint8_t NetworkService::GetLocalNodeCapabilities() const {
    std::lock_guard<std::mutex> lock(network_mutex_);
    // Return from dedicated field, not routing table
    return local_capabilities_;
}

void NetworkService::SetLocalAllocatedDataSlots(uint8_t data_slots) {
    std::lock_guard<std::mutex> lock(network_mutex_);

    // Check if data slots actually changed
    if (local_allocated_data_slots_ == data_slots) {
        return;  // No change
    }

    local_allocated_data_slots_ = data_slots;
    slot_table_dirty_ = true;
    LOG_INFO("Updated local node data slots to %d", data_slots);
}

uint8_t NetworkService::GetNodeCapabilities(AddressType node_address) const {
    std::lock_guard<std::mutex> lock(network_mutex_);

    // Check if requesting local node capabilities
    if (node_address == node_address_) {
        return local_capabilities_;
    }

    // Search routing table for other nodes
    const auto& nodes = routing_table_->GetNodes();
    for (const auto& node : nodes) {
        if (node.GetAddress() == node_address) {
            return node.routing_entry.capabilities;
        }
    }

    return 0;  // Node not found
}

Result NetworkService::StartDiscovery(uint32_t discovery_timeout_ms) {
    // Reset discovery state
    network_found_ = false;
    network_creator_ = false;

    // Clear any previous sponsor selection for fresh discovery
    if (selected_sponsor_ != 0) {
        LOG_INFO("Clearing previous sponsor 0x%04X for fresh discovery",
                 selected_sponsor_);
        selected_sponsor_ = 0;
    }

    // Handle NETWORK_MANAGER role - skip discovery, create network immediately
    if (node_role_ == NodeRole::NETWORK_MANAGER) {
        LOG_INFO("Node role is NETWORK_MANAGER, creating network immediately");
        return CreateNetwork();
    }

    // Set protocol state
    SetState(ProtocolState::DISCOVERY);

    // Set the slot allocation for discovery
    SetDiscoverySlots();

    // Record discovery start time
    discovery_start_time_ = GetRTOS().getTickCount();
    nm_election_start_time_ = 0;

    LOG_INFO("Starting network discovery, timeout: %d ms, current time: %d ms",
             discovery_timeout_ms, discovery_start_time_);

    // Start discovery process
    // return PerformDiscovery(discovery_timeout_ms);
    return Result::Success();
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

    // Reset join retry backoff
    join_retry_count_ = 0;
    join_backoff_remaining_ = 1;

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
                                              uint32_t reception_timestamp) {
    LOG_INFO(
        "*** RECEIVED MESSAGE: type 0x%02X from 0x%04X to 0x%04X (my state: "
        "%d, "
        "timestamp: %u) ***",
        static_cast<int>(message.GetType()), message.GetSource(),
        message.GetDestination(), static_cast<int>(state_),
        reception_timestamp);

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

        case MessageType::NM_CLAIM:
            return ProcessNMClaim(message);

        case MessageType::DATA:
            return ProcessDataMessage(message, reception_timestamp);

        case MessageType::DATA_BROADCAST:
            return ProcessBroadcastMessage(message, reception_timestamp);

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
    if (state_change_callback_) {
        state_change_callback_(state);
    }
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
        uint32_t current_time = GetRTOS().getTickCount();
        const auto& nodes = routing_table_->GetNodes();
        for (const auto& node : nodes) {
            bool is_manager =
                (node.routing_entry.destination == manager_address);
            routing_table_->UpdateNode(
                node.routing_entry.destination, node.battery_level, is_manager,
                node.GetAllocatedDataSlots(), node.routing_entry.capabilities,
                current_time);
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
    node_role_ = config.node_role;
    target_duty_cycle_ = config.target_duty_cycle;
    min_sleep_fraction_ = config.min_sleep_fraction;
    ewma_alpha_fixed_ = static_cast<uint8_t>(
        std::clamp(config.link_quality_ewma_alpha, 0.05f, 0.95f) * 256.0f);
    consecutive_missed_for_inactivation_ =
        config.consecutive_missed_for_inactivation;
    min_consecutive_for_reactivation_ = config.min_consecutive_for_reactivation;

    routing_table_->SetLinkQualityParams(ewma_alpha_fixed_,
                                         consecutive_missed_for_inactivation_,
                                         min_consecutive_for_reactivation_);

    LOG_INFO("Network service configured with node address 0x%04X, role: %d",
             node_address_, static_cast<int>(node_role_));

    return Result::Success();
}

const INetworkService::NetworkConfig& NetworkService::GetConfig() const {
    return config_;
}

uint8_t NetworkService::CalculateLinkQuality(AddressType node_address) const {
    return routing_table_->GetLinkQuality(node_address);
}

std::unique_ptr<BaseMessage> NetworkService::CreateRoutingTableMessage(
    AddressType destination) {
    std::lock_guard<std::mutex> lock(network_mutex_);

    // Get routing entries from routing table (excludes own address)
    std::vector<RoutingTableEntry> entries =
        routing_table_->GetRoutingEntries(node_address_);

    // Increment table version
    table_version_ = (table_version_ + 1) % 256;

    // Get local capabilities and data slots directly
    uint8_t local_capabilities = local_capabilities_;
    uint8_t local_data_slots = local_allocated_data_slots_;

    auto routing_msg_opt = RoutingTableMessage::Create(
        destination, node_address_, network_manager_, table_version_, entries,
        local_capabilities, local_data_slots);
    if (!routing_msg_opt) {
        LOG_ERROR("Failed to create routing table message");
        return nullptr;
    }

    RoutingTableMessage routing_msg = std::move(routing_msg_opt.value());

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
    if (!superframe_service_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Superframe service not available");
    }

    Result result = superframe_service_->StopSuperframe();
    if (!result.IsSuccess()) {
        LOG_ERROR("Failed to stop superframe: %s",
                  result.GetErrorMessage().c_str());
        return result;
    }

    LOG_INFO("Created new network as manager 0x%04X", node_address_);

    // Ensure election_priority_ is always set so cross-network NM_CLAIM
    // comparisons work correctly even when the node was configured as
    // NETWORK_MANAGER from boot (never went through StartElectionBackoff).
    election_priority_ = ComputeElectionPriority();
    surrendered_in_election_ = false;

    // Generate stable network_id_ if not already set (e.g. from a prior beacon)
    if (network_id_ == 0) {
        network_id_ = static_cast<uint16_t>(node_address_ ^
                                            (GetRTOS().GetRandom() & 0xFFFF));
        if (network_id_ == 0)
            network_id_ = node_address_;  // never 0
    }

    // Set ourselves as network manager
    SetNetworkManager(node_address_);

    // Update states
    is_synchronized_ = true;
    last_sync_time_ = GetRTOS().getTickCount();
    network_found_ = true;
    network_creator_ = true;

    // Initialize control slot assignment for NM
    my_control_slot_index_ = 0;  // NM always has slot index 0

    // Initialize local data slots for network creator
    SetLocalAllocatedDataSlots(config_.default_data_slots);

    superframe_service_->SetSynchronized(true);

    // Update state
    SetState(ProtocolState::NETWORK_MANAGER);

    // Add the network manager node
    // uint8_t current_capabilities = GetLocalNodeCapabilities();
    // NetworkNodeRoute manager_node(node_address_, 100, last_sync_time_, true,
    //                               current_capabilities,
    //                               config_.default_data_slots);
    // if (!routing_table_->AddNode(manager_node)) {
    //     LOG_ERROR("Failed to add network manager node 0x%04X to routing table",
    //               node_address_);
    //     return Result(LoraMesherErrorCode::kInvalidState,
    //                   "Failed to add network manager to routing table");
    // }
    // LOG_INFO("Added network manager node 0x%04X", node_address_);

    // Auto-calculate slot duration from radio ToA parameters
    uint32_t min_slot_duration = CalculateMinSlotDuration();
    if (superframe_service_) {
        LOG_INFO(
            "Auto-calculated slot duration: %u ms "
            "(ToA(%u)=%u ms + guard=%u ms + margin)",
            min_slot_duration, config_.max_packet_size,
            CalculateTimeOnAir(config_.max_packet_size), config_.guard_time_ms);
        uint16_t current_slots =
            static_cast<uint16_t>(superframe_service_->GetSuperframeDuration() /
                                  superframe_service_->GetSlotDuration());
        if (current_slots == 0)
            current_slots = ISuperframeService::DEFAULT_DISCOVERY_SLOT_COUNT;
        superframe_service_->UpdateSuperframeConfig(current_slots,
                                                    min_slot_duration, false);
    }

    // Initialize slot table as network manager
    result = UpdateSlotTable();
    if (!result) {
        LOG_ERROR("Failed to update slot table");
        return result;
    }

    result = superframe_service_->StartSuperframe();
    if (!result.IsSuccess()) {
        LOG_ERROR("Failed to start superframe: %s",
                  result.GetErrorMessage().c_str());
        return result;
    }

    return Result::Success();
}

Result NetworkService::PerformTimingSynchronization(
    const SyncBeaconMessage& sync_beacon, uint32_t reception_timestamp,
    const std::string& context_name, std::function<void()> pre_start_action) {

    if (!superframe_service_) {
        return Result(
            LoraMesherErrorCode::kInvalidState,
            "Superframe service not available for timing synchronization");
    }

    // Compensate for processing delay between radio DIO interrupt and timestamp capture
    // This delay includes: task switch (~5-10ms) + SPI read (~10-20ms) +
    // deserialization (~5-10ms) + logging (~10-15ms) = ~40-50ms total
    const uint32_t RECEPTION_PROCESSING_DELAY_MS = 0;
    uint32_t corrected_reception_timestamp =
        reception_timestamp - RECEPTION_PROCESSING_DELAY_MS;

    // Calculate original timing for synchronization using corrected reception timestamp
    uint32_t propagation_delay = sync_beacon.GetPropagationDelay();
    uint32_t time_on_air_ms = CalculateTimeOnAir(sync_beacon.GetTotalSize());
    uint32_t original_timing =
        sync_beacon.CalculateOriginalTiming(corrected_reception_timestamp);
    uint32_t estimated_nm_time = original_timing - time_on_air_ms;

    LOG_DEBUG(
        "%s sync beacon timing calculation: "
        "reception_time=%u ms, corrected_reception=%u ms, propagation_delay=%u "
        "ms, ToA=%u ms, "
        "original_timing=%u ms, estimated_nm_time=%u ms",
        context_name.c_str(), reception_timestamp,
        corrected_reception_timestamp, propagation_delay, time_on_air_ms,
        original_timing, estimated_nm_time);

    // Update superframe configuration to match network manager
    uint16_t superframe_duration = sync_beacon.GetSuperframeDuration();
    uint8_t total_slots = sync_beacon.GetTotalSlots();
    uint16_t slot_duration = sync_beacon.GetSlotDuration();

    LOG_DEBUG(
        "%s sync beacon timing: duration %d ms, slots %d, slot_duration %d ms",
        context_name.c_str(), superframe_duration, total_slots, slot_duration);

    // Skip the disruptive stop/sync/start cycle when drift is small and config
    // unchanged. The stop/start truncates the active slot to ~5ms, causing 9.7%
    // outlier rate on SYNC_BEACON_RX. Normal crystal drift (~0.3ms/superframe)
    // is well within the guard_time/2 threshold.
    bool config_unchanged =
        (total_slots == number_of_slots_per_superframe_) &&
        (slot_duration == superframe_service_->GetSlotDuration());

    if (superframe_service_->IsSynchronized() && config_unchanged) {
        uint32_t current_time_check = GetRTOS().getTickCount();
        uint32_t current_sf_start =
            current_time_check -
            superframe_service_->GetTimeSinceSuperframeStart();
        int32_t drift =
            static_cast<int32_t>(estimated_nm_time - current_sf_start);
        uint32_t abs_drift = static_cast<uint32_t>(std::abs(drift));
        uint32_t drift_threshold = config_.guard_time_ms / 2;

        if (abs_drift < drift_threshold) {
            LOG_DEBUG(
                "%s: drift %dms < threshold %ums (guard_time/2), skipping "
                "resync",
                context_name.c_str(), drift, drift_threshold);

            if (pre_start_action) {
                pre_start_action();
            }
            return Result::Success();
        }
    }

    // Stop the superframe now that all radio-dependent computations are done.
    superframe_service_->StopSuperframe();

    // Update superframe configuration
    Result config_result = superframe_service_->UpdateSuperframeConfig(
        total_slots, slot_duration, false);
    if (!config_result.IsSuccess()) {
        LOG_WARNING(
            "Failed to update superframe config from sync beacon in %s: %s",
            context_name.c_str(), config_result.GetErrorMessage().c_str());
    }

    // Calculate current slot at the Network Manager when the beacon was sent
    // Use corrected reception timestamp for consistency with timing calculations
    uint32_t nm_superframe_elapsed =
        corrected_reception_timestamp - estimated_nm_time;
    uint16_t nm_current_slot =
        static_cast<uint16_t>(nm_superframe_elapsed / slot_duration);

    // Synchronize our superframe timing with the Network Manager's timing
    Result sync_result = superframe_service_->SynchronizeWith(estimated_nm_time,
                                                              nm_current_slot);
    if (!sync_result.IsSuccess()) {
        LOG_WARNING("Failed to synchronize superframe timing in %s: %s",
                    context_name.c_str(),
                    sync_result.GetErrorMessage().c_str());
        if (pre_start_action)
            pre_start_action();
        Result sup_start_res = superframe_service_->StartSuperframe();
        if (!sup_start_res.IsSuccess()) {
            LOG_ERROR("Failed to start superframe in %s: %s",
                      context_name.c_str(),
                      sup_start_res.GetErrorMessage().c_str());
            return sup_start_res;
        }

        return sync_result;
    } else {
        LOG_DEBUG(
            "%s: Synchronized superframe with Network Manager timing (slot %d)",
            context_name.c_str(), nm_current_slot);
    }

    superframe_service_->DoNotUpdateStartTimeOnNewSuperframe();

    if (pre_start_action) {
        pre_start_action();
    }

    Result sup_start_res = superframe_service_->StartSuperframe();
    if (!sup_start_res.IsSuccess()) {
        LOG_ERROR("Failed to start superframe in %s: %s", context_name.c_str(),
                  sup_start_res.GetErrorMessage().c_str());
        return sup_start_res;
    }

    return Result::Success();
}

void NetworkService::ScheduleRoutingMessageExpectations() {
    // Delegate to routing table to update link statistics
    routing_table_->UpdateLinkStatistics();

    // Degrade routes through next_hops that are not TDMA-reachable.
    auto nodes = routing_table_->GetNodesCopy();
    for (const auto& node : nodes) {
        if (node.is_active && node.routing_entry.hop_count > 1 &&
            (!IsTDMANeighbor(node.next_hop) ||
             routing_table_->HasUnidirectionalRisk(node.next_hop))) {
            routing_table_->DegradeRouteQuality(node.routing_entry.destination,
                                                1);
        }
    }
}

void NetworkService::ResetLinkQualityStats() {
    // Reset link quality statistics - delegate to routing table
    // Note: Current routing table interface doesn't provide reset functionality
    // This is now handled internally by the routing table implementation
    LOG_DEBUG(
        "Link quality statistics reset requested - handled by routing table");
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
    // Delegate to routing table for link quality calculation
    return routing_table_->GetLinkQuality(node_address);
}

uint32_t NetworkService::CalculateTimeOnAir(uint8_t message_size) const {
    // Check cache first
    auto cache_it = toa_cache_.find(message_size);
    if (cache_it != toa_cache_.end()) {
        return cache_it->second;
    }

    uint32_t toa;
    if (!hardware_manager_) {
        // Fallback to rough estimate: 10ms per byte
        toa = message_size * 10;
    } else {
        toa = hardware_manager_->getTimeOnAir(message_size);
    }

    // Cache the result
    toa_cache_[message_size] = toa;

    return toa;
}

uint32_t NetworkService::CalculateNMTxTimeMs(uint8_t rt_node_count,
                                             uint8_t nm_data_slots) const {
    uint8_t sync_size = static_cast<uint8_t>(
        BaseHeader::Size() + SyncBeaconHeader::SyncBeaconFieldsSize());
    uint8_t rt_entries = (rt_node_count > 0) ? rt_node_count - 1 : 0;
    uint8_t rt_size = static_cast<uint8_t>(std::min(
        BaseHeader::Size() + RoutingTableHeader::RoutingTableFieldsSize() +
            rt_entries * RoutingTableEntry::Size(),
        static_cast<size_t>(255)));
    return CalculateTimeOnAir(sync_size) + CalculateTimeOnAir(rt_size) +
           nm_data_slots * CalculateTimeOnAir(config_.max_packet_size);
}

uint32_t NetworkService::CalculateMinSlotDuration() const {
    uint32_t max_toa = CalculateTimeOnAir(config_.max_packet_size);
    if (max_toa == 0) {
        return ISuperframeService::DEFAULT_SLOT_DURATION_MS;
    }

    // Slot must fit: guard_time + ToA(max_packet) + processing margin.
    // Margin covers superframe detection latency (20 ms) + task scheduling.
    static constexpr uint32_t kProcessingMarginMs = 50;
    uint32_t raw = max_toa + config_.guard_time_ms + kProcessingMarginMs;

    // Round up to the nearest 50 ms
    static constexpr uint32_t kRoundMs = 50;
    return ((raw + kRoundMs - 1) / kRoundMs) * kRoundMs;
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
    if (state_ != ProtocolState::JOINING) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Cannot send join request when not in JOINING state");
    }

    if (message_queue_service_->HasMessage(MessageType::JOIN_REQUEST)) {
        LOG_WARNING("Join request already in progress");
        // Remove the previous one to avoid duplicates
        bool has_removed =
            message_queue_service_->RemoveMessage(MessageType::JOIN_REQUEST);
        if (!has_removed) {
            LOG_ERROR("Failed to remove existing join request from queue");
            return Result(LoraMesherErrorCode::kInvalidState,
                          "Failed to remove existing join request from queue");
        }
    }

    // Battery level (default to 100%)
    uint8_t battery_level = 100;

    // Create join request message with selected sponsor
    auto join_request = JoinRequestMessage::Create(
        manager_address, node_address_, battery_level, requested_slots, {},
        selected_sponsor_, selected_sponsor_);

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
                                          uint32_t /* reception_timestamp */) {
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
    auto sponsor_address = join_request_opt->GetHeader().GetSponsorAddress();

    // Handle sponsor-based join request forwarding
    if (network_manager_ != node_address_) {
        // If we are the designated sponsor, forward to network manager via routing table
        if (sponsor_address != 0 && sponsor_address == node_address_ &&
            next_hop == node_address_) {
            LOG_INFO("Acting as sponsor for join request from 0x%04X", source);
            return ForwardJoinRequest(*join_request_opt);
        }
        // If there's a sponsor but it's not us, and the next hop is us, check if we're on the path to NM
        else if (sponsor_address != 0 && next_hop == node_address_) {
            // We are on the routing path, forward it
            LOG_DEBUG(
                "Forwarding join request from 0x%04X (on path to NM, sponsor: "
                "0x%04X)",
                source, sponsor_address);
            return ForwardJoinRequest(*join_request_opt);
        }
        // If we're not the network manager and not involved, ignore
        else {
            LOG_DEBUG("Ignoring join request - not for us");
            return Result::Success();
        }
    }

    // sponsor_address = sponsor_address == node_address_ ? 0 : sponsor_address;

    // Now we're the network manager, process the join request
    auto battery_level = join_request_opt->GetBatteryLevel();
    auto requested_slots = join_request_opt->GetRequestedSlots();
    auto hop_count = join_request_opt->GetHopCount();

    LOG_INFO("Join request from 0x%04X: battery=%d%%, slots=%d, hops=%d",
             source, battery_level, requested_slots, hop_count);

    // Check for duplicate from same source
    for (const auto& pending : pending_joins_) {
        if (pending.GetSource() == source) {
            LOG_INFO(
                "Duplicate join request from 0x%04X - already pending, "
                "ignoring",
                source);
            return Result::Success();
        }
    }

    // If queue is full, tell the node to retry next superframe
    if (pending_joins_.size() >= kMaxPendingJoins) {
        LOG_INFO("Join queue full (%zu/%zu), sending RETRY_LATER to 0x%04X",
                 pending_joins_.size(), kMaxPendingJoins, source);
        return SendJoinResponse(source, JoinResponseHeader::RETRY_LATER, 0,
                                sponsor_address);
    }

    uint8_t routing_hop_count = hop_count + 1;

    // Account for slots already committed to pending joins
    uint8_t pending_slot_count = 0;
    for (const auto& p : pending_joins_) {
        pending_slot_count += p.GetRequestedSlots();
    }

    // Determine if node should be accepted
    auto [accepted, allocated_slots] =
        ShouldAcceptJoin(source, requested_slots, routing_hop_count,
                         pending_joins_.size(), pending_slot_count);

    if (!accepted) {
        LOG_INFO("Join request from 0x%04X rejected - network constraints",
                 source);
        return SendJoinResponse(source, JoinResponseHeader::REJECTED, 0,
                                sponsor_address);
    }

    // Buffer the join request for next superframe boundary
    Result setReqSlotsResult =
        (*join_request_opt).SetRequestedSlots(requested_slots);
    if (!setReqSlotsResult) {
        LOG_ERROR("Failed to set requested slots for join request");
        return setReqSlotsResult;
    }

    pending_joins_.push_back(*join_request_opt);

    LOG_INFO(
        "Join request from 0x%04X buffered (%zu/%zu) for next superframe, "
        "allocated %d slots",
        source, pending_joins_.size(), kMaxPendingJoins, allocated_slots);

    // Compute control slot index for ACCEPTED joins
    uint8_t control_slot_index = 0xFF;
    {
        // Check if this node already has a control slot (re-join case)
        const auto& nodes = routing_table_->GetNodes();
        for (const auto& node : nodes) {
            if (node.GetAddress() == source &&
                node.control_slot_index != 0xFF) {
                control_slot_index = node.control_slot_index;
                LOG_INFO(
                    "Reusing control slot index %d for re-joining node 0x%04X",
                    control_slot_index, source);
                break;
            }
        }

        // Verify no other node already holds this index (stale propagation
        // can cause a previously-removed node to re-appear with an index
        // that was already reassigned to another node)
        if (control_slot_index != 0xFF) {
            for (const auto& node : nodes) {
                if (node.GetAddress() != source &&
                    node.control_slot_index == control_slot_index) {
                    LOG_WARNING(
                        "Control slot index %d conflict: already assigned to "
                        "0x%04X, reassigning for 0x%04X",
                        control_slot_index, node.GetAddress(), source);
                    control_slot_index = 0xFF;
                    break;
                }
            }
        }

        if (control_slot_index == 0xFF) {
            // New node: find lowest available control slot index
            control_slot_index = FindLowestAvailableControlSlot();
            LOG_INFO(
                "Assigned new control slot index %d to joining node 0x%04X",
                control_slot_index, source);
        }
    }

    // Send immediate acceptance response
    Result result =
        SendJoinResponse(source, JoinResponseHeader::ACCEPTED, allocated_slots,
                         sponsor_address, control_slot_index);
    if (!result) {
        // Remove the just-pushed pending join if response fails
        if (!pending_joins_.empty()) {
            pending_joins_.pop_back();
        }
        return result;
    }

    // Update routing information with hop_count from join request
    // hop_count in message = number of forwards, actual routing hop_count = hop_count + 1
    // next_hop should be the first hop towards the joining node, NOT the sponsor directly
    AddressType route_next_hop;
    if (sponsor_address != 0 && sponsor_address != node_address_) {
        // Use routing table to find next hop towards the sponsor
        route_next_hop = routing_table_->FindNextHop(sponsor_address);
        if (route_next_hop == 0) {
            // No route to sponsor yet, use sponsor directly (edge case)
            LOG_WARNING("No route to sponsor 0x%04X, using sponsor directly",
                        sponsor_address);
            route_next_hop = sponsor_address;
        }
    } else {
        route_next_hop = source;
    }

    uint32_t current_time = GetRTOS().getTickCount();
    bool route_updated = routing_table_->UpdateRoute(
        route_next_hop,  // source of the route (next hop towards joining node)
        source,          // destination (the joining node)
        routing_hop_count,  // hop count to reach the joining node
        255,                // link quality (max for new join)
        allocated_slots,    // allocated data slots
        0,  // capabilities (will be updated from routing table later)
        current_time);

    if (route_updated) {
        LOG_INFO(
            "Route to joining node 0x%04X updated: hop_count=%d, "
            "next_hop=0x%04X",
            source, routing_hop_count, route_next_hop);
    }

    // Add the joining node to the network immediately so it appears in network views
    // The full slot allocation will be handled at the superframe boundary
    bool node_updated =
        UpdateNetworkNode(source, battery_level, false, allocated_slots,
                          0);  // capabilities will be set from routing table
    if (node_updated) {
        LOG_INFO("Joining node 0x%04X updated to network manager's node list",
                 source);
    }

    // Store the assigned control slot index in the routing table
    routing_table_->SetControlSlotIndex(source, control_slot_index);

    return Result::Success();
}

Result NetworkService::ProcessJoinResponse(const BaseMessage& message,
                                           uint32_t /* reception_timestamp */) {
    // Deserialize to get sponsor information for forwarding logic
    auto join_response_opt =
        JoinResponseMessage::CreateFromSerialized(*message.Serialize());
    if (!join_response_opt) {
        return Result(
            LoraMesherErrorCode::kSerializationError,
            "Failed to deserialize join response for forwarding check");
    }

    auto target_address = join_response_opt->GetHeader().GetTargetAddress();
    auto dest = message.GetDestination();

    // Print debug info
    LOG_DEBUG("Processing JOIN_RESPONSE from 0x%04X to 0x%04X (target: 0x%04X)",
              message.GetSource(), dest, target_address);

    // Handle sponsor-based join response forwarding
    if (dest == node_address_ && target_address != 0) {
        // We are the sponsor and need to forward to the target (joining node)
        LOG_INFO("Forwarding join response to target node 0x%04X",
                 target_address);
        return ForwardJoinResponseToSponsoredNode(*join_response_opt);
    }

    // Check if we should forward this response (we're on the multi-hop path to sponsor)
    auto next_hop = join_response_opt->GetHeader().GetNextHop();
    if (dest != node_address_ && next_hop == node_address_) {
        // We are on the routing path, forward to next hop toward destination (sponsor)
        LOG_DEBUG("Forwarding join response to 0x%04X (on path to sponsor)",
                  dest);
        return ForwardJoinResponse(*join_response_opt);
    }

    if (dest != node_address_) {
        // This response is not for us and we're not on the path
        LOG_DEBUG(
            "Ignoring join response - not intended for us (dest: 0x%04X, "
            "next_hop: 0x%04X)",
            dest, next_hop);
        return Result::Success();
    }

    // Only process in joining state if the response is for us
    if (state_ != ProtocolState::JOINING) {
        LOG_DEBUG("Ignoring join response - not in joining state");
        return Result::Success();
    }

    auto status = join_response_opt->GetStatus();
    auto network_id = join_response_opt->GetNetworkId();
    auto allocated_slots = join_response_opt->GetAllocatedSlots();
    auto source = message.GetSource();

    LOG_INFO("Join response from 0x%04X: status=%d, network=0x%04X, slots=%d",
             source, static_cast<int>(status), network_id, allocated_slots);

    if (status == JoinResponseStatus::ACCEPTED) {
        // Store the assigned control slot index
        my_control_slot_index_ = join_response_opt->GetControlSlotIndex();
        slot_table_dirty_ = true;
        LOG_INFO("Received control slot index %d from NM",
                 my_control_slot_index_);

        // Update network state
        SetNetworkManager(source);
        is_synchronized_ = true;
        last_sync_time_ = GetRTOS().getTickCount();
        network_found_ = true;
        superframe_service_->SetSynchronized(true);

        // Move to normal operation first so UpdateNetworkNode allows adding local node
        SetState(ProtocolState::NORMAL_OPERATION);

        // Add ourselves to the network nodes so we get TX and CONTROL_TX slots
        UpdateNetworkNode(node_address_, 100, false, allocated_slots);
        LOG_INFO("Added local node 0x%04X to network for slot allocation",
                 node_address_);

        SetLocalAllocatedDataSlots(allocated_slots);

        LOG_INFO("Successfully joined network 0x%04X", network_id);

        // Clear sponsor information - we're now part of the network
        if (selected_sponsor_ != 0) {
            LOG_INFO("Clearing sponsor 0x%04X - successfully joined network",
                     selected_sponsor_);
            selected_sponsor_ = 0;
        }
    } else if (status == JoinResponseStatus::RETRY_LATER) {
        LOG_INFO("Join request deferred (NM busy), will retry next superframe");
        // NM received our request but is busy processing another join.
        // Set a short backoff (1 superframe) and reset retry count since
        // the message was delivered successfully - this isn't a collision.
        join_backoff_remaining_ = 1;
        join_retry_count_ = 0;
    } else {
        LOG_WARNING("Join rejected with status %d", static_cast<int>(status));

        // Clear sponsor information - join was rejected, need fresh discovery
        if (selected_sponsor_ != 0) {
            LOG_INFO("Clearing sponsor 0x%04X - join was rejected",
                     selected_sponsor_);
            selected_sponsor_ = 0;
        }

        // TODO: Check this
        // Return to discovery
        SetState(ProtocolState::FAULT_RECOVERY);
    }

    return Result::Success();
}

Result NetworkService::SendJoinResponse(AddressType dest,
                                        JoinResponseStatus status,
                                        uint8_t allocated_slots,
                                        AddressType sponsor_address,
                                        uint8_t control_slot_index) {
    // Only network manager sends join responses
    if (network_manager_ != node_address_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Only network manager can send join responses");
    }

    // Determine routing: if there's an external sponsor, route via sponsor; otherwise direct
    // Special case: if sponsor_address == node_address_, we ARE the sponsor, so route directly
    AddressType response_destination;
    AddressType target_address;
    AddressType next_hop = 0;

    if (sponsor_address != 0 && sponsor_address != node_address_) {
        // External sponsor case: route via sponsor with target for final delivery
        response_destination = sponsor_address;
        target_address = dest;

        // Calculate next hop toward sponsor for multi-hop routing
        next_hop = routing_table_->FindNextHop(sponsor_address);
        if (next_hop == 0) {
            next_hop = sponsor_address;  // Direct if no route found
        }
    } else {
        // Direct case: no external sponsor OR we are the sponsor
        response_destination = dest;
        target_address = 0;
    }

    // Create join response with corrected addressing and next_hop for multi-hop forwarding
    auto join_response =
        JoinResponseMessage::Create(response_destination, node_address_,
                                    network_manager_,  // Network ID
                                    allocated_slots, status, {}, next_hop,
                                    target_address, control_slot_index);

    if (!join_response) {
        return Result(LoraMesherErrorCode::kMemoryError,
                      "Failed to create join response");
    }

    // Queue for transmission
    auto base_msg =
        std::make_unique<BaseMessage>(join_response->ToBaseMessage());
    message_queue_service_->AddMessageToQueue(
        SlotAllocation::SlotType::DISCOVERY_TX, std::move(base_msg));

    if (sponsor_address != 0 && sponsor_address != node_address_) {
        LOG_INFO(
            "Join response queued for 0x%04X via external sponsor 0x%04X: "
            "status=%d, slots=%d, ctrl_idx=%d",
            dest, sponsor_address, static_cast<int>(status), allocated_slots,
            control_slot_index);
    } else if (sponsor_address == node_address_) {
        LOG_INFO(
            "Join response queued for 0x%04X (we are the sponsor): status=%d, "
            "slots=%d, ctrl_idx=%d",
            dest, static_cast<int>(status), allocated_slots,
            control_slot_index);
    } else {
        LOG_INFO(
            "Join response queued for 0x%04X (direct, no sponsor): status=%d, "
            "slots=%d, ctrl_idx=%d",
            dest, static_cast<int>(status), allocated_slots,
            control_slot_index);
    }

    return Result::Success();
}

// Data message implementations

Result NetworkService::ProcessDataMessage(const BaseMessage& message,
                                          uint32_t /* reception_timestamp */) {
    // Deserialize the data message
    auto data_msg_opt = DataMessage::CreateFromSerialized(*message.Serialize());
    if (!data_msg_opt) {
        LOG_ERROR("Failed to deserialize DATA message");
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to deserialize data message");
    }

    const DataMessage& data_msg = *data_msg_opt;
    AddressType next_hop = data_msg.GetNextHop();
    AddressType final_dest = data_msg.GetDestination();
    AddressType original_src = data_msg.GetSource();
    uint8_t seq_num = data_msg.GetSeqNum();
    uint8_t ttl = data_msg.GetTTL();

    LOG_DEBUG(
        "DATA message: src=0x%04X, dest=0x%04X, next_hop=0x%04X, "
        "my_addr=0x%04X, ttl=%u, seq=%u, payload_size=%zu",
        original_src, final_dest, next_hop, node_address_, ttl, seq_num,
        data_msg.GetPayload().size());

    // Ignore our own messages heard back
    if (original_src == node_address_) {
        LOG_DEBUG("Ignoring own data message (seq=%u)", seq_num);
        return Result::Success();
    }

    // De-duplication check (prevents loops)
    if (IsMessageDuplicate(original_src, seq_num)) {
        LOG_DEBUG("Dropping duplicate DATA from 0x%04X seq=%u", original_src,
                  seq_num);
        return Result::Success();
    }

    // Check if we are the intended next hop (before caching — overheard
    // packets must not poison the dedup table or legitimate forwarded
    // copies addressed to us will be falsely dropped)
    if (next_hop != node_address_) {
        LOG_DEBUG("DATA not for this node (next_hop=0x%04X), ignoring",
                  next_hop);
        return Result::Success();
    }

    AddToMessageCache(original_src, seq_num);

    // We are the next hop - check if we are also the final destination
    if (final_dest == node_address_) {
        // Deliver to application layer
        LOG_INFO(
            "DATA reached final destination: src=0x%04X, dest=0x%04X, seq=%u, "
            "payload_size=%zu",
            original_src, final_dest, seq_num, data_msg.GetPayload().size());

        if (data_received_callback_) {
            data_received_callback_(original_src, data_msg.GetPayload());
            LOG_DEBUG("DATA delivered to application layer");
        } else {
            LOG_WARNING("No data callback registered - DATA dropped");
        }
    } else {
        // TTL check before forwarding
        if (ttl <= 1) {
            LOG_WARNING(
                "DATA TTL expired: src=0x%04X, dest=0x%04X, seq=%u, dropping",
                original_src, final_dest, seq_num);
            return Result::Success();
        }
        // Forward to next hop toward final destination
        LOG_INFO("Forwarding DATA: src=0x%04X, dest=0x%04X, seq=%u, ttl=%u",
                 original_src, final_dest, seq_num, ttl);
        return ForwardDataMessage(data_msg);
    }

    return Result::Success();
}

Result NetworkService::ForwardDataMessage(const DataMessage& original_msg) {
    // Find the next hop toward the final destination
    AddressType new_next_hop = FindNextHop(original_msg.GetDestination());

    if (new_next_hop == 0) {
        LOG_ERROR("No route to dest=0x%04X for forwarding: src=0x%04X, seq=%u",
                  original_msg.GetDestination(), original_msg.GetSource(),
                  original_msg.GetSeqNum());
        return Result(LoraMesherErrorCode::kNoRoute,
                      "No route to destination for forwarding");
    } else if (new_next_hop == original_msg.GetSource()) {
        LOG_ERROR(
            "Next hop for forwarding is the original sender (0x%04X) - "
            "potential routing loop, dropping message: src=0x%04X, "
            "dest=0x%04X, seq=%u",
            new_next_hop, original_msg.GetSource(),
            original_msg.GetDestination(), original_msg.GetSeqNum());
        return Result(
            LoraMesherErrorCode::kNoRoute,
            "Next hop is the original sender, potential routing loop");
    }

    auto forwarded_msg =
        DataMessage::CreateForwarded(original_msg, new_next_hop);
    if (!forwarded_msg) {
        LOG_WARNING("DATA TTL expired during forwarding, dropping");
        return Result::Success();
    }

    LOG_DEBUG(
        "Forwarding DATA: dest=0x%04X, src=0x%04X, next_hop=0x%04X, ttl=%u",
        original_msg.GetDestination(), original_msg.GetSource(), new_next_hop,
        forwarded_msg->GetTTL());

    // Queue for TX slot
    auto base_msg =
        std::make_unique<BaseMessage>(forwarded_msg->ToBaseMessage());
    message_queue_service_->AddMessageToQueue(SlotAllocation::SlotType::TX,
                                              std::move(base_msg));

    return Result::Success();
}

Result NetworkService::SendData(AddressType destination,
                                const std::vector<uint8_t>& data) {
    // Check if destination is self
    if (destination == node_address_) {
        LOG_WARNING("Cannot send data to self");
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Cannot send data to self");
    }

    if (state_ != ProtocolState::NORMAL_OPERATION &&
        state_ != ProtocolState::NETWORK_MANAGER) {
        LOG_WARNING("Cannot send data in state %d, not in normal operation",
                    static_cast<int>(state_));
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Cannot send data outside normal operation");
    }

    // Find the next hop to the destination
    AddressType next_hop = FindNextHop(destination);

    if (next_hop == 0) {
        // No route found - check if destination might be a direct neighbor
        // If we have no route information, assume direct delivery
        LOG_DEBUG("No route to 0x%04X, attempting direct delivery",
                  destination);
        next_hop = destination;
    }

    // Assign TTL and sequence number
    message_seq_++;
    uint8_t ttl = (config_.max_hops > 0) ? config_.max_hops : kDefaultTTL;

    auto data_msg = DataMessage::Create(destination, node_address_, next_hop,
                                        data, ttl, message_seq_);
    if (!data_msg) {
        LOG_ERROR("Failed to create DATA message for 0x%04X", destination);
        return Result(LoraMesherErrorCode::kMemoryError,
                      "Failed to create data message");
    }

    // Prevent self-receive if we hear our own message
    AddToMessageCache(node_address_, message_seq_);

    LOG_INFO(
        "Sending DATA to 0x%04X via 0x%04X (ttl=%u, seq=%u), payload_size=%zu",
        destination, next_hop, ttl, message_seq_, data.size());

    // Queue for TX slot
    auto base_msg = std::make_unique<BaseMessage>(data_msg->ToBaseMessage());
    message_queue_service_->AddMessageToQueue(SlotAllocation::SlotType::TX,
                                              std::move(base_msg));

    return Result::Success();
}

// Broadcast message implementations

Result NetworkService::ProcessBroadcastMessage(
    const BaseMessage& message, uint32_t /* reception_timestamp */) {
    auto bcast_opt =
        BroadcastMessage::CreateFromSerialized(*message.Serialize());
    if (!bcast_opt) {
        LOG_ERROR("Failed to deserialize broadcast message");
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to deserialize broadcast message");
    }

    const BroadcastMessage& bcast = *bcast_opt;
    AddressType source = bcast.GetSource();
    uint8_t seq_num = bcast.GetSeqNum();
    uint8_t ttl = bcast.GetTTL();

    // Ignore our own broadcasts heard back
    if (source == node_address_) {
        LOG_DEBUG("Ignoring own broadcast (seq=%u)", seq_num);
        return Result::Success();
    }

    // De-duplication check
    if (IsMessageDuplicate(source, seq_num)) {
        LOG_DEBUG("Dropping duplicate broadcast from 0x%04X seq=%u", source,
                  seq_num);
        return Result::Success();
    }

    AddToMessageCache(source, seq_num);

    // Deliver to application layer
    LOG_INFO("BROADCAST from 0x%04X (ttl=%u, seq=%u), payload_size=%zu", source,
             ttl, seq_num, bcast.GetPayload().size());

    if (data_received_callback_) {
        data_received_callback_(source, bcast.GetPayload());
    } else {
        LOG_WARNING("No data callback registered - broadcast dropped");
    }

    // Forward if TTL allows
    if (ttl > 1) {
        return ForwardBroadcastMessage(bcast);
    }

    return Result::Success();
}

Result NetworkService::SendBroadcast(std::span<const uint8_t> data) {
    message_seq_++;

    auto bcast = BroadcastMessage::Create(node_address_, kDefaultTTL,
                                          message_seq_, data);
    if (!bcast) {
        LOG_ERROR("Failed to create broadcast message");
        return Result(LoraMesherErrorCode::kMemoryError,
                      "Failed to create broadcast message");
    }

    // Prevent self-receive if we hear our own broadcast
    AddToMessageCache(node_address_, message_seq_);

    LOG_INFO("Sending BROADCAST (ttl=%u, seq=%u), payload_size=%zu",
             kDefaultTTL, message_seq_, data.size());

    auto base_msg = std::make_unique<BaseMessage>(bcast->ToBaseMessage());
    message_queue_service_->AddMessageToQueue(SlotAllocation::SlotType::TX,
                                              std::move(base_msg));

    return Result::Success();
}

bool NetworkService::IsMessageDuplicate(AddressType source,
                                        uint8_t seq_num) const {
    for (const auto& entry : message_cache_) {
        if (entry.valid && entry.source == source && entry.seq_num == seq_num) {
            return true;
        }
    }
    return false;
}

void NetworkService::AddToMessageCache(AddressType source, uint8_t seq_num) {
    message_cache_[message_cache_head_] = {source, seq_num, true};
    message_cache_head_ = (message_cache_head_ + 1) % kMessageCacheSize;
}

Result NetworkService::ForwardBroadcastMessage(
    const BroadcastMessage& original) {
    auto forwarded = BroadcastMessage::CreateForwarded(original);
    if (!forwarded) {
        LOG_DEBUG("Broadcast TTL expired, not forwarding");
        return Result::Success();
    }

    LOG_DEBUG("Forwarding broadcast from 0x%04X (ttl=%u->%u, seq=%u)",
              original.GetSource(), original.GetTTL(), forwarded->GetTTL(),
              original.GetSeqNum());

    auto base_msg = std::make_unique<BaseMessage>(forwarded->ToBaseMessage());
    message_queue_service_->AddMessageToQueue(SlotAllocation::SlotType::TX,
                                              std::move(base_msg));

    return Result::Success();
}

// Slot management implementations

Result NetworkService::ProcessSlotRequest(const BaseMessage& message,
                                          uint32_t /* reception_timestamp */) {
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
    bool node_exists = routing_table_->IsNodePresent(source);

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

        // Defer slot table rebuild to next superframe boundary.
        // Non-NM nodes learn the new table only via the next SyncBeacon anyway,
        // so rebuilding mid-superframe would cause TX slot collisions.
        pending_slot_table_rebuild_ = true;

        LOG_INFO(
            "Allocated %d slots to node 0x%04X, slot reallocation deferred to "
            "next superframe",
            allocated_slots, source);
    } else {
        LOG_WARNING("No slots available for node 0x%04X", source);
    }

    return Result::Success();
}

Result NetworkService::ProcessSlotAllocation(
    const BaseMessage& /* message */, uint32_t /* reception_timestamp */) {
    // Only accept from network manager
    // if (message.GetSource() != network_manager_) {
    //     LOG_WARNING("Ignoring slot allocation from non-manager 0x%04X",
    //                 message.GetSource());
    //     return Result::Success();
    // }

    // auto slot_alloc_opt =
    //     SlotAllocationMessage::CreateFromSerialized(*message.Serialize());

    // if (!slot_alloc_opt) {
    //     return Result(LoraMesherErrorCode::kSerializationError,
    //                   "Failed to deserialize slot allocation");
    // }

    // auto network_id = slot_alloc_opt->GetNetworkId();
    // auto allocated_slots = slot_alloc_opt->GetAllocatedSlots();
    // auto total_nodes = slot_alloc_opt->GetTotalNodes();

    // LOG_INFO("Slot allocation: network=0x%04X, slots=%d, nodes=%d", network_id,
    //          allocated_slots, total_nodes);

    // // Reinitialize slot table
    // Result result = UpdateSlotTable();
    // if (!result) {
    //     return result;
    // }

    // // Update synchronization
    // is_synchronized_ = true;
    // last_sync_time_ = GetRTOS().getTickCount();

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
    slot_count_ = 0;

    // Get all nodes including self ordered.
    std::vector<NetworkNodeRoute> ordered_nodes = routing_table_->GetNodes();

    // Ensure self node is included for CONTROL_TX slot allocation
    // (self may not be in routing table since we removed self-entries)
    bool self_found = std::any_of(ordered_nodes.begin(), ordered_nodes.end(),
                                  [this](const NetworkNodeRoute& node) {
                                      return node.GetAddress() == node_address_;
                                  });

    if (!self_found) {
        NetworkNodeRoute self_node(node_address_, 0, 0, false, 0,
                                   local_allocated_data_slots_, 0);
        self_node.is_active = true;
        self_node.is_network_manager = (network_manager_ == node_address_);
        ordered_nodes.push_back(self_node);
    }

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

    // Get the total data slots allocated (includes self's local_allocated_data_slots_)
    uint8_t total_data_slots = GetAllocatedDataSlots();

    // Use max_hops from received sync beacons
    int max_hops_count = current_network_depth_;

    if (network_manager_ == node_address_) {
        // NM: compute from actual assignments
        uint8_t max_index =
            (my_control_slot_index_ != 0xFF) ? my_control_slot_index_ : 0;
        for (const auto& node : ordered_nodes) {
            if (node.control_slot_index != 0xFF &&
                node.control_slot_index > max_index) {
                max_index = node.control_slot_index;
            }
        }
        allocated_control_slots_ = max_index + 1;
    } else {
        // Non-NM: use authoritative node_count from sync beacon
        allocated_control_slots_ = beacon_node_count_;
    }

    // Add discovery slots, (max hops + 1) * 2 to get a full round trip message to the request
    allocated_discovery_slots_ = (max_hops_count + 1) * 2;

    // Add sync beacon slots 1 per hop layer
    uint8_t sync_beacon_slots = (max_hops_count + 1);

    // Calculate active slots (non-sleep)
    uint8_t total_active_slots = sync_beacon_slots + allocated_control_slots_ +
                                 allocated_discovery_slots_ + total_data_slots;

    uint8_t total_superframe_slots =
        std::max(number_of_slots_per_superframe_, kMinSlots);

    // TX time used for both NM duty cycle computation and logging
    uint32_t tx_time_ms = 0;

    if (network_creator_) {
        // Duty cycle applies only to TX time (not RX or sleep slots).
        // NM is the worst case: it transmits sync beacon + routing table + data.

        // Find NM's own data slot allocation
        // TODO: Find the grater node that contains the most allocated data slots. This will be the reference.
        // Or do the duty cycle by device and calculate it someway?
        uint8_t nm_data_slots = config_.default_data_slots;
        for (const auto& node : ordered_nodes) {
            if (node.routing_entry.destination == node_address_) {
                nm_data_slots = node.routing_entry.allocated_data_slots;
                break;
            }
        }

        // Calculate total NM TX time using Time-on-Air for each packet
        tx_time_ms =
            CalculateNMTxTimeMs(allocated_control_slots_, nm_data_slots);

        // Compute superframe size: total_tx_time / (slot_duration * duty_cycle)
        uint32_t slot_duration_ms =
            superframe_service_ ? superframe_service_->GetSlotDuration() : 1000;
        if (slot_duration_ms == 0)
            slot_duration_ms = 1000;

        float required_superframe_ms =
            static_cast<float>(tx_time_ms) / target_duty_cycle_;
        uint8_t computed_slots = static_cast<uint8_t>(
            std::min(std::ceil(required_superframe_ms / slot_duration_ms),
                     static_cast<float>(255)));

        uint8_t min_for_sleep = total_active_slots;
        if (min_sleep_fraction_ > 0.0f && min_sleep_fraction_ < 1.0f) {
            min_for_sleep = static_cast<uint8_t>(
                std::min(std::ceil(static_cast<float>(total_active_slots) /
                                   (1.0f - min_sleep_fraction_)),
                         255.0f));
        }
        total_superframe_slots =
            std::max({computed_slots, kMinSlots, min_for_sleep});
    }

    uint8_t sleep_slots = total_superframe_slots - total_active_slots;
    uint32_t slot_duration_ms_log =
        superframe_service_ ? superframe_service_->GetSlotDuration() : 1000;
    float actual_tx_duty_cycle =
        (slot_duration_ms_log > 0 && tx_time_ms > 0)
            ? static_cast<float>(tx_time_ms) /
                  (total_superframe_slots * slot_duration_ms_log)
            : 0.0f;

    LOG_DEBUG("Total slots in the superframe %d (target TX duty cycle: %.2f%%)",
              total_superframe_slots, target_duty_cycle_ * 100.0f);
    LOG_DEBUG("Active slots %d: sync %d, control %d, discovery %d, data %d",
              total_active_slots, sync_beacon_slots, allocated_control_slots_,
              allocated_discovery_slots_, total_data_slots);
    LOG_DEBUG("SLEEP slots %d | actual TX duty cycle: %.2f%%", sleep_slots,
              actual_tx_duty_cycle * 100.0f);

    slot_count_ = total_superframe_slots;

    // Ensure we never shrink below the NM-announced superframe size.
    // A stale/incomplete local routing table (e.g. after ApplyPendingJoin) can
    // compute fewer total slots than the NM expects, causing premature superframe
    // end and a 1000ms slot-skip on CalculateNextEventTimeout().
    if (slot_count_ < number_of_slots_per_superframe_) {
        LOG_DEBUG("Clamping slot_count_ from %d to NM-announced %d",
                  slot_count_, number_of_slots_per_superframe_);
        slot_count_ = number_of_slots_per_superframe_;
    }

    // Single slot_index advances through all allocation phases
    size_t slot_index = 0;
    auto AllocateSlot = [&](SlotAllocation::SlotType type,
                            AddressType addr = 0) {
        slot_table_[slot_index] = SlotAllocation(slot_index, type, addr);
        slot_index++;
    };

    // Determine our hop distance from Network Manager
    uint8_t our_hop_distance = GetHopDistanceToNM();

    // ── Phase 1: Sync beacon slots (hop-layered forwarding) ──────────────────
    for (size_t hop_layer = 0;
         hop_layer < sync_beacon_slots && slot_index < slot_count_;
         hop_layer++) {
        SlotAllocation::SlotType sync_type;
        if (hop_layer == 0) {
            sync_type = (state_ == ProtocolState::NETWORK_MANAGER &&
                         network_manager_ == node_address_)
                            ? SlotAllocation::SlotType::SYNC_BEACON_TX
                            : SlotAllocation::SlotType::SYNC_BEACON_RX;
        } else if (our_hop_distance == hop_layer) {
            sync_type = SlotAllocation::SlotType::SYNC_BEACON_TX;
        } else if (our_hop_distance == hop_layer + 1) {
            sync_type = SlotAllocation::SlotType::SYNC_BEACON_RX;
        } else {
            sync_type = SlotAllocation::SlotType::SLEEP;
        }
        AllocateSlot(sync_type, kBroadcastAddress);
    }

    // ── Phase 2: Control slots (join-order indexed TX/RX) ────────────────────
    for (size_t i = 0; i < allocated_control_slots_ && slot_index < slot_count_;
         i++) {
        if (my_control_slot_index_ != 0xFF && i == my_control_slot_index_ &&
            network_manager_ != 0) {
            AllocateSlot(SlotAllocation::SlotType::CONTROL_TX, node_address_);
        } else {
            AllocateSlot(SlotAllocation::SlotType::CONTROL_RX,
                         kBroadcastAddress);
        }
    }

    // ── Phase 3: Data slots (per-node TX/RX/SLEEP) ───────────────────────────
    for (const auto& node : ordered_nodes) {
        AddressType addr = node.GetAddress();
        uint8_t slot_data_number = node.GetAllocatedDataSlots();
        for (size_t j = 0; j < slot_data_number; j++) {
            if (slot_index >= slot_count_) {
                LOG_ERROR(
                    "Slot index %zu out of bounds for node 0x%04X, skipping",
                    slot_index, addr);
                return Result(LoraMesherErrorCode::kInvalidState,
                              "Slot index out of bounds");
            }
            if (node_address_ == addr) {
                AllocateSlot(SlotAllocation::SlotType::TX, addr);
            } else if (node.IsDirectNeighbor()) {
                AllocateSlot(SlotAllocation::SlotType::RX, addr);
            } else {
                AllocateSlot(SlotAllocation::SlotType::SLEEP, addr);
            }
        }
    }

    // ── Phase 4: Sleep slots ──────────────────────────────────────────────────
    for (size_t i = 0; i < sleep_slots; i++) {
        if (slot_index >= slot_count_) {
            LOG_ERROR("SLEEP slot index %zu out of bounds, skipping",
                      slot_index);
            return Result(LoraMesherErrorCode::kInvalidState,
                          "Slot index out of bounds");
        }
        AllocateSlot(SlotAllocation::SlotType::SLEEP, 0);
    }

    // ── Phase 5: Discovery slots ──────────────────────────────────────────────
    for (size_t i = 0; i < allocated_discovery_slots_; i++) {
        if (slot_index >= slot_count_) {
            LOG_ERROR("Discovery slot index %zu out of bounds, skipping",
                      slot_index);
            return Result(LoraMesherErrorCode::kInvalidState,
                          "Slot index out of bounds");
        }
        AllocateSlot(SlotAllocation::SlotType::DISCOVERY_RX, 0);
    }

    LogSlotTable();

    LOG_INFO(
        "Updated slot table: %d total (%d active: %d sync + %d ctrl + %d disc "
        "+ %d data, %d sleep, %.1f%% TX duty cycle)",
        total_superframe_slots, total_active_slots, sync_beacon_slots,
        allocated_control_slots_, allocated_discovery_slots_, total_data_slots,
        sleep_slots, actual_tx_duty_cycle * 100.0f);

    if (!superframe_service_) {
        LOG_ERROR("Superframe service not available, cannot update slot table");
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Superframe service not available");
    }

    // Notify superframe service of new slot table
    Result result =
        superframe_service_->UpdateSuperframeConfig(slot_count_, 0, false);
    if (!result) {
        LOG_ERROR("Failed to update superframe service with new slot table");
        return result;
    }

    slot_table_dirty_ = false;
    return Result::Success();
}

void NetworkService::LogSlotTable() const {
#if LORAMESHER_LOG_LEVEL > 0
    return;
#else
    // 256 slots * 3 chars + row prefixes + header + detail ≈ 1024 max
    static char buf[1024];
    constexpr size_t kSlotsPerRow = 20;
    constexpr size_t kBufSize = sizeof(buf);

    auto Abbrev = [](SlotAllocation::SlotType t) -> const char* {
        switch (t) {
            case SlotAllocation::SlotType::SYNC_BEACON_TX:
                return "ST";
            case SlotAllocation::SlotType::SYNC_BEACON_RX:
                return "SR";
            case SlotAllocation::SlotType::CONTROL_TX:
                return "CT";
            case SlotAllocation::SlotType::CONTROL_RX:
                return "CR";
            case SlotAllocation::SlotType::TX:
                return "TX";
            case SlotAllocation::SlotType::RX:
                return "RX";
            case SlotAllocation::SlotType::SLEEP:
                return "..";
            case SlotAllocation::SlotType::DISCOVERY_RX:
                return "DR";
            case SlotAllocation::SlotType::DISCOVERY_TX:
                return "DT";
            default:
                return "??";
        }
    };

    size_t off = 0;
    auto Append = [&](const char* fmt,
                      ...) __attribute__((format(printf, 2, 3))) {
        if (off >= kBufSize)
            return;
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(buf + off, kBufSize - off, fmt, args);
        va_end(args);
        if (n > 0)
            off += std::min(static_cast<size_t>(n), kBufSize - off);
    };

    Append("SlotTable[%u] NM=%04X hop=%u:\n", slot_count_, network_manager_,
           GetHopDistanceToNM());

    // Grid rows, 20 slots per row
    for (size_t row_start = 0; row_start < slot_count_;
         row_start += kSlotsPerRow) {
        Append("%02zu|", row_start);
        size_t row_end = std::min(row_start + kSlotsPerRow,
                                  static_cast<size_t>(slot_count_));
        for (size_t i = row_start; i < row_end; i++) {
            Append(i > row_start ? " %s" : "%s", Abbrev(slot_table_[i].type));
        }
        Append("\n");
    }

    // Data slot detail: group consecutive same-type slots with addresses
    bool has_detail = false;
    size_t i = 0;
    while (i < slot_count_) {
        const auto& slot = slot_table_[i];
        if (slot.type != SlotAllocation::SlotType::TX &&
            slot.type != SlotAllocation::SlotType::RX) {
            i++;
            continue;
        }

        size_t run_start = i;
        while (i + 1 < slot_count_ && slot_table_[i + 1].type == slot.type &&
               slot_table_[i + 1].target_address == slot.target_address) {
            i++;
        }

        if (!has_detail)
            Append("  ");
        else
            Append(" ");
        has_detail = true;

        if (slot.type == SlotAllocation::SlotType::TX) {
            if (run_start == i)
                Append("TX:#%zu(self)", run_start);
            else
                Append("TX:#%zu-%zu(self)", run_start, i);
        } else {
            if (run_start == i)
                Append("RX:#%zu(%04X)", run_start, slot.target_address);
            else
                Append("RX:#%zu-%zu(%04X)", run_start, i, slot.target_address);
        }
        i++;
    }

    loramesher::LOG.LogRaw(LogLevel::kDebug, buf);
#endif
}

Result NetworkService::SetDiscoverySlots() {
    // Clear existing discovery slots
    allocated_discovery_slots_ =
        std::max(ISuperframeService::DEFAULT_DISCOVERY_SLOT_COUNT,
                 static_cast<uint32_t>(slot_count_));

    slot_count_ = static_cast<uint16_t>(allocated_discovery_slots_);
    for (size_t i = 0; i < allocated_discovery_slots_; i++) {
        SlotAllocation slot;
        slot.slot_number = i;
        slot.target_address = kBroadcastAddress;  // Discovery to broadcast
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
                // Keep sync beacon slots active for synchronization
                active_slots++;
                break;

            case SlotAllocation::SlotType::SYNC_BEACON_TX:
                // Do not send sync beacon when joining
                slot.type = SlotAllocation::SlotType::SYNC_BEACON_RX;
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

    float duty_cycle = (float)active_slots / slot_count_ * 100.0f;

    LOG_INFO(
        "Set joining slots: %zu active + %zu sleep = %zu total (%.1f%% duty "
        "cycle) - synchronized with network",
        active_slots, slot_count_ - active_slots, slot_count_, duty_cycle);

    return Result::Success();
}

void NetworkService::ExpandSyncBeaconListening() {
    uint8_t sync_beacon_slots =
        static_cast<uint8_t>(current_network_depth_ + 1);
    uint16_t limit =
        std::min(static_cast<uint16_t>(sync_beacon_slots), slot_count_);

    for (uint16_t i = 0; i < limit; i++) {
        auto& slot = slot_table_[i];
        if (slot.type ==
                types::protocols::lora_mesh::SlotAllocation::SlotType::SLEEP ||
            slot.type == types::protocols::lora_mesh::SlotAllocation::SlotType::
                             SYNC_BEACON_TX) {
            slot.type = types::protocols::lora_mesh::SlotAllocation::SlotType::
                SYNC_BEACON_RX;
        }
    }

    LOG_WARNING(
        "Expanded sync beacon listening to all %d sync slots after %d "
        "missed beacons",
        limit, no_received_sync_beacon_count_);
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
    // NODE_ONLY nodes never create a network.
    if (node_role_ == NodeRole::NODE_ONLY) {
        return Result::Success();
    }

    uint32_t current_time = GetRTOS().getTickCount();
    uint32_t end_time = discovery_start_time_ + timeout_ms;

    // NETWORK_MANAGER-role nodes that surrendered in an election enter
    // DISCOVERY to listen for the winner's beacon. If the winner is alive,
    // the beacon triggers JOINING before the timeout. If the winner is dead
    // (no beacon received), clear the surrender flag and restart election
    // via FAULT_RECOVERY so the node can create its own network.
    if (node_role_ == NodeRole::NETWORK_MANAGER && surrendered_in_election_) {
        if (current_time >= end_time) {
            LOG_INFO(
                "Discovery timeout after surrender — clearing flag and "
                "entering FAULT_RECOVERY for fresh election");
            surrendered_in_election_ = false;
            SetState(ProtocolState::FAULT_RECOVERY);
            StartElectionBackoff();
        }
        return Result::Success();
    }

    // Check if discovery timeout has elapsed (AUTO and unsurrendered NM roles)
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

uint32_t NetworkService::GetNMElectionTimeout() const {
    uint32_t window_ms =
        superframe_service_ ? 2 * superframe_service_->GetSlotDuration() : 2000;
    if (nm_election_start_time_ == 0) {
        return window_ms;
    }
    uint32_t end_time = nm_election_start_time_ + window_ms;
    uint32_t now = GetRTOS().getTickCount();
    return (now < end_time) ? (end_time - now) : 0;
}

Result NetworkService::PerformNMElection() {
    if (state_ != ProtocolState::NM_ELECTION) {
        return Result::Success();
    }
    if (GetNMElectionTimeout() == 0) {
        // A node that surrendered to a higher-priority claimant must not
        // re-create a network — the winner is still out there. Enter
        // DISCOVERY to keep listening for the winner's sync beacon.
        if (surrendered_in_election_) {
            LOG_INFO(
                "NM_ELECTION deadline reached but previously surrendered — "
                "entering DISCOVERY instead of creating network");
            network_found_ = false;
            network_creator_ = false;
            selected_sponsor_ = 0;
            discovery_start_time_ = GetRTOS().getTickCount();
            SetDiscoverySlots();
            SetState(ProtocolState::DISCOVERY);
            return Result::Success();
        }
        LOG_INFO("NM_ELECTION deadline reached, creating network");
        return CreateNetwork();
    }
    return Result::Success();
}

void NetworkService::SetNumberOfSlotsPerSuperframe(uint8_t slots) {
    number_of_slots_per_superframe_ = slots;
}

void NetworkService::SetMaxHopCount(uint8_t max_hops) {
    current_network_depth_ = max_hops;
}

uint8_t NetworkService::GetHopDistanceToNM() const {
    if (network_manager_ == node_address_) {
        return 0;  // We are the Network Manager
    }

    // Find network manager in routing table
    const auto& nodes = routing_table_->GetNodes();
    auto nm_it = std::find_if(
        nodes.begin(), nodes.end(),
        [this](const types::protocols::lora_mesh::NetworkNodeRoute& node) {
            return node.routing_entry.destination == network_manager_;
        });
    if (nm_it != nodes.end()) {
        return nm_it->routing_entry.hop_count;
    }

    // If we don't know our distance, default to 1
    return 1;
}

// Helper method implementations

std::pair<bool, uint8_t> NetworkService::ShouldAcceptJoin(
    AddressType node_address, uint8_t requested_slots, uint8_t hops,
    size_t pending_node_count, uint8_t pending_slot_count) {

    // Check network capacity including already-pending joins
    if (routing_table_->GetSize() + pending_node_count >=
        config_.max_network_nodes) {
        LOG_WARNING("Network at capacity, rejecting node 0x%04X", node_address);
        return {false, 0};
    }

    if (hops > config_.max_hops) {
        LOG_WARNING("Node 0x%04X is too far (hops %d), rejecting", node_address,
                    hops);
        return {false, 0};
    }

    // Check available slots accounting for pending joins
    uint8_t allocated_data_slots = GetAllocatedDataSlots();
    uint8_t total_committed =
        (allocated_data_slots + pending_slot_count > config_.max_network_nodes)
            ? config_.max_network_nodes
            : allocated_data_slots + pending_slot_count;
    uint8_t available_slots = config_.max_network_nodes - total_committed;
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
    for (uint16_t i = start_slot; i < slot_count_; i++) {
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
    uint8_t total_allocated = 0;
    bool is_self_active = false;
    const auto& nodes = routing_table_->GetNodes();
    for (const auto& node : nodes) {
        total_allocated += node.GetAllocatedDataSlots();
        if (node.GetAddress() == node_address_) {
            is_self_active = true;
        }
    }
    if (!is_self_active) {
        total_allocated += local_allocated_data_slots_;
    }

    return total_allocated;
}

uint32_t NetworkService::GetJoinTimeout() {
    if (!superframe_service_) {
        return 60000;
    }

    return superframe_service_->GetSuperframeDuration() * 3;
}

Result NetworkService::ProcessSyncBeacon(const BaseMessage& message,
                                         uint32_t reception_timestamp) {
    // NETWORK_MANAGER special case: only react to foreign-network beacons.
    // We never synchronize to another node's superframe when we are the NM,
    // but we do need to detect and respond to foreign networks.
    if (state_ == ProtocolState::NETWORK_MANAGER) {
        auto nm_beacon_opt = SyncBeaconMessage::CreateFromBaseMessage(message);
        if (nm_beacon_opt.has_value()) {
            const auto& sb = nm_beacon_opt.value();
            uint16_t bid = sb.GetNetworkId();
            if (network_id_ != 0 && bid != 0 && bid != network_id_) {
                HandleForeignBeacon(sb);
            }
        }
        return Result::Success();
    }

    // Process sync beacons in discovery, joining, normal operation, fault
    // recovery, and NM election states.
    if (state_ != ProtocolState::DISCOVERY &&
        state_ != ProtocolState::JOINING &&
        state_ != ProtocolState::NORMAL_OPERATION &&
        state_ != ProtocolState::FAULT_RECOVERY &&
        state_ != ProtocolState::NM_ELECTION) {
        LOG_DEBUG("Ignoring sync beacon in state %d", static_cast<int>(state_));
        return Result::Success();
    }

    // Deserialize sync beacon message
    auto sync_beacon_opt = SyncBeaconMessage::CreateFromBaseMessage(message);
    if (!sync_beacon_opt.has_value()) {
        LOG_ERROR("Failed to deserialize sync beacon message");
        return Result::Error(LoraMesherErrorCode::kSerializationError);
    }

    const auto& sync_beacon = sync_beacon_opt.value();

    LOG_DEBUG("Received sync beacon from 0x%04X, hop count %d at timestamp %u",
              sync_beacon.GetSource(), sync_beacon.GetHopCount(),
              reception_timestamp);

    uint32_t current_time = GetRTOS().getTickCount();
    if (current_time < last_sync_beacon_received_ +
                           superframe_service_->GetSuperframeDuration() / 2 &&
        last_sync_beacon_received_ != 0) {
        LOG_DEBUG("Received sync beacon too soon after previous one, ignoring");
        return Result::Success();
    }

    last_sync_beacon_received_ = current_time;

    // Foreign-beacon guard for stable operating states: don't silently adopt a
    // different network's identity.  DISCOVERY / FAULT_RECOVERY / NM_ELECTION
    // may join any live network, so they fall through.
    {
        uint16_t bid = sync_beacon.GetNetworkId();
        if (network_id_ != 0 && bid != 0 && bid != network_id_) {
            if (state_ == ProtocolState::NORMAL_OPERATION ||
                state_ == ProtocolState::JOINING) {
                LOG_DEBUG(
                    "Ignoring foreign SYNC_BEACON (net 0x%04X vs ours "
                    "0x%04X)",
                    bid, network_id_);
                return Result::Success();
            }
            // DISCOVERY, FAULT_RECOVERY, NM_ELECTION: fall through and join
            // whichever live network we detected.
        }
    }

    {
        std::lock_guard<std::mutex> lock(network_mutex_);
        bool params_changed = false;

        // Update network manager from the sync beacon header
        AddressType beacon_nm = sync_beacon.GetNetworkManager();
        if (network_manager_ != beacon_nm) {
            network_manager_ = beacon_nm;
            params_changed = true;
            LOG_INFO("Updated network manager to 0x%04X from sync beacon",
                     beacon_nm);
        }

        // Preserve stable network_id_ from beacon (survives NM elections)
        uint16_t beacon_network_id = sync_beacon.GetNetworkId();
        if (beacon_network_id != 0 && network_id_ != beacon_network_id) {
            network_id_ = beacon_network_id;
            LOG_INFO("Stored network_id 0x%04X from sync beacon", network_id_);
        }

        // Cancel any pending election — a live NM is broadcasting
        if ((state_ == ProtocolState::FAULT_RECOVERY ||
             state_ == ProtocolState::NM_ELECTION) &&
            election_end_time_ != 0) {
            LOG_INFO(
                "Cancelling NM election: received sync beacon from NM 0x%04X",
                beacon_nm);
            election_end_time_ = 0;
        }

        // Store max_hops from the sync beacon for slot allocation calculations
        uint8_t beacon_max_hops = sync_beacon.GetMaxHops();
        if (beacon_max_hops != current_network_depth_) {
            SetMaxHopCount(beacon_max_hops);
            params_changed = true;
            LOG_DEBUG("Updated network max_hops to %d from sync beacon",
                      current_network_depth_);
        }

        uint8_t total_slots = sync_beacon.GetTotalSlots();
        if (total_slots != number_of_slots_per_superframe_) {
            SetNumberOfSlotsPerSuperframe(total_slots);
            params_changed = true;
            LOG_DEBUG(
                "Updated number_of_slots_per_superframe_ to %d from sync "
                "beacon",
                number_of_slots_per_superframe_);
        }

        // Store authoritative node count from NM's sync beacon
        uint8_t node_count = sync_beacon.GetNodeCount();
        if (node_count != beacon_node_count_) {
            beacon_node_count_ = node_count;
            params_changed = true;
            LOG_DEBUG("Updated beacon_node_count_ to %d from sync beacon",
                      beacon_node_count_);
        }

        if (params_changed) {
            slot_table_dirty_ = true;
        }
    }

    // If we were electing ourselves and got a beacon, pivot to DISCOVERY so the
    // block below triggers joining to the real NM.
    if (state_ == ProtocolState::NM_ELECTION) {
        selected_sponsor_ = 0;  // reset so sponsor is picked from this beacon
        SetState(ProtocolState::DISCOVERY);
    }

    // Add/update route to NM based on sync beacon info
    // Our hop distance to NM = beacon's hop_count + 1 (we're one hop further than the forwarder)
    AddressType beacon_source = sync_beacon.GetSource();
    AddressType beacon_nm = sync_beacon.GetNetworkManager();
    uint8_t our_hop_count_to_nm = sync_beacon.GetHopCount() + 1;
    // Always refresh the NM route on beacon receipt.
    // UpdateRoute() internally handles "don't overwrite a better active route",
    // so this is safe even when the NM route is already active.
    // Removing the IsNodePresent() guard fixes the case where a stale node entry
    // (is_active=false) blocks the route refresh, leaving IsDirectNeighbor() false.
    if (beacon_nm != node_address_) {
        current_time = GetRTOS().getTickCount();
        // Use direct physical link quality if available, otherwise default 200.
        // GetDirectLinkQuality returns link_stats.CalculateQuality() (EWMA +
        // unidirectional penalty), not the route quality which may reflect a
        // good multi-hop path.
        uint8_t beacon_quality =
            routing_table_->GetDirectLinkQuality(beacon_source);
        if (beacon_quality == 0) {
            beacon_quality = 200;  // First beacon, no stats yet
        }
        bool route_updated = routing_table_->UpdateRoute(
            beacon_source,        // next_hop: go through the beacon forwarder
            beacon_nm,            // destination: the network manager
            our_hop_count_to_nm,  // hop_count: beacon's hop_count + 1
            beacon_quality,       // link_quality: measured or default
            config_.default_data_slots,  // allocated_data_slots
            0,                           // capabilities: unknown
            current_time);

        if (route_updated) {
            LOG_DEBUG("Updated route to NM 0x%04X via 0x%04X, hop_count=%d",
                      beacon_nm, beacon_source, our_hop_count_to_nm);
        }
    }

    // Special handling for DISCOVERY state: sync beacon indicates existing network
    if (state_ == ProtocolState::DISCOVERY) {
        AddressType source = sync_beacon.GetSource();

        // Sponsor selection: Use first sync beacon sender as sponsor
        if (selected_sponsor_ == 0) {
            selected_sponsor_ = source;
            LOG_INFO(
                "Selected sponsor node 0x%04X from first sync beacon received",
                selected_sponsor_);
        }

        auto network_id = sync_beacon.GetNetworkId();
        LOG_INFO("Discovery: Found existing network with id 0x%04X",
                 network_id);

        AddressType network_manager = sync_beacon.GetNetworkManager();

        // Trigger transition to JOINING state
        LOG_INFO("Transitioning from DISCOVERY to JOINING for network 0x%04X",
                 network_id);

        Result join_result = StartJoining(network_manager, GetJoinTimeout());
        if (!join_result.IsSuccess()) {
            LOG_ERROR("Failed to start joining process: %s",
                      join_result.GetErrorMessage().c_str());
        }
        // Add the source as a direct neighbor, but only mark as NM if it actually is
        bool is_network_manager = (source == network_manager);
        UpdateNetworkNode(source, 100, is_network_manager,
                          config_.default_data_slots);

        // CRITICAL FIX: Perform timing synchronization BEFORE transitioning to JOINING
        Result sync_result = PerformTimingSynchronization(
            sync_beacon, reception_timestamp, "Discovery");
        if (!sync_result.IsSuccess()) {
            LOG_WARNING("Discovery timing synchronization failed: %s",
                        sync_result.GetErrorMessage().c_str());
            // Continue with joining process even if synchronization fails
        }

        no_received_sync_beacon_count_ = 0;  // Reset missed beacon counter

        return join_result;
    }

    if (slot_table_dirty_) {
        Result result = UpdateSlotTable();
        if (!result.IsSuccess()) {
            LOG_ERROR("Failed to update slot table: %s",
                      result.GetErrorMessage().c_str());
            return result;
        }
    }

    // Capture forwarding decision and slot duration before stopping the
    // superframe service, so we can queue the beacon inside the pre_start_action
    // callback (while the service is still stopped) — eliminating the race where
    // the update task fires the SYNC_BEACON_TX slot handler before
    // ForwardSyncBeacon() has had a chance to enqueue the beacon.
    bool should_forward = ShouldForwardSyncBeacon(sync_beacon);
    uint32_t slot_duration = sync_beacon.GetSlotDuration();

    // Perform timing synchronization with Network Manager
    Result sync_result = PerformTimingSynchronization(
        sync_beacon, reception_timestamp, "Normal", [&]() {
            // Invoked after SynchronizeWith() but before StartSuperframe(),
            // so the update task cannot run yet — no race condition possible.
            if (should_forward) {
                Result forward_result =
                    ForwardSyncBeacon(sync_beacon, slot_duration);
                if (!forward_result.IsSuccess()) {
                    LOG_WARNING("Failed to forward sync beacon: %s",
                                forward_result.GetErrorMessage().c_str());
                }
            }
        });
    if (!sync_result.IsSuccess()) {
        LOG_WARNING("Normal operation timing synchronization failed: %s",
                    sync_result.GetErrorMessage().c_str());
        // Continue even if synchronization fails
    }

    no_received_sync_beacon_count_ = 0;  // Reset missed beacon counter

    return Result::Success();
}

void NetworkService::SetSyncBeaconPreSendCallback(BaseMessage& base_msg) {
    base_msg.SetPreSendCallback([this](BaseMessage& msg) {
        constexpr uint32_t kSerializationOverheadMs = 1;
        uint32_t actual_time =
            superframe_service_->GetTimeSinceSuperframeStart() +
            kSerializationOverheadMs;

        auto payload = msg.MutablePayload();
        constexpr size_t kOffset =
            SyncBeaconHeader::kPropagationDelayPayloadOffset;
        if (payload.size() < kOffset + sizeof(uint32_t)) {
            LOG_ERROR(
                "Pre-send callback: payload too small for propagation delay");
            return;
        }
        std::memcpy(payload.data() + kOffset, &actual_time, sizeof(uint32_t));

        LOG_DEBUG("Pre-send callback: updated propagation_delay to %u ms",
                  actual_time);
    });
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
    uint16_t total_slots = static_cast<uint16_t>(slot_count_);
    if (total_slots == 0) {
        total_slots = 20;  // Fallback default
        LOG_WARNING("Slot table empty, using default total slots: %d",
                    total_slots);
    }

    // Create original sync beacon with placeholder propagation_delay (0)
    // The actual timing will be captured by the pre-send callback right before transmission
    auto sync_beacon_opt = SyncBeaconMessage::CreateOriginal(
        kBroadcastAddress,  // Broadcast destination
        node_address_,      // Network manager as source
        network_id_,        // Stable network identifier (survives NM elections)
        total_slots,        // Actual total slots from slot table
        static_cast<uint16_t>(superframe_service_->GetSlotDuration()),
        node_address_,  // Network manager address
        0,              // Placeholder - will be updated by callback
        std::min(
            static_cast<uint8_t>(current_network_depth_),
            config_
                .max_hops),  // Dynamic growth (depth+1) capped by configured limit
        allocated_control_slots_);  // Authoritative node count for slot alignment

    if (!sync_beacon_opt.has_value()) {
        LOG_ERROR("Failed to create sync beacon message");
        return Result::Error(LoraMesherErrorCode::kConfigurationError);
    }

    // Convert to base message and queue for transmission
    BaseMessage base_msg = sync_beacon_opt.value().ToBaseMessage();

    // Set callback to update propagation_delay right before transmission
    SetSyncBeaconPreSendCallback(base_msg);

    // Add to sync beacon TX queue - protocol layer handles actual transmission
    auto base_msg_ptr = std::make_unique<BaseMessage>(std::move(base_msg));
    message_queue_service_->AddMessageToQueue(
        types::protocols::lora_mesh::SlotAllocation::SlotType::SYNC_BEACON_TX,
        std::move(base_msg_ptr));

    LOG_INFO("Queued sync beacon for transmission: %d total slots, %d max hops",
             total_slots, current_network_depth_);
    return Result::Success();
}

Result NetworkService::ForwardSyncBeacon(
    const SyncBeaconMessage& original_beacon, uint32_t processing_delay) {
    // Create forwarded beacon from the original
    auto forwarded_beacon_opt = original_beacon.CreateForwardedBeacon(
        node_address_, processing_delay, config_.guard_time_ms);

    if (!forwarded_beacon_opt.has_value()) {
        LOG_ERROR("Failed to create forwarded sync beacon");
        return Result::Error(LoraMesherErrorCode::kConfigurationError);
    }

    // Convert to base message and queue for transmission
    BaseMessage base_msg = forwarded_beacon_opt.value().ToBaseMessage();

    // Set pre-send callback to capture actual TX time including subslot wait.
    // CreateForwardedBeacon sets an initial propagation_delay estimate, but the
    // callback overwrites it with GetTimeSinceSuperframeStart() right before TX,
    // which naturally includes any subslot delay that has elapsed.
    SetSyncBeaconPreSendCallback(base_msg);

    // Clear any stale beacon before queuing the fresh one
    message_queue_service_->ClearQueue(
        types::protocols::lora_mesh::SlotAllocation::SlotType::SYNC_BEACON_TX);

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
    if (state_ != ProtocolState::NORMAL_OPERATION) {
        return false;
    }

    // Forward any beacon that hasn't exceeded max propagation distance.
    // The rate limiter in ProcessSyncBeacon ensures only the first beacon per
    // superframe is processed, so hop-layer filtering is unnecessary and
    // harmful for mobile nodes whose routing-table distance may be stale.
    bool should_forward = beacon.GetHopCount() < beacon.GetMaxHops();

    if (should_forward) {
        LOG_DEBUG("Will forward sync beacon: beacon hop %d, max_hops %d",
                  beacon.GetHopCount(), beacon.GetMaxHops());
    } else {
        LOG_DEBUG("Not forwarding: hop count %d reached max_hops %d",
                  beacon.GetHopCount(), beacon.GetMaxHops());
    }

    return should_forward;
}

Result NetworkService::HandleSuperframeStart() {
    // Link quality tracking for active operational states only
    if (state_ == ProtocolState::NETWORK_MANAGER ||
        state_ == ProtocolState::NORMAL_OPERATION) {
        ScheduleRoutingMessageExpectations();
    }

    // Periodic route cleanup runs in all states — stale routes should
    // expire regardless of protocol state (e.g., after entering
    // FAULT_RECOVERY or DISCOVERY following neighbor loss)
    {
        uint32_t current_time = GetRTOS().getTickCount();
        if ((current_time - last_cleanup_time_) >= kCleanupIntervalMs) {
            last_cleanup_time_ = current_time;
            RemoveInactiveNodes();
        }
    }

    // Sync beacon transmission is handled by slot-based processing
    // in ProcessSlotMessages() when SYNC_BEACON_TX slot is encountered.
    // This prevents duplicate transmissions at superframe start.
    if (state_ == ProtocolState::NETWORK_MANAGER &&
        network_manager_ == node_address_) {
        Result result = ApplyPendingJoin();
        if (!result) {
            return result;
        }

        // Update current_network_depth_ from routing table
        uint8_t routing_max_hops = GetMaxHopsFromRoutingTable();
        if (routing_max_hops != current_network_depth_) {
            SetMaxHopCount(routing_max_hops);
            LOG_INFO("Updated network max_hops to %d from routing table",
                     current_network_depth_);
            pending_slot_table_rebuild_ = true;
        }

        // Flush any deferred slot table rebuild (e.g., from mid-superframe slot requests).
        // This runs independently of ApplyPendingJoin; if both were pending simultaneously,
        // ApplyPendingJoin already cleared the flag and called UpdateSlotTable().
        if (pending_slot_table_rebuild_) {
            pending_slot_table_rebuild_ = false;
            Result rebuild_result = UpdateSlotTable();
            if (!rebuild_result) {
                LOG_ERROR("Failed to update slot table for pending join: %s",
                          rebuild_result.GetErrorMessage().c_str());
                return rebuild_result;
            }
        }

        LOG_DEBUG(
            "Network Manager superframe start - sync beacon will be sent in "
            "slot 0");

    } else if (state_ == ProtocolState::JOINING) {
        // Exponential backoff for join retries (Slotted ALOHA)
        if (join_backoff_remaining_ > 0) {
            join_backoff_remaining_--;
            LOG_DEBUG("Join backoff: %d superframes remaining",
                      join_backoff_remaining_);
        } else {
            Result join_req_result =
                SendJoinRequest(network_manager_, config_.default_data_slots);
            if (!join_req_result) {
                LOG_ERROR("Failed to resend JoinRequest: %s",
                          join_req_result.GetErrorMessage().c_str());
            }
            join_retry_count_++;
            // Binary exponential backoff capped at 4 superframes to ensure
            // convergence in dense networks (e.g., 9 nodes, 5 subslots).
            uint8_t max_backoff = std::min(
                static_cast<uint8_t>(
                    1 << std::min(join_retry_count_, static_cast<uint8_t>(2))),
                static_cast<uint8_t>(4));

            // Always wait at least 1 superframe so the sponsor has time to deliver
            // the JOIN_RESPONSE before the joining node retransmits
            join_backoff_remaining_ =
                1 + GetRTOS().GetRandom() % (max_backoff + 1);
            LOG_DEBUG("Join retry #%d, next backoff: %d superframes",
                      join_retry_count_, join_backoff_remaining_);
        }

    } else if (state_ == ProtocolState::NORMAL_OPERATION) {
        no_received_sync_beacon_count_++;
        // If no received sync beacon for x times set to FaultRecovery
        if (no_received_sync_beacon_count_ >= kMaxNoReceivedSyncBeacons) {
            LOG_WARNING(
                "No received sync beacon for %d times, entering FaultRecovery",
                kMaxNoReceivedSyncBeacons);
            SetState(ProtocolState::FAULT_RECOVERY);
            StartElectionBackoff();
        } else if (no_received_sync_beacon_count_ >=
                   kExpandListeningThreshold) {
            ExpandSyncBeaconListening();
        }
    } else if (state_ == ProtocolState::FAULT_RECOVERY) {
        // Decrement election backoff if one is pending
        if (election_end_time_ != 0) {
            uint32_t now = GetRTOS().getTickCount();
            if (now >= election_end_time_) {
                LOG_INFO(
                    "Election backoff expired (priority=%d), entering "
                    "NM_ELECTION",
                    election_priority_);
                election_end_time_ = 0;
                // Switch to discovery slots so the NM_CLAIM is sent through
                // the DISCOVERY_RX fallback TX path in this same slot
                SetDiscoverySlots();
                SendNMClaim();  // queue claim for next DISCOVERY_TX slot
                nm_election_start_time_ = GetRTOS().getTickCount();
                SetState(ProtocolState::NM_ELECTION);
            }
        }
    }

    return Result::Success();
}

Result NetworkService::ApplyPendingJoin() {
    // Only apply if we're the network manager and have pending joins
    if (state_ != ProtocolState::NETWORK_MANAGER ||
        network_manager_ != node_address_ || pending_joins_.empty()) {
        return Result::Success();
    }

    LOG_INFO("Applying %zu pending join request(s) at superframe boundary",
             pending_joins_.size());

    // Re-establish routes for all pending joins before rebuilding the slot table.
    // RemoveInactiveNodes() (called just before this function) may have expired
    // routes (is_active=false), causing IsDirectNeighbor() → false →
    // SLEEP instead of RX in UpdateSlotTable().
    for (const auto& pending : pending_joins_) {
        auto source = pending.GetSource();
        auto allocated_slots = pending.GetRequestedSlots();
        AddressType sponsor = pending.GetHeader().GetSponsorAddress();
        bool has_external_sponsor = (sponsor != 0 && sponsor != node_address_);
        AddressType route_next_hop;
        if (has_external_sponsor) {
            route_next_hop = routing_table_->FindNextHop(sponsor);
            if (route_next_hop == 0)
                route_next_hop = sponsor;
        } else {
            route_next_hop = source;
        }
        uint8_t routing_hop_count =
            pending.GetHopCount() + (has_external_sponsor ? 2 : 1);
        routing_table_->UpdateRoute(route_next_hop, source, routing_hop_count,
                                    255, allocated_slots, 0,
                                    GetRTOS().getTickCount());
        LOG_INFO(
            "Re-established route to joining node 0x%04X via 0x%04X (hops=%d)",
            source, route_next_hop, routing_hop_count);
    }

    // ApplyPendingJoin calls UpdateSlotTable() below; clear deferred flag to prevent a
    // redundant rebuild in HandleSuperframeStart() when both are pending simultaneously.
    pending_slot_table_rebuild_ = false;

    // Single UpdateSlotTable call after all routes are established
    Result result = UpdateSlotTable();
    if (!result) {
        LOG_ERROR("Failed to update slot table for pending joins: %s",
                  result.GetErrorMessage().c_str());
        pending_joins_.clear();
        return result;
    }

    for (const auto& pending : pending_joins_) {
        LOG_INFO("Node 0x%04X successfully added to network with %d slots",
                 pending.GetSource(), pending.GetRequestedSlots());
    }

    pending_joins_.clear();

    return Result::Success();
}

Result NetworkService::ForwardJoinRequest(
    const JoinRequestMessage& join_request) {
    if (state_ != ProtocolState::NORMAL_OPERATION &&
        state_ != ProtocolState::NETWORK_MANAGER) {
        LOG_WARNING("Ignoring join request in state: %d", state_);
        return Result::Success();
    }

    // Best-effort slot conversion; DISCOVERY_RX fallback TX handles delivery
    // regardless, so don't abort forwarding if no slot can be converted.
    ScheduleDiscoverySlotForwarding();

    // Calculate next hop towards network manager using routing table
    AddressType next_hop = routing_table_->FindNextHop(network_manager_);
    if (next_hop == 0) {
        // No route to network manager, try direct connection
        next_hop = network_manager_;
        LOG_WARNING(
            "No route to network manager 0x%04X, attempting direct connection",
            network_manager_);
        // TODO: This should be an error and remove the actual message.
    }

    // Create forwarded join request preserving original source and sponsor
    // Increment hop_count to track how many hops the message has traveled
    auto forwarded_request = JoinRequestMessage::Create(
        join_request.GetDestination(),
        join_request
            .GetSource(),  // Preserve original source for end-to-end tracking
        join_request.GetBatteryLevel(), join_request.GetRequestedSlots(),
        {},        // No additional info
        next_hop,  // Set next hop for routing
        join_request.GetHeader()
            .GetSponsorAddress(),       // Preserve sponsor address
        join_request.GetHopCount() + 1  // Increment hop count for forwarding
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
        "next hop 0x%04X (sponsor: 0x%04X, hop_count: %d)",
        forwarded_request->GetSource(), network_manager_, next_hop,
        forwarded_request->GetHeader().GetSponsorAddress(),
        forwarded_request->GetHopCount());

    return Result::Success();
}

Result NetworkService::ForwardJoinResponseToSponsoredNode(
    const JoinResponseMessage& join_response) {
    if (state_ != ProtocolState::NORMAL_OPERATION &&
        state_ != ProtocolState::NETWORK_MANAGER) {
        LOG_WARNING("Ignoring join response forwarding in state: %d", state_);
        return Result::Success();
    }

    // Get the final target node address (stored in target_address field)
    AddressType joining_node = join_response.GetHeader().GetTargetAddress();

    // If the response is ACCEPTED, add the joining node as a direct neighbor
    // This is CRITICAL: as sponsor, we have a direct link to the joining node
    // We must add this route BEFORE forwarding so routing tables are consistent
    if (join_response.GetStatus() ==
            JoinResponseHeader::ResponseStatus::ACCEPTED &&
        joining_node != 0) {
        routing_table_->UpdateRoute(
            joining_node,  // source: the joining node itself
            joining_node,  // destination: the joining node
            1,             // hop_count: direct neighbor (1 hop)
            200,           // link_quality: good quality for direct link
            join_response.GetAllocatedSlots(),  // allocated_data_slots
            0,                                  // capabilities: unknown yet
            GetRTOS().getTickCount());
        LOG_DEBUG(
            "Sponsor added joining node 0x%04X as direct neighbor (hops=1)",
            joining_node);
    }

    // Sanity check: we should be the current destination (sponsor)
    if (join_response.GetDestination() != node_address_) {
        LOG_ERROR(
            "Attempted to forward join response but we're not the intended "
            "recipient (0x%04X)",
            join_response.GetDestination());
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Not the intended recipient for this response");
    }

    // Sanity check: target address should be set for sponsored responses
    if (joining_node == 0) {
        LOG_WARNING(
            "Join response has no target address, treating as direct delivery");
        return Result::Success();
    }

    // Create final join response for the joining node (remove sponsor information)
    auto final_response = JoinResponseMessage::Create(
        joining_node,               // Direct to joining node now
        join_response.GetSource(),  // From network manager
        join_response.GetNetworkId(), join_response.GetAllocatedSlots(),
        join_response.GetStatus(), {},       // No additional info
        joining_node,                        // Next hop is the joining node
        0,                                   // No sponsor (final delivery)
        join_response.GetControlSlotIndex()  // Control slot index
    );

    if (!final_response) {
        LOG_ERROR("Failed to create final join response for sponsored node");
        return Result(LoraMesherErrorCode::kMemoryError,
                      "Failed to create final join response");
    }

    // Queue for transmission
    auto base_msg =
        std::make_unique<BaseMessage>(final_response->ToBaseMessage());
    message_queue_service_->AddMessageToQueue(
        SlotAllocation::SlotType::DISCOVERY_TX, std::move(base_msg));

    LOG_INFO(
        "Forwarded join response to sponsored node 0x%04X: status=%d, slots=%d",
        joining_node, static_cast<int>(join_response.GetStatus()),
        join_response.GetAllocatedSlots());

    return Result::Success();
}

Result NetworkService::ForwardJoinResponse(
    const JoinResponseMessage& join_response) {
    if (state_ != ProtocolState::NORMAL_OPERATION &&
        state_ != ProtocolState::NETWORK_MANAGER) {
        LOG_WARNING("Ignoring join response forwarding in state: %d", state_);
        return Result::Success();
    }

    // Best-effort slot conversion; DISCOVERY_RX fallback TX handles delivery
    // regardless, so don't abort forwarding if no slot can be converted.
    ScheduleDiscoverySlotForwarding();

    // Calculate next hop toward destination (sponsor)
    AddressType dest = join_response.GetDestination();
    AddressType next_hop = routing_table_->FindNextHop(dest);
    if (next_hop == 0) {
        next_hop = dest;  // Direct if no route found
        LOG_WARNING("No route to sponsor 0x%04X, attempting direct", dest);
    }

    // If accepted, add the joining node to our routing table with correct hop count
    // The joining node is reached via the sponsor, so its hop count = sponsor's hop count + 1
    AddressType joining_node = join_response.GetHeader().GetTargetAddress();
    if (join_response.GetStatus() ==
            JoinResponseHeader::ResponseStatus::ACCEPTED &&
        joining_node != 0) {
        auto sponsor_route = routing_table_->GetNode(dest);
        if (sponsor_route != routing_table_->GetNodes().end()) {
            uint8_t hops_to_joining =
                sponsor_route->routing_entry.hop_count + 1;
            routing_table_->UpdateRoute(
                next_hop,  // source: learned via sponsor path but add next_hop
                joining_node,     // destination: the joining node
                hops_to_joining,  // hop_count: sponsor's hops + 1
                200,              // link_quality
                join_response.GetAllocatedSlots(),  // allocated_data_slots
                0,                                  // capabilities: unknown yet
                GetRTOS().getTickCount());
            LOG_DEBUG(
                "Added joining node 0x%04X with hops=%d (via sponsor 0x%04X)",
                joining_node, hops_to_joining, dest);
        }
    }

    // Create forwarded response with updated next_hop
    auto forwarded = JoinResponseMessage::Create(
        dest, join_response.GetSource(), join_response.GetNetworkId(),
        join_response.GetAllocatedSlots(), join_response.GetStatus(),
        {},        // No additional info
        next_hop,  // Updated next hop
        join_response.GetHeader().GetTargetAddress(),
        join_response.GetControlSlotIndex()  // Control slot index
    );

    if (!forwarded) {
        LOG_ERROR("Failed to create forwarded join response");
        return Result(LoraMesherErrorCode::kMemoryError,
                      "Failed to create forwarded join response");
    }

    auto base_msg = std::make_unique<BaseMessage>(forwarded->ToBaseMessage());
    message_queue_service_->AddMessageToQueue(
        SlotAllocation::SlotType::DISCOVERY_TX, std::move(base_msg));

    LOG_INFO(
        "Forwarded join response to 0x%04X via next hop 0x%04X (target: "
        "0x%04X)",
        dest, next_hop, join_response.GetHeader().GetTargetAddress());

    return Result::Success();
}

bool NetworkService::ScheduleDiscoverySlotForwarding() {
    // Find the next DISCOVERY_RX slot and temporarily convert it to TX
    // When next slot allocation the DISCOVERY_TX slot will be replaced by
    // a DISCOVERY_RX as previously set.
    for (auto& slot : slot_table_) {
        if (slot.type == SlotAllocation::SlotType::DISCOVERY_RX) {
            // Temporarily convert this slot to TX for forwarding
            slot.type = SlotAllocation::SlotType::DISCOVERY_TX;
            slot.target_address = network_manager_;

            LOG_DEBUG("Scheduled discovery slot %d for forwarding to 0x%04X",
                      slot.slot_number, network_manager_);

            return true;
        }
    }

    LOG_WARNING("No available DISCOVERY_RX slots found for forwarding");
    return false;
}

void NetworkService::ResetNetworkState() {
    std::lock_guard<std::mutex> lock(network_mutex_);

    // Store count before clearing for logging
    size_t node_count = routing_table_->GetSize();
    size_t slot_count = slot_count_;

    // Clear network topology data
    routing_table_->Clear();
    slot_count_ = 0;

    // Reset state variables
    network_found_ = false;
    network_creator_ = false;
    is_synchronized_ = false;
    network_manager_ = 0;
    local_allocated_data_slots_ = 0;

    // Reset timing variables
    discovery_start_time_ = 0;
    joining_start_time_ = 0;
    last_sync_time_ = 0;
    last_cleanup_time_ = 0;

    // Clear join data
    pending_joins_.clear();

    // Reset message de-duplication state
    message_cache_.fill({});
    message_cache_head_ = 0;
    message_seq_ = 0;

    // Reset to initial state
    SetState(ProtocolState::INITIALIZING);

    LOG_DEBUG("Network state reset - cleared %zu nodes and %zu slots",
              node_count, slot_count);
}

uint8_t NetworkService::GetMaxHopsFromRoutingTable() const {
    uint8_t max_hop_count = 0;
    const auto& nodes = routing_table_->GetNodes();
    for (const auto& node : nodes) {
        if (!node.is_active)
            continue;  // skip stale entries
        auto hop_count = node.routing_entry.hop_count;
        if (hop_count > max_hop_count) {
            max_hop_count = hop_count;
        }
    }

    return max_hop_count;
}

uint8_t NetworkService::FindLowestAvailableControlSlot() {
    std::set<uint8_t> used_indices;
    if (my_control_slot_index_ != 0xFF) {
        used_indices.insert(my_control_slot_index_);  // NM's own slot (0)
    }
    for (const auto& node : routing_table_->GetNodes()) {
        if (node.control_slot_index != 0xFF) {
            used_indices.insert(node.control_slot_index);
        }
    }
    // Find lowest gap
    for (uint8_t i = 0; i < 255; i++) {
        if (used_indices.find(i) == used_indices.end()) {
            return i;
        }
    }
    return 255;  // Fallback (shouldn't happen with <255 nodes)
}

// ---------------------------------------------------------------------------
// NM election helpers
// ---------------------------------------------------------------------------

uint8_t NetworkService::ComputeElectionPriority() const {
    // Lower value = higher priority.
    // NETWORK_MANAGER role: base 0–63 (always beats AUTO)
    // AUTO role:            base 64–191
    // NODE_ONLY:            never wins (returns 0xFF)
    if (node_role_ == NodeRole::NODE_ONLY)
        return 0xFF;

    uint8_t role_base = (node_role_ == NodeRole::NETWORK_MANAGER) ? 0 : 64;
    // Lower address → lower priority value → wins ties
    uint8_t addr_component =
        static_cast<uint8_t>((node_address_ & 0xFF) >> 1);  // 0–127
    return static_cast<uint8_t>(
        std::min(static_cast<uint16_t>(role_base + addr_component),
                 static_cast<uint16_t>(0xFE)));
}

void NetworkService::StartElectionBackoff() {
    if (node_role_ == NodeRole::NODE_ONLY) {
        election_end_time_ = 0;  // NODE_ONLY never elects
        return;
    }

    election_priority_ = ComputeElectionPriority();

    // Backoff formula (all in ms):
    //   listen_window + role_bonus + addr_bonus + jitter
    uint32_t listen_window_ms = kElectionListenWindowMs;
    uint32_t role_bonus_ms =
        (node_role_ == NodeRole::NETWORK_MANAGER) ? 0 : listen_window_ms;
    // addr_bonus: up to 1 extra listen window spread over address space
    uint32_t addr_bonus_ms = (listen_window_ms * (node_address_ & 0xFF)) / 256;
    uint32_t jitter_ms = static_cast<uint32_t>(GetRTOS().GetRandom()) %
                         (listen_window_ms / 2 + 1);

    uint32_t backoff_ms =
        listen_window_ms + role_bonus_ms + addr_bonus_ms + jitter_ms;

    election_end_time_ = GetRTOS().getTickCount() + backoff_ms;

    LOG_INFO(
        "Election backoff started: priority=%d, delay=%ums "
        "(role_bonus=%u addr_bonus=%u jitter=%u)",
        election_priority_, backoff_ms, role_bonus_ms, addr_bonus_ms,
        jitter_ms);
}

Result NetworkService::SendNMClaim() {
    uint8_t node_count =
        static_cast<uint8_t>(routing_table_->GetSize() + 1);  // +1 for self
    auto claim_opt = NMClaimMessage::Create(node_address_, election_priority_,
                                            100,  // battery (simplified)
                                            node_count, network_id_);
    if (!claim_opt) {
        LOG_ERROR("Failed to create NM_CLAIM message");
        return Result::Error(LoraMesherErrorCode::kConfigurationError);
    }

    auto base_msg = std::make_unique<BaseMessage>(claim_opt->ToBaseMessage());
    message_queue_service_->AddMessageToQueue(
        types::protocols::lora_mesh::SlotAllocation::SlotType::DISCOVERY_TX,
        std::move(base_msg));

    LOG_INFO("Queued NM_CLAIM (priority=%d, network_id=0x%04X)",
             election_priority_, network_id_);
    return Result::Success();
}

void NetworkService::HandleForeignBeacon(const SyncBeaconMessage& beacon) {
    LOG_INFO(
        "Foreign network 0x%04X detected (ours: 0x%04X) — broadcasting "
        "NM_CLAIM so the secondary NM can compare priorities",
        beacon.GetNetworkId(), network_id_);
    SendNMClaim();
}

Result NetworkService::ProcessNMClaim(const BaseMessage& message) {
    auto claim_opt = NMClaimMessage::CreateFromSerialized(*message.Serialize());
    if (!claim_opt) {
        LOG_ERROR("Failed to deserialize NM_CLAIM message");
        return Result::Error(LoraMesherErrorCode::kSerializationError);
    }
    const auto& claim = *claim_opt;

    AddressType claimant = claim.GetSource();
    uint8_t their_priority = claim.GetPriority();

    LOG_INFO("Received NM_CLAIM from 0x%04X, priority=%d (ours=%d)", claimant,
             their_priority, election_priority_);

    // NETWORK_MANAGER state: cross-network merge path.
    // If the incoming claim has a better (lower) priority we yield; otherwise
    // we are the winner and the remote node will surrender when they hear our
    // own NM_CLAIM (queued by HandleForeignBeacon on the next beacon exchange).
    if (state_ == ProtocolState::NETWORK_MANAGER) {
        if (their_priority < election_priority_) {
            LOG_INFO(
                "Foreign NM 0x%04X priority 0x%02X beats ours 0x%02X — "
                "yielding network 0x%04X",
                claimant, their_priority, election_priority_, network_id_);
            // Adopt winner's network id so our nodes eventually re-join there
            if (claim.GetNetworkId() != 0) {
                network_id_ = claim.GetNetworkId();
            }
            surrendered_in_election_ = true;
            // Enter DISCOVERY directly — cannot call StartDiscovery() because
            // NETWORK_MANAGER-role nodes skip discovery and re-create a network.
            // By setting state to DISCOVERY we stop broadcasting sync beacons
            // (our nodes lose sync → FAULT_RECOVERY → rejoin the winner) and
            // we listen for the winner's beacon to join their network.
            network_found_ = false;
            network_creator_ = false;
            selected_sponsor_ = 0;
            discovery_start_time_ = GetRTOS().getTickCount();
            SetDiscoverySlots();
            SetState(ProtocolState::DISCOVERY);
        } else {
            // Our priority wins — send counter-claim so the remote NM can
            // compare priorities and yield without waiting for beacon alignment
            SendNMClaim();
        }
        return Result::Success();
    }

    // Only relevant in FAULT_RECOVERY or NM_ELECTION states
    if (state_ != ProtocolState::FAULT_RECOVERY &&
        state_ != ProtocolState::NM_ELECTION) {
        return Result::Success();
    }

    // If their priority is lower (= higher priority), surrender
    if (their_priority < election_priority_) {
        LOG_INFO(
            "Surrendering to higher-priority claimant 0x%04X (their=%d "
            "ours=%d)",
            claimant, their_priority, election_priority_);
        election_end_time_ = 0;  // cancel our election
        surrendered_in_election_ = true;

        // Store network_id from the claimant's beacon if available
        if (claim.GetNetworkId() != 0) {
            network_id_ = claim.GetNetworkId();
        }

        // Enter DISCOVERY directly — cannot call StartDiscovery() because
        // NETWORK_MANAGER-role nodes skip discovery and re-create a network.
        network_found_ = false;
        network_creator_ = false;
        selected_sponsor_ = 0;
        discovery_start_time_ = GetRTOS().getTickCount();
        SetDiscoverySlots();
        SetState(ProtocolState::DISCOVERY);
    }
    // If our priority is lower or equal, we win — ignore their claim

    return Result::Success();
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher