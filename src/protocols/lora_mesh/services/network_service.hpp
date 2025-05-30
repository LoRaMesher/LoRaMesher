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
#include "time_provider.hpp"
#include "types/messages/loramesher/routing_table_message.hpp"
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
        std::shared_ptr<ITimeProvider> time_provider,
        std::shared_ptr<ISuperframeService> superframe_service = nullptr);

    /**
      * @brief Virtual destructor
      */
    virtual ~NetworkService() = default;

    // INetworkService node management implementation
    bool UpdateNetworkNode(AddressType node_address, uint8_t battery_level,
                           bool is_network_manager, uint8_t capabilities = 0,
                           uint8_t allocated_slots = 0) override;

    bool IsNodeInNetwork(AddressType node_address) const override;

    const std::vector<types::protocols::lora_mesh::NetworkNodeRoute>&
    GetNetworkNodes() const override;

    size_t GetNetworkSize() const override;

    size_t RemoveInactiveNodes() override;

    // INetworkService routing implementation
    Result ProcessRoutingTableMessage(const BaseMessage& message) override;

    Result SendRoutingTableUpdate() override;

    AddressType FindNextHop(AddressType destination) const override;

    bool UpdateRouteEntry(AddressType source, AddressType destination,
                          uint8_t hop_count, uint8_t link_quality,
                          uint8_t allocated_slots) override;

    void SetRouteUpdateCallback(RouteUpdateCallback callback) override;

    // INetworkService discovery implementation
    Result StartDiscovery() override;

    bool IsNetworkFound() const override;

    bool IsNetworkCreator() const override;

    Result ProcessReceivedMessage(const BaseMessage& message) override;

    // INetworkService superframe integration
    Result NotifySuperframeOfNetworkChanges() override;

    // INetworkService state and configuration
    ProtocolState GetState() const override;

    void SetState(ProtocolState state) override;

    AddressType GetNetworkManagerAddress() const override;

    void SetNetworkManager(AddressType manager_address) override;

    Result Configure(const NetworkConfig& config) override;

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
      * @brief Add or update a direct route to a neighbor
      * 
      * @param neighbor_address Neighbor node address
      * @param link_quality Initial link quality (0-255)
      * @return bool True if route was added or updated
      */
    bool AddDirectRoute(AddressType neighbor_address,
                        uint8_t link_quality = 128);

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

    // Member variables
    AddressType node_address_;  ///< Local node address
    std::shared_ptr<IMessageQueueService> message_queue_service_;
    std::shared_ptr<ITimeProvider> time_provider_;
    std::shared_ptr<ISuperframeService> superframe_service_;

    // Network state
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute> network_nodes_;
    NetworkConfig config_;
    RouteUpdateCallback route_update_callback_;
    ProtocolState state_;
    AddressType network_manager_;
    bool network_found_;
    bool network_creator_;
    bool is_synchronized_;
    uint32_t last_sync_time_;
    uint8_t table_version_;

    // Thread safety
    mutable std::mutex network_mutex_;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher