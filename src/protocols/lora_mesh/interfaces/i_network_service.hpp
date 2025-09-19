/**
 * @file i_network_service.hpp
 * @brief Interface for combined network service
 */

#pragma once

#include <functional>
#include <vector>
#include "types/error_codes/result.hpp"
#include "types/messages/base_message.hpp"
#include "types/protocols/lora_mesh/network_node_route.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Interface for network service combining node management, routing, and discovery
 */
class INetworkService {
   public:
    virtual ~INetworkService() = default;

    /**
     * @brief Protocol state enumeration
     */
    enum class ProtocolState {
        INITIALIZING,      ///< Protocol is initializing
        DISCOVERY,         ///< Looking for existing network
        JOINING,           ///< Attempting to join network
        NORMAL_OPERATION,  ///< Normal network operation
        NETWORK_MANAGER,   ///< Acting as network manager
        FAULT_RECOVERY     ///< Attempting to recover from fault
    };

    /**
     * @brief Callback for route update notifications
     */
    using RouteUpdateCallback =
        std::function<void(bool route_updated, AddressType destination,
                           AddressType next_hop, uint8_t hop_count)>;

    /**
     * @brief Callback for received data messages
     */
    using DataReceivedCallback = std::function<void(
        AddressType source, const std::vector<uint8_t>& data)>;

    /**
     * @brief Configuration for network service
     */
    struct NetworkConfig {
        AddressType node_address = 0;  ///< Local node address
        uint32_t hello_interval_ms =
            60000;  ///< Interval between hello messages
        uint32_t route_timeout_ms = 180000;   ///< Route expiration timeout
        uint32_t node_timeout_ms = 300000;    ///< Node expiration timeout
        uint8_t max_hops = 5;                 ///< Maximum hops for routing
        uint8_t max_packet_size = 255;        ///< Maximum packet size
        uint8_t max_network_nodes = 50;       ///< Maximum network nodes
        uint8_t default_data_slots = 1;       ///< Default data slots to request
        uint8_t default_control_slots = 1;    ///< Default control slots
        uint8_t default_discovery_slots = 1;  ///< Default discovery slots
        uint32_t guard_time_ms = 50;  ///< TX guard time for RX readiness

        // Join request retry configuration
        uint8_t retry_delay_superframes =
            3;  ///< Delay in superframes for RETRY_LATER responses (default: 3)
        uint8_t max_join_retries =
            5;  ///< Maximum number of join retry attempts (default: 5)
        float backoff_multiplier =
            1.5f;  ///< Exponential backoff multiplier (default: 1.5)
        uint32_t max_retry_delay_ms =
            60000;  ///< Maximum retry delay cap in ms (default: 60s)
    };

    static constexpr AddressType kBroadcastAddress =
        0xFFFF;  ///< Broadcast address for routing

    // Node management methods

    /**
     * @brief Update node information in the network
     * 
     * @param node_address Node address
     * @param battery_level Battery level (0-100%)
     * @param is_network_manager Whether node is network manager
     * @param allocated_data_slots Allocated slots for node
     * @param capabilities Node capabilities bitmap
     * @return bool True if node was added or significantly updated
     */
    virtual bool UpdateNetworkNode(AddressType node_address,
                                   uint8_t battery_level,
                                   bool is_network_manager,
                                   uint8_t allocated_data_slots,
                                   uint8_t capabilities = 0) = 0;

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
    virtual bool UpdateNetwork(uint8_t allocated_control_slots = 0,
                               uint8_t allocated_discovery_slots = 0) = 0;

    /**
     * @brief Check if node exists in network
     * 
     * @param node_address Node address to check
     * @return bool True if node exists
     */
    virtual bool IsNodeInNetwork(AddressType node_address) const = 0;

    /**
     * @brief Get all network nodes with their routing information
     * 
     * @return std::vector<NetworkNodeRoute> All nodes and their routes
     */
    virtual const std::vector<types::protocols::lora_mesh::NetworkNodeRoute>&
    GetNetworkNodes() const = 0;

    /**
     * @brief Get total node count
     * 
     * @return size_t Number of nodes
     */
    virtual size_t GetNetworkSize() const = 0;

    /**
     * @brief Remove inactive nodes and routes
     * 
     * @return size_t Number of nodes removed
     */
    virtual size_t RemoveInactiveNodes() = 0;

    // Routing methods

    /**
     * @brief Process a routing update message
     * 
     * @param message Routing table message
     * @return Result Success or error
     */
    virtual Result ProcessRoutingTableMessage(const BaseMessage& message,
                                              uint32_t reception_timestamp) = 0;

    /**
     * @brief Send a routing table update message
     * 
     * @return Result Success or error
     */
    virtual Result SendRoutingTableUpdate() = 0;

    /**
     * @brief Find next hop to destination
     * 
     * @param destination Destination address
     * @return AddressType Next hop address or 0 if not found
     */
    virtual AddressType FindNextHop(AddressType destination) const = 0;

    /**
     * @brief Update route information
     * 
     * @param source Source of update
     * @param destination Destination address
     * @param hop_count Hop count
     * @param link_quality Link quality
     * @param allocated_slots Allocated slots
     * @return bool True if route was significantly updated
     */
    virtual bool UpdateRouteEntry(AddressType source, AddressType destination,
                                  uint8_t hop_count, uint8_t link_quality,
                                  uint8_t allocated_slots) = 0;

    /**
     * @brief Set route update callback
     *
     * @param callback Callback function
     */
    virtual void SetRouteUpdateCallback(RouteUpdateCallback callback) = 0;

    /**
     * @brief Set data received callback
     *
     * @param callback Callback function
     */
    virtual void SetDataReceivedCallback(DataReceivedCallback callback) = 0;

    // Discovery methods

    /**
     * @brief Start network discovery
     * 
     * @param discovery_timeout_ms Timeout for discovery in milliseconds
     * @return Result Success or error
     */
    virtual Result StartDiscovery(uint32_t discovery_timeout_ms) = 0;

    /**
     * @brief Start Joining process to an existing network
     * @param manager_address Network manager address
     * @param join_timeout_ms Timeout for joining in milliseconds
     * 
     * Initiates the joining process by sending a join request to the
     * network manager.
     * @return Result Success or error
     */
    virtual Result StartJoining(AddressType manager_address,
                                uint32_t join_timeout_ms) = 0;

    /**
     * @brief Check if network was found
     * 
     * @return bool True if network found
     */
    virtual bool IsNetworkFound() const = 0;

    /**
     * @brief Check if this node is network creator
     * 
     * @return bool True if this node created network
     */
    virtual bool IsNetworkCreator() const = 0;

    /**
     * @brief Process message received during discovery
     * 
     * @param message The received message
     * @param reception_timestamp Timestamp when the message was received (from RadioEvent)
     * @return Result Success or error
     */
    virtual Result ProcessReceivedMessage(const BaseMessage& message,
                                          uint32_t reception_timestamp) = 0;

    // Superframe integration

    /**
     * @brief Notify superframe of network changes
     * 
     * Triggers superframe updates based on network changes.
     * 
     * @return Result Success or error
     */
    virtual Result NotifySuperframeOfNetworkChanges() = 0;

    // State and configuration management

    /**
     * @brief Get current protocol state
     * 
     * @return ProtocolState Current state
     */
    virtual ProtocolState GetState() const = 0;

    /**
     * @brief Set protocol state
     * 
     * @param state New state
     */
    virtual void SetState(ProtocolState state) = 0;

    /**
     * @brief Get network manager address
     * 
     * @return AddressType Network manager address or 0 if none
     */
    virtual AddressType GetNetworkManagerAddress() const = 0;

    /**
     * @brief Set network manager address
     * 
     * @param manager_address Network manager address
     */
    virtual void SetNetworkManager(AddressType manager_address) = 0;

    /**
     * @brief Configure network service
     * 
     * @param config Service configuration
     * @return Result Success or error
     */
    virtual Result Configure(const NetworkConfig& config) = 0;

    /**
     * @brief Get current configuration
     * 
     * @return const NetworkConfig& Current configuration
     */
    virtual const NetworkConfig& GetConfig() const = 0;

    /**
     * @brief Reset network state and clear allocated resources
     * 
     * Clears network nodes, slot table, and resets state to initial values.
     * Should be called when stopping the protocol to prevent memory leaks.
     */
    virtual void ResetNetworkState() = 0;

    /**
     * @brief Set the number of slots per superframe
     */
    virtual void SetNumberOfSlotsPerSuperframe(uint8_t slots) = 0;

    /**
     * @brief Set the max number of hops of the actual network
     */
    virtual void SetMaxHopCount(uint8_t max_hops) = 0;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher