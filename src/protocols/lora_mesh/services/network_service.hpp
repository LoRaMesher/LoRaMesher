/**
 * @file network_service.hpp
 * @brief Unified network service combining node management, routing, and discovery
 */

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "protocols/lora_mesh/interfaces/i_message_queue_service.hpp"
#include "protocols/lora_mesh/interfaces/i_network_service.hpp"
#include "protocols/lora_mesh/interfaces/i_superframe_service.hpp"
#include "types/messages/loramesher/join_request_message.hpp"
#include "types/messages/loramesher/join_response_header.hpp"
#include "types/messages/loramesher/join_response_message.hpp"
#include "types/messages/loramesher/routing_table_message.hpp"
#include "types/messages/loramesher/slot_allocation_message.hpp"
#include "types/messages/loramesher/slot_request_message.hpp"
#include "types/protocols/lora_mesh/network_node_route.hpp"
#include "types/protocols/lora_mesh/slot_allocation.hpp"
#include "utils/logger.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

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
     * @param time_provider Time provider for timing operations
     * @param superframe_service Optional superframe service for TDMA integration
     */
    NetworkService(
        AddressType node_address,
        std::shared_ptr<IMessageQueueService> message_queue_service,
        std::shared_ptr<ISuperframeService> superframe_service = nullptr);

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
     * @param battery_level Battery level (0-100%)
     * @param is_network_manager Whether this node is the network manager
     * @param allocated_data_slots Allocated data slots for this node
     * @param capabilities Node capabilities bitmap, if 0, get the previous value
     * @return bool True if node was added or significantly updated
     */
    bool UpdateNetworkNode(AddressType node_address, uint8_t battery_level,
                           bool is_network_manager,
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
     * @brief Get all network nodes with their routing information
     * 
     * Note: Caller must be careful with concurrent access as this returns
     * a reference to the internal vector.
     * 
     * @return const std::vector<NetworkNodeRoute>& Reference to all nodes
     */
    const std::vector<types::protocols::lora_mesh::NetworkNodeRoute>&
    GetNetworkNodes() const override;

    /**
     * @brief Get the number of nodes in the network
     * 
     * @return size_t Total number of nodes
     */
    size_t GetNetworkSize() const override;

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
    Result ProcessRoutingTableMessage(const BaseMessage& message) override;

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
     * @return bool True if routing table was significantly updated
     */
    bool UpdateRouteEntry(AddressType source, AddressType destination,
                          uint8_t hop_count, uint8_t link_quality,
                          uint8_t allocated_slots) override;

    /**
     * @brief Set callback for route update notifications
     * 
     * @param callback Function to call when routes are updated or removed
     */
    void SetRouteUpdateCallback(RouteUpdateCallback callback) override;

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
     * @brief Start joining an existing network
     * * Initiates the joining process by sending a join request to the
     * network manager. If no manager is found, it will create a new network.
     * * @param manager_address Address of the network manager to join
     * @param join_timeout_ms Timeout for joining in milliseconds
     * @return Result Success or error details
     */
    Result StartJoining(
        AddressType manager_address,
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
     * @return Result Success or error details
     */
    Result ProcessReceivedMessage(const BaseMessage& message) override;

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
    Result ProcessJoinRequest(const BaseMessage& message);

    /**
     * @brief Process a join response from network manager
     * 
     * Only processed when in joining state. Updates network configuration
     * if accepted.
     * 
     * @param message Join response message
     * @return Result Success or error
     */
    Result ProcessJoinResponse(const BaseMessage& message);

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
        uint8_t allocated_slots);

    // Slot management methods

    /**
     * @brief Process a slot request from a node
     * 
     * Only processed by network manager. Allocates slots if available.
     * 
     * @param message Slot request message
     * @return Result Success or error
     */
    Result ProcessSlotRequest(const BaseMessage& message);

    /**
     * @brief Process a slot allocation message
     * 
     * Updates local slot table based on allocation from network manager.
     * 
     * @param message Slot allocation message
     * @return Result Success or error
     */
    Result ProcessSlotAllocation(const BaseMessage& message);

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
     * @return const std::vector<SlotAllocation>& Slot table
     */
    const std::vector<types::protocols::lora_mesh::SlotAllocation>&
    GetSlotTable() const {
        return slot_table_;
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
    
   private:
    /**
     * @brief Find node by address
     * 
     * @param node_address Node address to find
     * @return Iterator to node or end()
     */
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute>::iterator
    FindNode(AddressType node_address);

    /**
     * @brief Find node by address (const version)
     * 
     * @param node_address Node address to find
     * @return Const iterator to node or end()
     */
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute>::const_iterator
    FindNode(AddressType node_address) const;

    /**
     * @brief Check if adding a node would exceed limits
     * 
     * @return bool True if limit would be exceeded
     */
    bool WouldExceedLimit() const;

    /**
     * @brief Remove oldest node to make space
     * 
     * @return bool True if a node was removed
     */
    bool RemoveOldestNode();

    /**
     * @brief Update network topology after changes
     * 
     * @param notify_superframe Whether to notify superframe service
     * @return bool True if topology changed significantly
     */
    bool UpdateNetworkTopology(bool notify_superframe = true);

    /**
     * @brief Advanced link quality metrics structure
     */
    struct LinkQualityMetrics {
        uint8_t reception_ratio;  ///< Message reception ratio (0-255)
        uint8_t signal_strength;  ///< Signal strength (0-255)
        uint8_t stability;        ///< Link stability metric (0-255)

        /**
         * @brief Calculate combined quality from all metrics
         * 
         * @return uint8_t Combined quality (0-255)
         */
        uint8_t CalculateCombinedQuality() const;
    };

    /**
     * @brief Calculate comprehensive link quality using multiple metrics
     * 
     * @param node_address Address of node to evaluate
     * @return uint8_t Comprehensive link quality (0-255)
     */
    uint8_t CalculateComprehensiveLinkQuality(AddressType node_address);

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
     * @param capabilities Node capabilities
     * @param requested_slots Requested slots
     * @return std::pair<bool, uint8_t> Accept decision and allocated slots
     */
    std::pair<bool, uint8_t> ShouldAcceptJoin(AddressType node_address,
                                              uint8_t capabilities,
                                              uint8_t requested_slots);

    /**
     * @brief Allocate data slots based on routing information
     * 
     * @param is_network_manager Whether this node is network manager
     * @param available_data_slots Number of available data slots
     */
    void AllocateDataSlotsBasedOnRouting(bool is_network_manager,
                                         uint16_t available_data_slots);

    /**
     * @brief Find next available slot
     * 
     * @param start_slot Starting slot to search from
     * @return uint16_t Next available slot or UINT16_MAX if none
     */
    uint16_t FindNextAvailableSlot(uint16_t start_slot);

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

    // Member variables
    AddressType node_address_;  ///< Local node address
    std::shared_ptr<IMessageQueueService> message_queue_service_;
    std::shared_ptr<ISuperframeService> superframe_service_;

    // Network state
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute> network_nodes_;
    std::vector<types::protocols::lora_mesh::SlotAllocation> slot_table_;
    NetworkConfig config_;
    RouteUpdateCallback route_update_callback_;
    ProtocolState state_;
    AddressType network_manager_;
    bool network_found_;
    bool network_creator_;
    bool is_synchronized_;
    uint32_t last_sync_time_;
    uint8_t table_version_;
    uint32_t discovery_start_time_;
    uint8_t allocated_control_slots_ = ISuperframeService::DEFAULT_CONTROL_SLOT_COUNT;
    uint8_t allocated_discovery_slots_ = ISuperframeService::DEFAULT_DISCOVERY_SLOT_COUNT;

    // Thread safety
    mutable std::mutex network_mutex_;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher