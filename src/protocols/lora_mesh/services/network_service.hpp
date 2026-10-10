/**
 * @file network_service.hpp
 * @brief Unified network service combining node management, routing, and discovery
 */

#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "protocols/lora_mesh/interfaces/i_message_queue_service.hpp"
#include "protocols/lora_mesh/interfaces/i_network_service.hpp"
#include "protocols/lora_mesh/interfaces/i_routing_table.hpp"
#include "protocols/lora_mesh/interfaces/i_superframe_service.hpp"
#include "protocols/lora_mesh/services/message_cache.hpp"
#include "protocols/lora_mesh/services/reliable_messaging.hpp"
#include "protocols/lora_mesh/services/slot_scheduler.hpp"
#include "protocols/lora_mesh/services/sync_beacon_service.hpp"
#include "protocols/reliability/reliable_delivery.hpp"
#include "types/application/application_types.hpp"
#include "types/hardware/i_hardware_manager.hpp"
#include "types/messages/loramesher/ack_payload.hpp"
#include "types/messages/loramesher/broadcast_message.hpp"
#include "types/messages/loramesher/data_message.hpp"
#include "types/messages/loramesher/group_message.hpp"
#include "types/messages/loramesher/join_request_message.hpp"
#include "types/messages/loramesher/join_response_header.hpp"
#include "types/messages/loramesher/join_response_message.hpp"
#include "types/messages/loramesher/nm_claim_message.hpp"
#include "types/messages/loramesher/routing_table_message.hpp"
#include "types/messages/loramesher/slot_allocation_message.hpp"
#include "types/messages/loramesher/slot_request_message.hpp"
#include "types/messages/loramesher/sync_beacon_message.hpp"
#include "types/protocols/lora_mesh/network_node_route.hpp"
#include "types/protocols/lora_mesh/slot_allocation.hpp"
#include "types/storage/network_snapshot.hpp"
#include "types/storage/resume_snapshot.hpp"
#include "utils/compat/span.hpp"
#include "utils/logger.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

static const uint8_t kMaxNoReceivedSyncBeacons =
    5;  ///< Max number of superframes without receiving sync beacons

/// Cross-network NM merge (Path A) is disabled: its detection depends on the
/// TDMA phase of the two networks and can converge to the wrong NM. See
/// docs/todo_network_merge.md.
static constexpr bool kNetworkMergeEnabled = false;

/// Discovery windows a surrendered node keeps listening for the election
/// winner before assuming it is gone and re-forming its own network. Each
/// window is one discovery timeout (~a few superframes); this must cover the
/// worst-case TDMA phase-alignment + join handshake between two networks.
static const uint8_t kMaxSurrenderDiscoveryRetries = 5;

/// Minimum listen window before an election fires (ms).
static constexpr uint32_t kElectionListenWindowMs = 5000;

/// Superframes a node one hop from the manager listens before an election
/// fires, so a manager that resets and comes back is heard first.
static constexpr uint32_t kElectionListenSuperframes = 2;

/// Extra listen superframes per additional hop: every relay on the path has
/// to rejoin the manager before it forwards the manager's beacons again.
static constexpr uint32_t kElectionListenSuperframesPerHop = 4;

static constexpr uint32_t kCleanupIntervalMs =
    60000;  ///< Route cleanup every 60s

/// Routing-broadcast rotations a route may go unheard before it expires:
/// one missed broadcast, plus a rotation of slack for the cleanup's timing
static constexpr uint32_t kRouteAgingRotations = 3;

/**
 * @brief Unified implementation of network service
 * 
 * Combines node management, routing, and discovery in a single service
 * using the NetworkNodeRoute data structure for efficient memory usage
 * and reduced code duplication.
 */
class NetworkService : public INetworkService {
   public:
    /**
     * @brief Constructor
     * 
     * @param node_address Local node address
     * @param message_queue_service Message queue service for outgoing messages
     * @param superframe_service Optional superframe service for TDMA integration
     * @param hardware_manager Hardware manager for ToA calculations
     * @param routing_table Optional routing table implementation (default: distance-vector)
     */
    NetworkService(
        AddressType node_address,
        std::shared_ptr<IMessageQueueService> message_queue_service,
        std::shared_ptr<ISuperframeService> superframe_service = nullptr,
        std::shared_ptr<hardware::IHardwareManager> hardware_manager = nullptr,
        std::unique_ptr<IRoutingTable> routing_table = nullptr);

    /**
     * @brief Virtual destructor
     */
    virtual ~NetworkService() = default;

    // INetworkService node management implementation

    /**
     * @brief Update or add a node in the network
     * 
     * Updates node information if it exists, or adds a new node if it doesn't.
     * Handles network manager updates and triggers superframe changes if needed.
     * 
     * @param node_address Node address to update
     * @param is_network_manager Whether this node is the network manager
     * @param allocated_data_slots Allocated data slots for this node
     * @param capabilities Node capabilities bitmap, if 0, get the previous value
     * @return bool True if node was added or significantly updated
     */
    bool UpdateNetworkNode(AddressType node_address, bool is_network_manager,
                           uint8_t allocated_data_slots,
                           uint8_t capabilities = 0) override;

    /**
    * @brief Update the network with control and discovery slots
    * 
    * Updates the network configuration with the specified control and discovery slots.
    * If a slot count is 0, the previous value will be retained.
    * 
    * @param allocated_control_slots Number of control slots to allocate
    * @param allocated_discovery_slots Number of discovery slots to allocate
    * @return bool True if the network was updated successfully
    */
    bool UpdateNetwork(uint8_t allocated_control_slots = 0,
                       uint8_t allocated_discovery_slots = 0) override;

    /**
     * @brief Check if a node exists in the network
     * 
     * @param node_address Node address to check
     * @return bool True if node exists in network
     */
    bool IsNodeInNetwork(AddressType node_address) const override;

    /**
     * @brief Get a snapshot of all network nodes with their routing information
     *
     * @return std::vector<NetworkNodeRoute> Copy of all nodes and their routes
     */
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute> GetNetworkNodes()
        const override;

    /**
     * @brief Get the number of nodes in the network
     * 
     * @return size_t Total number of nodes
     */
    size_t GetNetworkSize() const override;

    /**
     * @brief Get direct access to the routing table interface
     * 
     * Provides access to the underlying routing table for direct manipulation.
     * Useful for testing and upper layer protocols that need precise routing control.
     * 
     * @return IRoutingTable* Pointer to the routing table interface
     */
    IRoutingTable* GetRoutingTable() { return routing_table_.get(); }

    /**
     * @brief Remove nodes that haven't been seen recently
     * 
     * Marks routes as inactive if they've timed out and removes nodes
     * that have been inactive for too long.
     * 
     * @return size_t Number of nodes removed
     */
    size_t RemoveInactiveNodes() override;

    // INetworkService routing implementation

    /**
     * @brief Process a routing table update message
     * 
     * Updates routing information based on received routing table.
     * Handles network manager updates and time synchronization.
     * 
     * @param message Routing table message to process
     * @return Result Success or error details
     */
    Result ProcessRoutingTableMessage(const BaseMessage& message,
                                      uint32_t reception_timestamp,
                                      float rssi = 0.0f,
                                      float snr = 0.0f) override;

    /**
     * @brief Send a routing table update to the network
     * 
     * Creates and queues a routing table message containing all active routes.
     * 
     * @return Result Success or error details
     */
    Result SendRoutingTableUpdate() override;

    /**
     * @brief Find the best next hop to reach a destination
     * 
     * Searches routing table for the best route based on hop count
     * and link quality.
     * 
     * @param destination Destination address
     * @return AddressType Next hop address or 0 if no route found
     */
    AddressType FindNextHop(AddressType destination) const override;

    /**
     * @brief Update routing entry for a destination
     *
     * Updates or creates a routing entry based on information received
     * from a neighbor.
     *
     * @param source Source of the routing update
     * @param destination Destination address
     * @param hop_count Hop count from source to destination
     * @param link_quality Link quality from source to destination
     * @param allocated_slots Slots allocated to destination
     * @param capabilities Node capabilities bitmap
     * @return bool True if routing table was significantly updated
     */
    bool UpdateRouteEntry(AddressType source, AddressType destination,
                          uint8_t hop_count, uint8_t link_quality,
                          uint8_t allocated_slots,
                          uint8_t capabilities) override;

    /**
     * @brief Set callback for route update notifications
     *
     * @param callback Function to call when routes are updated or removed
     */
    void SetRouteUpdateCallback(RouteUpdateCallback callback) override;

    /**
     * @brief Set callback for received data messages
     *
     * @param callback Function to call when data is received
     */
    void SetDataReceivedCallback(DataReceivedCallback callback) override;

    /**
     * @brief Set local node capabilities
     *
     * Updates the capabilities for this node. Changes will be propagated
     * in the next routing table broadcast.
     *
     * @param capabilities Capabilities bitmap (NodeCapabilities flags)
     */
    void SetLocalNodeCapabilities(uint8_t capabilities);

    /**
     * @brief Get local node capabilities
     *
     * @return uint8_t Local node capabilities bitmap
     */
    uint8_t GetLocalNodeCapabilities() const;

    /**
     * @brief Set local node allocated data slots
     *
     * Updates the allocated data slots for this node. Changes will be propagated
     * in the next routing table broadcast.
     *
     * @param data_slots Number of allocated data slots
     */
    void SetLocalAllocatedDataSlots(uint8_t data_slots);

    /**
     * @brief Get local node allocated data slots
     *
     * @return uint8_t Local node's allocated data slots
     */
    uint8_t GetLocalAllocatedDataSlots() const {
        return local_allocated_data_slots_;
    }

    /**
     * @brief Get capabilities for a specific node
     *
     * @param node_address Address of the node to query
     * @return uint8_t Node capabilities bitmap (0 if node not found)
     */
    uint8_t GetNodeCapabilities(AddressType node_address) const;

    // INetworkService discovery implementation

    /**
     * @brief Start network discovery process
     * 
     * Initiates discovery to find existing networks. If none found within
     * timeout, creates a new network.
     * 
     * @param discovery_timeout_ms Timeout for discovery in milliseconds
     * @return Result Success or error details
     */
    Result StartDiscovery(uint32_t discovery_timeout_ms) override;

    /**
     * @brief Capture the state this node keeps across a reset
     *
     * A network manager also records the control slot of every member it
     * knows (and of members it still holds slots for after its own restart).
     * Safe to call from the application thread while the protocol runs.
     *
     * @return Snapshot of the current state
     */
    storage::NetworkSnapshot CaptureSnapshot() const;

    /**
     * @brief Restore state captured by CaptureSnapshot() before a reset
     *
     * Call after Configure() and before StartDiscovery(). The sequence counter
     * continues past every number the node may have used. A network manager
     * snapshot (ignored for NODE_ONLY nodes) makes StartDiscovery() listen
     * briefly for its network and then resume it with the same network id,
     * holding the members' control slots until they are heard again. A member
     * snapshot makes the first discovery wait for the network to come back
     * before forming a new one.
     *
     * @param snapshot Snapshot taken by this node
     * @return Success, or kInvalidParameter if it was taken by another node
     */
    Result ApplySnapshot(const storage::NetworkSnapshot& snapshot);

    /**
     * @brief Capture the state a member needs to resume after a deep sleep
     *
     * Fills every part of the snapshot but the timing, which belongs to the
     * caller. Call from the protocol task.
     *
     * @return The snapshot, or std::nullopt when the node is not a member in
     *         normal operation
     */
    std::optional<storage::ResumeSnapshot> CaptureResumeSnapshot() const;

    /**
     * @brief Resume the membership saved by CaptureResumeSnapshot()
     *
     * Restores the routes, the membership, the message sequences and the
     * slot table, and enters normal operation. Call after Configure() instead
     * of StartDiscovery(); the caller then resumes the superframe schedule.
     *
     * @param snapshot Snapshot taken by this node before it slept
     * @return Success, or kInvalidParameter (nothing restored) if the
     *         snapshot is invalid, was taken by another node or the node is
     *         configured as network manager
     */
    Result ApplyResumeSnapshot(const storage::ResumeSnapshot& snapshot);

    /**
     * @brief Reason the node cannot deep-sleep and resume now
     *
     * A node may only resume a state that needs nothing from the network
     * while it sleeps: a member in normal operation, in sync, with no
     * reliable message in flight.
     *
     * @return The reason, or nullptr if the node may deep-sleep
     */
    const char* GetDeepSleepBlocker() const;

    /**
     * @brief True if a sync beacon arrived in the current superframe
     *
     * A beacon up to half a superframe before the node's own superframe
     * start counts, so a clock slightly ahead of the network's still counts
     * the beacon it just synchronized to.
     */
    bool HeardSyncBeaconThisSuperframe() const;

    /**
     * @brief True if a sync beacon arrived within the last @p window_ms
     */
    bool ReceivedSyncBeaconWithin(uint32_t window_ms) const;

    /**
     * @brief Reason the MCU must stay awake now, if any
     *
     * Sleep makes the clock drift, so the node stays awake while it, or a
     * join it relays, depends on precise timing: until it has joined and
     * heard beacons in kSettleSuperframes consecutive superframes, while it
     * relays a join request or response (until the joiner is reachable or
     * the superframe after the relay ends), and, as manager, while it answers
     * a join. The radio still sleeps in SLEEP slots.
     *
     * @return The reason, or nullptr if the MCU may sleep
     */
    const char* GetSleepHold() const;

    /**
     * @brief True from a resume until a sync beacon confirms the schedule
     *
     * The resumed schedule is only as accurate as the sleep clock, so the
     * node listens instead of transmitting until then.
     */
    bool IsAwaitingResync() const {
        return awaiting_resync_ && state_ == ProtocolState::NORMAL_OPERATION;
    }

    /**
     * @brief Learned error of the sleep clock in @p kind sleep
     */
    power::SleepClockCalibration GetSleepClockCalibration(
        power::SleepKind kind) const {
        return kind == power::SleepKind::LIGHT ? light_sleep_clock_
                                               : deep_sleep_clock_;
    }

    /**
     * @brief Count a sleep that just ended towards the next calibration
     *        sample
     *
     * At the next sync beacon, the drift the schedule gained since the
     * previous beacon is the error the sleep clock made over all sleeps in
     * between (the node runs on its crystal while awake). It calibrates the
     * kind of sleep that took longer; the other kind counts as corrected by
     * its own estimate. No sample is taken
     * if a beacon was missed in between, or if the node was awake for more
     * than half a superframe before the beacon.
     *
     * @param kind Kind of the sleep
     * @param slept_ms Corrected length of the sleep
     */
    void RecordSleepForCalibration(power::SleepKind kind, uint32_t slept_ms);

    /**
     * @brief Identifier of the network this node belongs to (0 = none)
     */
    uint16_t GetNetworkId() const { return network_id_.load(); }

    /**
     * @brief This node's control slot index (0xFF = unassigned)
     */
    uint8_t GetMyControlSlotIndex() const { return my_control_slot_index_; }

    /**
     * @brief Consecutive superframes without a sync beacon (members only)
     */
    uint8_t GetMissedSyncBeaconCount() const {
        return no_received_sync_beacon_count_;
    }

    /**
     * @brief Number of control slots allocated in the slot table.
     */
    uint8_t GetAllocatedControlSlots() const {
        return slot_scheduler_->GetAllocatedControlSlots();
    }

    /**
     * @brief Milliseconds until a warm-restarted manager stops listening for
     *        its network and resumes it
     *
     * @return 0 when no such wait is pending, otherwise at least 1
     */
    uint32_t GetManagerResumeDelayRemaining() const;

    /**
     * @brief Start joining an existing network
     * * Initiates the joining process by sending a join request to the
     * network manager. If no manager is found, it will create a new network.
     * * @param manager_address Address of the network manager to join
     * @param join_timeout_ms Timeout for joining in milliseconds
     * @return Result Success or error details
     */
    Result StartJoining(AddressType manager_address,
                        uint32_t join_timeout_ms) override;

    /**
     * @brief Get the join timeout
     * 
     * @return uint32_t Join timeout in milliseconds
     */
    uint32_t GetJoinTimeout();

    /**
     * @brief Check if a network was found during discovery
     * 
     * @return bool True if network was found
     */
    bool IsNetworkFound() const override;

    /**
     * @brief Check if this node created the network
     * 
     * @return bool True if this node is the network creator
     */
    bool IsNetworkCreator() const override;

    /**
     * @brief Process any received message
     * 
     * Routes messages to appropriate handlers based on message type.
     * 
     * @param message The received message
     * @param reception_timestamp Timestamp when the message was received (from RadioEvent)
     * @return Result Success or error details
     */
    Result ProcessReceivedMessage(const BaseMessage& message,
                                  uint32_t reception_timestamp,
                                  float rssi = 0.0f, float snr = 0.0f) override;

    // INetworkService superframe integration

    /**
     * @brief Notify superframe service of network changes
     * 
     * Triggers superframe updates when network topology changes.
     * 
     * @return Result Success or error details
     */
    Result NotifySuperframeOfNetworkChanges() override;

    // INetworkService state and configuration

    /**
     * @brief Get current protocol state
     * 
     * @return ProtocolState Current state of the protocol
     */
    ProtocolState GetState() const override;

    /**
     * @brief Set protocol state
     * 
     * Updates protocol state and logs the change.
     * 
     * @param state New protocol state
     */
    void SetState(ProtocolState state) override;

    /**
     * @brief Get network manager address
     * 
     * @return AddressType Address of network manager or 0 if none
     */
    AddressType GetNetworkManagerAddress() const override;

    /**
     * @brief Set network manager address
     * 
     * Updates network manager and adjusts node status accordingly.
     * 
     * @param manager_address New network manager address
     */
    void SetNetworkManager(AddressType manager_address) override;

    /**
     * @brief Configure the network service
     * 
     * @param config Network configuration parameters
     * @return Result Success or error details
     */
    Result Configure(const NetworkConfig& config) override;

    /**
     * @brief Get current configuration
     * 
     * @return const NetworkConfig& Current configuration
     */
    const NetworkConfig& GetConfig() const override;

    /**
     * @brief Reset network state and clear allocated resources
     * 
     * Clears network nodes, slot table, and resets state to initial values.
     * Should be called when stopping the protocol to prevent memory leaks.
     */
    void ResetNetworkState() override;

    /**
     * @brief Calculate link quality to a specific node
     * 
     * @param node_address Node address
     * @return uint8_t Link quality (0-255)
     */
    uint8_t CalculateLinkQuality(AddressType node_address) const;

    /**
     * @brief Create a routing table message for broadcast
     *
     * @param destination Destination address (default broadcast)
     * @return std::unique_ptr<BaseMessage> Message ready for transmission
     */
    std::unique_ptr<BaseMessage> CreateRoutingTableMessage(
        AddressType destination = 0xFFFF);

    /**
     * @brief Compute the maximum number of routing entries that fit in
     *        a single broadcast frame given the current PHY cap.
     *
     * Used both by CreateRoutingTableMessage() to size the rotation slice
     * and by RemoveInactiveNodes() to scale aging timeouts to the
     * rotation period. Clamped to RoutingTableMessage::kMaxRoutingEntries.
     *
     * @return size_t Slice capacity (0 if header overhead exceeds the cap)
     */
    size_t ComputeBroadcastSliceCapacity() const;

    /**
     * @brief Number of sliced broadcasts a peer needs to cycle its whole
     *        active table once: ceil(table_size / slice_capacity).
     *
     * Used to scale both route aging (RemoveInactiveNodes) and the
     * unidirectional-detection threshold to the rotation period, so a node
     * whose entry only appears once per rotation is not mistaken for a missed
     * or unidirectional link.
     *
     * @return size_t Rotation steps (at least 1)
     */
    size_t ComputeRotationSteps() const;

    /**
     * @brief Join an existing network
     * 
     * @param manager_address Network manager address
     * @return Result Success or error details
     */
    Result JoinNetwork(AddressType manager_address);

    /**
     * @brief Create a new network with this node as manager
     * 
     * @return Result Success or error details
     */
    Result CreateNetwork();

    /**
     * @brief Check if synchronized with network timing
     * 
     * @return bool True if synchronized
     */
    bool IsSynchronized() const { return is_synchronized_; }

    /**
     * @brief Schedule routing message expectations for link quality
     * 
     * Should be called periodically to track link quality statistics
     */
    void ScheduleRoutingMessageExpectations();

    /**
     * @brief Reset link quality statistics for new measurement period
     */
    void ResetLinkQualityStats();

    // Join management methods

    /**
     * @brief Send a join request to network manager
     * 
     * @param manager_address Network manager address
     * @param requested_slots Number of slots requested
     * @return Result Success or error
     */
    Result SendJoinRequest(AddressType manager_address,
                           uint8_t requested_slots);

    /**
     * @brief Process a join request from another node
     * 
     * Only processed by network manager. Determines if node should be
     * accepted and sends appropriate response.
     * 
     * @param message Join request message
     * @return Result Success or error
     */
    Result ProcessJoinRequest(const BaseMessage& message,
                              uint32_t reception_timestamp);

    /**
     * @brief Process a join response from network manager
     * 
     * Only processed when in joining state. Updates network configuration
     * if accepted.
     * 
     * @param message Join response message
     * @return Result Success or error
     */
    Result ProcessJoinResponse(const BaseMessage& message,
                               uint32_t reception_timestamp);

    /**
     * @brief Send a join response to a node
     * 
     * Only network manager can send join responses.
     * 
     * @param dest Destination node address
     * @param status Response status (accepted/rejected)
     * @param allocated_slots Number of allocated slots
     * @return Result Success or error
     */
    Result SendJoinResponse(
        AddressType dest, loramesher::JoinResponseHeader::ResponseStatus status,
        uint8_t allocated_slots, AddressType sponsor_address = 0,
        uint8_t control_slot_index = 0xFF);

    // Data message methods

    /**
     * @brief Process a received data message
     *
     * Handles next-hop routing: if this node is the next_hop and the final
     * destination, delivers to application layer. If this node is the next_hop
     * but not the final destination, forwards the message.
     *
     * @param message Data message to process
     * @param reception_timestamp When the message was received
     * @param reliable Whether this is a reliable (acknowledged) data message
     * @return Result Success or error
     */
    Result ProcessDataMessage(const BaseMessage& message,
                              uint32_t reception_timestamp,
                              bool reliable = false);

    /**
     * @brief Process a received acknowledgement message
     *
     * Matches the acknowledgement against the reliable-delivery component when
     * this node is the final destination, or forwards it toward the original
     * sender otherwise.
     *
     * @param message Acknowledgement message to process
     * @return Result Success or error
     */
    Result ProcessAckMessage(const BaseMessage& message);

    /**
     * @brief Forward a data message to the next hop
     *
     * Looks up the next hop for the final destination and queues
     * a new data message with updated next_hop field.
     *
     * @param original_msg The original data message to forward
     * @return Result Success or error
     */
    Result ForwardDataMessage(const DataMessage& original_msg);

    /**
     * @brief Send user data to a destination
     *
     * Creates a DataMessage with proper next-hop routing and queues
     * it for transmission.
     *
     * @param destination Final destination address
     * @param data User data payload
     * @return Result Success or error (e.g., no route found)
     */
    Result SendData(AddressType destination, const std::vector<uint8_t>& data);

    // Reliable delivery methods

    /**
     * @brief Send user data with acknowledged (reliable) delivery
     *
     * Transmits the data as a reliable DATA message and tracks it in the
     * reliable-delivery component, retransmitting up to max_retries times until
     * an acknowledgement arrives. The registered delivery callback fires with
     * the terminal outcome (Delivered or Failed).
     *
     * @param destination Final destination address
     * @param data User data payload
     * @param max_retries Maximum retransmissions after the first attempt
     * @param timeout_override_ms If non-zero, overrides the computed timeout
     * @return reliability::MessageId Assigned id, or {0,0} if the send failed
     */
    reliability::MessageId SendReliable(AddressType destination,
                                        const std::vector<uint8_t>& data,
                                        uint8_t max_retries,
                                        uint32_t timeout_override_ms = 0);

    /**
     * @brief Register the callback fired on reliable-delivery outcomes
     *
     * @param callback Callback invoked with each delivery outcome
     */
    void SetDeliveryCallback(reliability::DeliveryCallback callback);

    /**
     * @brief Inbound data callback carrying the message metadata
     */
    using DataReceivedExCallback = loramesher::DataReceivedExCallback;

    /**
     * @brief Register an inbound callback that also reports the destination,
     *        sequence number and hop count
     *
     * SetDataReceivedCallback keeps working; when both are set, both fire.
     *
     * @param callback Callback invoked on application delivery
     */
    void SetDataReceivedExCallback(DataReceivedExCallback callback);

    /**
     * @brief Advance reliable-delivery retransmission timers
     *
     * Called from the protocol task's periodic tick; retransmits or fails out
     * expired entries.
     */
    void ProcessReliableTimers();

    /**
     * @brief Number of reliable messages currently awaiting acknowledgement
     *
     * @return size_t Pending reliable-delivery entries
     */
    size_t GetReliablePendingCount() const {
        return reliable_messaging_->GetReliablePendingCount();
    }

    // Broadcast message methods

    /**
     * @brief Process a received broadcast message
     *
     * Handles de-duplication, application delivery, and TTL-based re-broadcast.
     *
     * @param message Broadcast message to process
     * @param reception_timestamp When the message was received
     * @return Result Success or error
     */
    Result ProcessBroadcastMessage(const BaseMessage& message,
                                   uint32_t reception_timestamp);

    /**
     * @brief Send a broadcast message to all nodes in the mesh
     *
     * Creates a broadcast message with default TTL and queues it for
     * transmission. The message propagates via controlled flooding.
     *
     * @param data User data payload
     * @return Result Success or error
     */
    Result SendBroadcast(std::span<const uint8_t> data);

    // Group (multicast) methods

    /**
     * @brief Join a logical group (local membership only)
     *
     * @param group Group address (must satisfy IsGroupAddress)
     * @return Result Success, or kInvalidArgument / kBufferFull
     */
    Result JoinGroup(AddressType group);

    /**
     * @brief Leave a logical group
     *
     * @param group Group address
     * @return Result Success or kInvalidArgument
     */
    Result LeaveGroup(AddressType group);

    /**
     * @brief Whether this node is a member of the given group
     *
     * @param group Group address
     * @return bool True if a member
     */
    bool IsMemberOfGroup(AddressType group) const;

    /**
     * @brief Get the set of groups this node belongs to
     *
     * @return std::vector<AddressType> Member groups
     */
    std::vector<AddressType> GetGroups() const;

    /**
     * @brief Send data to a group via membership-gated flooding
     *
     * @param group Destination group address
     * @param data User data payload
     * @return Result Success or error
     */
    Result SendGroup(AddressType group, std::span<const uint8_t> data);

    /**
     * @brief Send data to a group and collect per-recipient acknowledgements
     *
     * Floods the group like SendGroup, but requests an acknowledgement from each
     * member. The delivery callback fires Delivered once per distinct responder,
     * then GroupWindowClosed with the responder count when window_ms elapses.
     *
     * @param group Destination group address
     * @param data User data payload
     * @param max_retries Reserved for whole-group rebroadcast (currently unused)
     * @param window_ms Acknowledgement-collection window duration
     * @return reliability::MessageId Assigned id, or {0,0} if the send failed
     */
    reliability::MessageId SendGroupReliable(AddressType group,
                                             std::span<const uint8_t> data,
                                             uint8_t max_retries,
                                             uint32_t window_ms);

    // Multi-hop synchronization beacon processing

    /**
     * @brief Process a received sync beacon message
     * 
     * Handles timing synchronization and forwarding decisions for multi-hop
     * sync beacon propagation across the mesh network.
     * 
     * @param message Sync beacon message
     * @return Result Success or error
     */
    Result ProcessSyncBeacon(const BaseMessage& message,
                             uint32_t reception_timestamp);

    /**
     * @brief Send an original sync beacon (Network Manager only)
     * 
     * Creates and sends the original synchronization beacon with current
     * superframe timing information. Only the Network Manager should call this.
     * 
     * @return Result Success or error
     */
    Result SendSyncBeacon();

    /**
     * @brief Forward a received sync beacon to the next hop
     * 
     * Creates and sends a forwarded version of the received sync beacon,
     * incrementing hop count and adding propagation delay.
     * 
     * @param original_beacon The sync beacon to forward
     * @param processing_delay Additional delay for processing and transmission
     * @return Result Success or error
     */
    Result ForwardSyncBeacon(const SyncBeaconMessage& original_beacon,
                             uint32_t processing_delay);

    /**
     * @brief Check if this node should forward a sync beacon
     * 
     * Determines if the node should forward based on hop count, network
     * topology, and forwarding rules to prevent loops and collisions.
     * 
     * @param beacon The sync beacon to evaluate
     * @return bool True if the node should forward this beacon
     */
    bool ShouldForwardSyncBeacon(const SyncBeaconMessage& beacon);

    /**
     * @brief Handle superframe start event for sync beacon transmission
     * 
     * Called at the beginning of each superframe. If this node is the Network Manager,
     * it will send a sync beacon. If it's a regular node, it will listen for sync beacons.
     * 
     * @return Result Success or error
     */
    Result HandleSuperframeStart();

    /**
     * @brief Handle the start of a discovery slot
     *
     * While joining, queues the scheduled JOIN_REQUEST when its discovery
     * slot begins, so it is transmitted in that slot.
     *
     * @param discovery_index Position of the slot within the discovery band
     */
    void HandleDiscoverySlotStart(uint8_t discovery_index);

    /**
     * @brief Consecutive join attempts that went unanswered
     *
     * Kept across rejoins of a network; reset once the node is accepted or
     * creates its own network.
     */
    uint8_t GetJoinRetryCount() const { return join_retry_count_; }

    /**
     * @brief Expand all sync beacon slots to RX after missed beacons
     *
     * When beacons are missed (e.g., because the node's hop distance changed
     * and the slot table no longer covers the right layer), this converts all
     * sync beacon SLEEP and TX slots to SYNC_BEACON_RX so the node can hear
     * beacons from any hop layer and recover synchronization.
     */
    void ExpandSyncBeaconListening();

    /**
     * @brief Restore the SYNC_BEACON_TX slot demoted by ExpandSyncBeaconListening()
     *
     * Called when a sync beacon has been received and queued for forwarding in
     * the same superframe. Flips the slot at our hop distance to NM from
     * SYNC_BEACON_RX back to SYNC_BEACON_TX so the queued forward can be sent.
     * No-op for the network manager (hop distance 0) and when the slot is
     * already TX.
     */
    void RestoreSyncBeaconTxSlot();

    /**
     * @brief Apply pending join request at superframe boundary
     * 
     * Called by the Network Manager at the start of each superframe to apply
     * any buffered join requests. Updates slot allocation and clears pending flag.
     * 
     * @return Result Success or error
     */
    Result ApplyPendingJoin();

    // Slot management methods

    /**
     * @brief Process a slot request from a node
     * 
     * Only processed by network manager. Allocates slots if available.
     * 
     * @param message Slot request message
     * @return Result Success or error
     */
    Result ProcessSlotRequest(const BaseMessage& message,
                              uint32_t reception_timestamp);

    /**
     * @brief Process a slot allocation message
     * 
     * Updates local slot table based on allocation from network manager.
     * 
     * @param message Slot allocation message
     * @return Result Success or error
     */
    Result ProcessSlotAllocation(const BaseMessage& message,
                                 uint32_t reception_timestamp);

    /**
     * @brief Send a slot request to network manager
     * 
     * @param num_slots Number of slots needed
     * @return Result Success or error
     */
    Result SendSlotRequest(uint8_t num_slots);

    /**
     * @brief Update slot table based on network role
     * 
     * Creates slot allocation table with appropriate TX/RX/Sleep slots.
     * 
     * @return Result Success or error
     */
    Result UpdateSlotTable();

    /**
     * @brief Set discovery slots in the slot table
     * 
     * Set discovery slots based on current network state.
     * @return Result Success or error
     */
    Result SetDiscoverySlots();

    /**
     * @brief Set joining slots for power-efficient join process
     * 
     * Configures minimal slot allocation for JOINING state:
     * - 1 CONTROL_TX slot for join requests
     * - 1 CONTROL_RX slot for join responses  
     * - 2 DISCOVERY_RX slots for network monitoring
     * - Remaining slots as SLEEP for power efficiency
     * 
     * @return Result Success or error
     */
    Result SetJoiningSlots();

    /**
     * @brief Broadcast slot allocation to all nodes
     * 
     * Only network manager can broadcast allocation.
     * 
     * @return Result Success or error
     */
    Result BroadcastSlotAllocation();

    /**
     * @brief Get current slot table
     *
     * @return Consistent copy of the active slot allocations
     */
    std::vector<types::protocols::lora_mesh::SlotAllocation> GetSlotTable()
        const {
        return slot_scheduler_->GetSlotTable();
    }

    /**
     * @brief Visit every active slot allocation without copying the table
     *
     * @param visitor Called once per slot in slot order; must not call back
     *                into the slot table
     */
    void ForEachSlot(const std::function<
                     void(const types::protocols::lora_mesh::SlotAllocation&)>&
                         visitor) const {
        slot_scheduler_->ForEachSlot(visitor);
    }

    // Discovery methods

    /**
     * @brief Perform discovery logic
     *
     * Checks for existing networks and creates new network if timeout expires.
     *
     * @param timeout_ms Discovery timeout in milliseconds
     * @return Result Success or error
     */
    Result PerformDiscovery(uint32_t timeout_ms);

    /**
     * @brief Perform joining logic
     *
     * Checks if network manager has responded to join request.
     *
     * @param manager_address Network manager address to join
     * @return Result Success or error
     */
    Result PerformJoining(uint32_t timeout_ms);

    /**
     * @brief Check election deadline and create network if window has closed.
     *
     * @return Result Success or error
     */
    Result PerformNMElection() override;

    /**
     * @brief Remaining ms until NM_ELECTION deadline (0 if expired or not in election).
     *
     * @return uint32_t Remaining milliseconds
     */
    uint32_t GetNMElectionTimeout() const override;

    /**
     * @brief Set the number of slots per superframe
     */
    void SetNumberOfSlotsPerSuperframe(uint8_t slots) override;

    /**
     * @brief Set the max number of hops of the actual network
     */
    void SetMaxHopCount(uint8_t max_hops) override;

    /**
     * @brief Set the authoritative node count from sync beacon
     *
     * Non-NM nodes use this to determine control slot allocation.
     *
     * @param node_count Number of nodes in the network
     */
    void SetBeaconNodeCount(uint8_t node_count) {
        beacon_node_count_ = node_count;
    }

    /**
     * @brief Set this node's control slot index (for testing)
     *
     * @param index Control slot index (0xFF = unassigned)
     */
    void SetMyControlSlotIndex(uint8_t index) {
        my_control_slot_index_ = index;
    }

    /**
     * @brief Get this node's hop distance to the network manager
     *
     * @return uint8_t Hop distance to NM (0 if NM, 1 if unknown)
     */
    uint8_t GetHopDistanceToNM() const override;

    /**
     * @brief Callback type for protocol state changes
     */
    using StateChangeCallback = std::function<void(ProtocolState)>;

    /**
     * @brief Register a callback invoked whenever the protocol state changes
     *
     * @param callback Function called with the new state
     */
    void SetStateChangeCallback(StateChangeCallback callback) {
        state_change_callback_ = std::move(callback);
    }

    /**
     * @brief Returns true if an NM election backoff is currently in progress
     */
    bool IsElectionPending() const { return election_deadline_ms_.has_value(); }

    /**
     * @brief Initiate NM election backoff (called when entering FAULT_RECOVERY)
     *
     * Computes a weighted staggered backoff based on node role and address.
     * NODE_ONLY nodes never start an election.
     */
    void StartElectionBackoff();

    /**
     * @brief Milliseconds until the pending election backoff expires
     *
     * @return 0 when it has expired or no election is pending
     */
    uint32_t GetElectionBackoffRemaining() const;

    /**
     * @brief Enter NM_ELECTION once the election backoff has expired
     *
     * Queues this node's NM_CLAIM. Called by the protocol task when the
     * backoff deadline passes, and at each superframe start.
     */
    void CheckElectionBackoff();

    /**
     * @brief Process a received NM_CLAIM message
     *
     * @param message The received NM_CLAIM message
     * @return Result
     */
    Result ProcessNMClaim(const BaseMessage& message);

    /**
     * @brief Broadcast an NM_CLAIM to all neighbors
     *
     * Queues the claim in the DISCOVERY_TX slot queue.
     * @return Result
     */
    Result SendNMClaim();

    NodeRole GetNodeRole() const override { return node_role_; }

    Result ApplyRoleChange(NodeRole new_role) override;

   private:
    /**
     * @brief Get comprehensive link quality for a node
     *
     * @param node_address Target node address
     * @return uint8_t Link quality (0-255)
     */
    uint8_t GetNodeLinkQuality(AddressType node_address) const;

    /**
     * @brief Perform timing synchronization with Network Manager using sync beacon
     * 
     * This function handles the timing synchronization logic that was previously
     * duplicated in ProcessSyncBeacon for both DISCOVERY and other states.
     * It calculates the Network Manager's timing and synchronizes the local
     * superframe to match.
     * 
     * @param sync_beacon The received sync beacon message
     * @param reception_timestamp When the sync beacon was received
     * @param context_name Context for logging (e.g., "Discovery", "Normal")
     * @param pre_start_action Optional callback invoked after SynchronizeWith()
     *        but before StartSuperframe(), while the superframe service is still
     *        stopped. Use this to queue work that must be ready before the
     *        update task resumes.
     * @return Result Success if synchronization succeeded, error otherwise
     */
    Result PerformTimingSynchronization(
        const SyncBeaconMessage& sync_beacon, uint32_t reception_timestamp,
        const std::string& context_name,
        std::function<void()> pre_start_action = nullptr);

    /**
     * @brief Update network topology after changes
     * 
     * @param notify_superframe Whether to notify superframe service
     * @return bool True if topology changed significantly
     */
    bool UpdateNetworkTopology(bool notify_superframe = true);

    /**
     * @brief Calculate Time-on-Air for a message
     * 
     * @param message_size Size of the message in bytes
     * @return uint32_t Time-on-Air in milliseconds
     */
    uint32_t CalculateTimeOnAir(uint8_t message_size) const;

    /**
     * @brief Calculate total NM TX time using Time-on-Air for each packet
     *
     * @param rt_node_count Total node count (including NM)
     * @param nm_data_slots Number of data slots allocated to the NM
     * @return uint32_t Total TX time in milliseconds
     */
    uint32_t CalculateNMTxTimeMs(uint8_t rt_node_count,
                                 uint8_t nm_data_slots) const;

    /**
     * @brief Calculate minimum slot duration from radio parameters
     *
     * Returns ToA(max_packet_size) + guard_time + processing margin,
     * rounded up to the nearest 50 ms. Falls back to DEFAULT_SLOT_DURATION_MS
     * when the hardware manager is unavailable.
     *
     * @return uint32_t Minimum slot duration in milliseconds
     */
    uint32_t CalculateMinSlotDuration() const;

    /**
     * @brief Calculate link stability metric
     * 
     * @param node Node to evaluate
     * @return uint8_t Stability metric (0-255)
     */
    uint8_t CalculateLinkStability(
        const types::protocols::lora_mesh::NetworkNodeRoute& node);

    /**
     * @brief Check if join request should be accepted
     *
     * @param node_address Requesting node address
     * @param requested_slots Requested slots
     * @param hops Hop count from requesting node to network manager
     * @return std::pair<bool, uint8_t> Accept decision and allocated slots
     */
    std::pair<bool, uint8_t> ShouldAcceptJoin(AddressType node_address,
                                              uint8_t requested_slots,
                                              uint8_t hops,
                                              size_t pending_node_count = 0,
                                              uint8_t pending_slot_count = 0);

    /**
     * @brief Pick the discovery slot of the next join attempt
     *
     * Discovery slots pair up as request/response. A direct join requests in
     * a random even slot so the network manager answers in the next one; a
     * sponsored join starts in the first slot, since relaying it to the
     * network manager and back takes the whole discovery band.
     */
    void ScheduleJoinAttempt();

    /// Random number of superframes to wait before retrying a join
    uint8_t DrawJoinBackoff() const;

    /// Whether the join request is relayed by a sponsor
    bool IsSponsoredJoin() const;

    /// Forget join attempts, once joined or no longer joining
    void ResetJoinRetryState();

    /**
     * @brief Forward a join request to the network manager
     * 
     * Implements dynamic discovery slot forwarding: temporarily switches 
     * the next discovery slot from RX to TX to forward the message.
     * 
     * @param join_request The join request message to forward
     * @return Result Success or error
     */
    Result ForwardJoinRequest(const JoinRequestMessage& join_request);

    /**
     * @brief Forward join response to sponsored node
     *
     * Forwards a join response from the network manager to the original joining node
     * when acting as a sponsor. Updates the destination and removes sponsor information
     * for final delivery.
     *
     * @param join_response The join response to forward
     * @return Result Success if forwarded successfully, error otherwise
     */
    Result ForwardJoinResponseToSponsoredNode(
        const JoinResponseMessage& join_response);

    /**
     * @brief Forward join response to next hop on path to sponsor
     *
     * Forwards a join response when this node is on the multi-hop path
     * between the network manager and the sponsor node. Updates the
     * next_hop field to continue routing toward the destination.
     *
     * @param join_response The join response to forward
     * @return Result Success if forwarded successfully, error otherwise
     */
    Result ForwardJoinResponse(const JoinResponseMessage& join_response);

    /**
     * @brief Schedule discovery slot for forwarding
     *
     * Finds the next available DISCOVERY_RX slot and temporarily converts
     * it to DISCOVERY_TX for message forwarding, then reverts it back.
     *
     * @return bool True if slot was scheduled successfully
     */
    bool ScheduleDiscoverySlotForwarding();

    /**
     * @brief Get number of allocated data slots
     * 
     * @return uint8_t Number of allocated slots
     */
    uint8_t GetAllocatedDataSlots() const;

    /**
     * @brief Convert slot table to superframe format
     *
     * @return Result Success or error details
     */
    Result SlotTableToSuperframe();

    // --- Slot-table accessor seam (delegates to SlotScheduler) -------------

    /**
     * @brief Mark the slot table dirty so the next rebuild regenerates it.
     */
    void MarkSlotTableDirty() { slot_scheduler_->MarkDirty(); }

    /**
     * @brief Rebuild the slot table when dirty, or unconditionally when forced.
     *
     * @param force Rebuild even if the dirty flag is clear.
     * @return Result Success or error
     */
    Result UpdateSlotTableIfDirty(bool force);

    /**
     * @brief Number of valid slots in the slot table.
     */
    uint16_t GetSlotCount() const { return slot_scheduler_->GetSlotCount(); }

    /**
     * @brief Build the callbacks through which ReliableMessaging reaches this
     *        service (queue, routing, delivery, configuration and clock).
     */
    ReliableMessaging::Host MakeReliableMessagingHost();

    /**
     * @brief Build the callbacks through which the slot scheduler reads
     *        routing state and updates the superframe.
     */
    SlotScheduler::Host MakeSlotSchedulerHost();

    /**
     * @brief Build the callbacks through which the sync-beacon service
     *        reaches this service.
     */
    SyncBeaconService::Host MakeSyncBeaconHost();

    /**
     * @brief Build a read-only context snapshot for the slot scheduler.
     */
    SlotScheduler::Context MakeSlotContext() const;

    /**
     * @brief Build a read-only context snapshot for the sync-beacon service.
     */
    SyncBeaconService::Context MakeSyncContext() const;

    /**
     * @brief Handle a foreign-network sync beacon (NM state only)
     *
     * Called when the NM receives a SYNC_BEACON from a different network.
     * Broadcasts an NM_CLAIM so the foreign NM can compare election priorities
     * and the lower-priority network surrenders gracefully.
     *
     * @param beacon The foreign sync beacon
     */
    void HandleForeignBeacon(const SyncBeaconMessage& beacon);

    /**
     * @brief Get max hops from routing table
     *
     * @return uint8_t Maximum hops from routing table
     */
    uint8_t GetMaxHopsFromRoutingTable() const;

    /**
     * @brief Compute election priority for this node
     *
     * Lower value = higher priority. NETWORK_MANAGER role gets lower base
     * value, ensuring it wins over AUTO nodes.
     *
     * @return uint8_t Priority value (0 = highest)
     */
    uint8_t ComputeElectionPriority() const;

    /**
     * @brief Find lowest available control slot index
     *
     * Scans routing table for used control slot indices and returns the
     * lowest unused index. Used by NM when assigning slots to new joiners.
     *
     * @return uint8_t Lowest available control slot index
     */
    uint8_t FindLowestAvailableControlSlot();

    /**
     * @brief Forward a broadcast message with decremented TTL
     */
    Result ForwardBroadcastMessage(const BroadcastMessage& original);

    /**
     * @brief Serialize a typed message into a BaseMessage and enqueue it for TX
     *
     * Centralizes the repeated make_unique<BaseMessage>(msg.ToBaseMessage())
     * plus AddMessageToQueue boilerplate shared by every send/forward path.
     *
     * @tparam MessageT Any message type exposing ToBaseMessage()
     * @param slot_type Transmit slot to enqueue into
     * @param message Typed message to serialize and send
     * @return Result of the enqueue operation
     */
    template <typename MessageT>
    Result EnqueueForTransmission(
        types::protocols::lora_mesh::SlotAllocation::SlotType slot_type,
        const MessageT& message) {
        auto base_msg = std::make_unique<BaseMessage>(message.ToBaseMessage());
        return message_queue_service_->AddMessageToQueue(slot_type,
                                                         std::move(base_msg));
    }

    // Member variables
    AddressType node_address_;  ///< Local node address
    std::shared_ptr<IMessageQueueService> message_queue_service_;
    std::shared_ptr<ISuperframeService> superframe_service_;
    std::shared_ptr<hardware::IHardwareManager> hardware_manager_;

    // Network state
    std::unique_ptr<IRoutingTable> routing_table_;

    NetworkConfig config_;  ///< Written under network_mutex_
    RouteUpdateCallback route_update_callback_;
    DataReceivedCallback data_received_callback_;
    /// Written by the protocol task, read by application-thread sends.
    std::atomic<ProtocolState> state_;
    /// Written by the protocol task, read by application-thread sends.
    std::atomic<AddressType> network_manager_{0};
    bool network_found_;
    bool network_creator_;
    bool is_synchronized_;
    uint32_t last_sync_time_;
    uint32_t last_sync_beacon_received_ = 0;
    uint8_t table_version_;
    uint32_t discovery_start_time_;
    uint32_t joining_start_time_;
    AddressType selected_sponsor_ =
        0;  ///< Sponsor node selected during discovery (first sync beacon sender)

    // Superframe parameters
    uint8_t current_network_depth_ =
        0;  ///< Current network depth from sync beacons/routing table
    uint8_t number_of_slots_per_superframe_ =
        0;  ///< Number of allocated slots x superframe, received from sync beacon
    uint8_t no_received_sync_beacon_count_ =
        0;  ///< Count of nº superframes without receiving sync beacons

    // Join request buffering for superframe coordination
    static constexpr size_t kMaxPendingJoins = 3;
    std::vector<JoinRequestMessage>
        pending_joins_;  ///< Buffered join requests (up to kMaxPendingJoins)

    bool pending_slot_table_rebuild_ =
        false;  ///< Flag indicating slot table rebuild is deferred to next superframe boundary

    // Control slot assignment
    uint8_t my_control_slot_index_ =
        0xFF;  ///< This node's assigned control slot index (0xFF=unassigned, NM=0, others assigned at join)
    uint8_t beacon_node_count_ =
        1;  ///< Authoritative node count from NM's sync beacon

    // Local node capabilities
    uint8_t local_capabilities_ = 0;  ///< Local node capabilities bitmap

    // Local node allocated data slots
    uint8_t local_allocated_data_slots_ =
        0;  ///< Local node's allocated data slots

    // Node role configuration
    NodeRole node_role_ = NodeRole::AUTO;  ///< Node role for network formation

    // Duty cycle regulation
    float target_duty_cycle_ = 0.01f;  ///< Target TX duty cycle
    float min_sleep_fraction_ =
        0.30f;  ///< Minimum fraction of superframe as sleep
    uint8_t churn_margin_slots_ =
        2;  ///< Absolute extra slots reserved by NM to absorb routing churn
    uint8_t ewma_alpha_fixed_ = 77;  ///< EWMA alpha in fixed-point (0.30 * 256)
    uint8_t consecutive_missed_for_inactivation_ =
        10;  ///< Consecutive misses before hard inactivation
    uint8_t min_consecutive_for_reactivation_ =
        2;  ///< Consecutive receptions to re-activate

    // Join retry backoff (Slotted ALOHA)
    /// Progress of the join attempt of the current superframe
    enum class JoinAttempt : uint8_t {
        kIdle,       ///< No attempt pending
        kScheduled,  ///< Waiting for its discovery slot
        kSent,       ///< JOIN_REQUEST queued, waiting for the response
    };
    static constexpr uint8_t kMaxJoinBackoffExponent =
        2;  ///< Backoff window caps at 2^2 superframes
    static constexpr uint32_t kJoinTimeoutSuperframes =
        13;  ///< First superframe plus three attempts at the full window
    uint8_t join_retry_count_ = 0;  ///< Consecutive unanswered join attempts
    uint8_t join_backoff_remaining_ =
        0;  ///< Superframes to skip before next retry
    JoinAttempt join_attempt_ = JoinAttempt::kIdle;
    uint8_t join_request_disc_index_ =
        0;  ///< Discovery slot of the scheduled attempt

    // Periodic cleanup
    uint32_t last_cleanup_time_ = 0;  ///< Last time route cleanup was performed

    // NM election state
    /// Tick count when the election backoff expires (empty when none)
    std::optional<uint32_t> election_deadline_ms_;
    uint8_t election_priority_ =
        0xFF;  ///< Our election priority (lower = higher priority)
    /// Tick count when NM_ELECTION began (empty when not electing)
    std::optional<uint32_t> nm_election_start_ms_;
    bool surrendered_in_election_ =
        false;  ///< True if this node yielded to a higher-priority claimant
    uint8_t surrender_discovery_retries_ =
        0;  ///< Discovery windows spent waiting for the winner after surrender

    // Stable network identifier (generated at CreateNetwork, preserved across elections)
    std::atomic<uint16_t> network_id_{0};

    // Warm restart state (see ApplySnapshot())
    /// Listen time of a warm-restarted manager, in percent of a superframe,
    /// before it resumes its network
    static constexpr uint32_t kManagerResumeListenPercent = 150;
    /// Superframes per hop a warm-restarted member allows the network to
    /// take to reach it again before forming its own
    static constexpr uint32_t kWarmDiscoverySuperframesPerHop = 4;
    /// Listen time before a warm-restarted manager resumes its network
    /// (0 = no warm resume pending)
    uint32_t manager_resume_listen_ms_ = 0;
    bool resume_as_manager_ = false;
    /// Tick count when a warm-restarted manager resumes its network
    std::optional<uint32_t> manager_resume_deadline_ms_;
    /// Extra discovery time of a warm-restarted member (first discovery only)
    uint32_t warm_discovery_extension_ms_ = 0;
    /// Resumed after a deep sleep and no sync beacon heard since
    std::atomic<bool> awaiting_resync_{false};
    /// Consecutive superframes with a sync beacon a member needs after joining
    /// before it sleeps
    static constexpr uint8_t kSettleSuperframes = 3;
    /// Consecutive superframes with a sync beacon since joining (saturating)
    uint8_t settled_superframes_ = 0;
    /// Joiner whose join this node relays (0 = none)
    AddressType relayed_joiner_ = 0;
    /// Tick count when the hold for a relayed join ends
    uint32_t relay_hold_until_ms_ = 0;

    /// Hold sleep while the join of @p joiner is relayed
    void HoldSleepForRelayedJoin(AddressType joiner);
    /// Learned errors of the sleep clock (kept across network resets)
    power::SleepClockCalibration light_sleep_clock_;
    power::SleepClockCalibration deep_sleep_clock_;

    /// Sleeps since the last sync beacon, for the next calibration sample
    struct SleepTally {
        uint32_t light_ms = 0;    ///< Time in light sleep
        uint32_t deep_ms = 0;     ///< Time in deep sleep
        uint32_t woke_at_ms = 0;  ///< Tick count at the end of the last sleep
    };

    SleepTally sleep_tally_;
    /// How far ahead of the network the schedule was left at the last beacon
    /// (drifts below the resync threshold are not corrected)
    int32_t schedule_offset_ms_ = 0;

    /// Fold the drift seen at a beacon into the sleep-clock calibration
    /// when the node slept since the previous beacon
    void RecordSleepClockDrift(int32_t drift_ms);

    /**
     * @brief Control slot a warm-restarted manager holds for a member
     */
    struct ControlSlotReservation {
        AddressType address;
        uint8_t control_slot_index;
        uint32_t expires_at_ms;  ///< Tick count when the hold lapses
    };

    /// Members' control slots held until they are heard again; guarded by
    /// reservations_mutex_
    std::vector<ControlSlotReservation> control_slot_reservations_;
    mutable std::mutex reservations_mutex_;

    /// Resume the network of a warm-restarted manager
    Result ResumeNetworkAsManager();

    /**
     * @brief Drop reservations of members that are back or timed out
     *
     * @return true if a reservation was dropped
     */
    bool PruneControlSlotReservations();

    /// Reserved control slot of @p address (0xFF = none)
    uint8_t FindReservedControlSlot(AddressType address) const;

    /// Highest reserved control slot index (0 = none)
    uint8_t GetHighestReservedControlSlot() const;

    // State-change notification callback
    StateChangeCallback state_change_callback_;

    static constexpr uint8_t kDefaultTTL = 10;

    /// Per-node sequence counter and de-duplication cache, shared by every
    /// send and receive path.
    MessageCache message_cache_;

    // Reliable delivery / group multicast subsystem
    DataReceivedExCallback data_received_ex_callback_;

    /// Deliver a received payload addressed to @p dest (this node, a group or
    /// broadcast) to both the legacy and extended callbacks.
    void DeliverToApp(AddressType source, uint8_t seq, AddressType dest,
                      uint8_t hops, std::span<const uint8_t> payload);

    /// Estimate hops travelled from a message's remaining TTL.
    uint8_t HopsFromTtl(uint8_t remaining_ttl) const;

    /// Group multicast + reliable-delivery subsystem; owns the reliability
    /// state machine, group membership, and ack-collection windows.
    std::unique_ptr<ReliableMessaging> reliable_messaging_;

    /// TDMA slot-table scheduler; sole owner of the slot table and the
    /// slot-shaping operations extracted from this coordinator.
    std::unique_ptr<SlotScheduler> slot_scheduler_;

    /// Sync-beacon transmit/forward path (build, forward, time-stamp beacons).
    std::unique_ptr<SyncBeaconService> sync_beacon_service_;

    /**
     * @brief Guards config_, the local node attributes and compound
     *        routing-table updates.
     *
     * Lock order, outermost first: ReliableMessaging's mutex, network_mutex_,
     * the routing table's mutex, then the slot scheduler's and the message
     * queue's mutexes. No path holding an inner lock calls back into an outer
     * component, and user callbacks are never invoked while network_mutex_ is
     * held.
     */
    mutable std::mutex network_mutex_;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
