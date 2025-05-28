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
     * @brief Configuration for network service
     */
    struct NetworkConfig {
        AddressType node_address = 0;  ///< Local node address
        uint32_t hello_interval_ms =
            60000;  ///< Interval between hello messages
        uint32_t route_timeout_ms = 180000;     ///< Route expiration timeout
        uint32_t node_timeout_ms = 300000;      ///< Node expiration timeout
        uint32_t discovery_timeout_ms = 30000;  ///< Network discovery timeout
        uint8_t max_hops = 5;                   ///< Maximum hops for routing
        uint8_t max_packet_size = 255;          ///< Maximum packet size
        uint8_t default_data_slots = 1;  ///< Default data slots to request
        uint8_t max_network_nodes = 50;  ///< Maximum network nodes
    };

    // Node management methods

    /**
     * @brief Update node information in the network
     * 
     * @param node_address Node address
     * @param battery_level Battery level (0-100%)
     * @param is_network_manager Whether node is network manager
     * @param capabilities Node capabilities bitmap
     * @param allocated_slots Allocated slots for node
     * @return bool True if node was added or significantly updated
     */
    virtual bool UpdateNetworkNode(AddressType node_address,
                                   uint8_t battery_level,
                                   bool is_network_manager,
                                   uint8_t capabilities = 0,
                                   uint8_t allocated_slots = 0) = 0;

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
    virtual Result ProcessRoutingTableMessage(const BaseMessage& message) = 0;

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

    // Discovery methods

    /**
     * @brief Start network discovery
     * 
     * @return Result Success or error
     */
    virtual Result StartDiscovery() = 0;

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
     * @return Result Success or error
     */
    virtual Result ProcessReceivedMessage(const BaseMessage& message) = 0;

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
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher