/**
 * @file network_service.hpp
 * @brief Implementation of combined network service
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
#include "types/protocols/lora_mesh/slot_allocation.hpp"
#include "utils/logger.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Implementation of network service
 * 
 * Combines node management, routing, and discovery in a unified service.
 */
class NetworkService : public INetworkService {
   public:
    /**
     * @brief Constructor
     * 
     * @param node_address Local node address
     * @param message_queue_service Message queue service
     * @param time_provider Time provider
     * @param superframe_service Superframe service
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
     * @brief Calculate link quality to a node
     * 
     * @param node_address Node address
     * @return uint8_t Link quality (0-100)
     */
    uint8_t CalculateLinkQuality(AddressType node_address) const;

    /**
     * @brief Create a routing table message
     * 
     * @param destination Destination address
     * @return std::unique_ptr<BaseMessage> Message object
     */
    std::unique_ptr<BaseMessage> CreateRoutingTableMessage(
        AddressType destination = 0xFFFF);

    /**
     * @brief Add a direct route to neighbor
     * 
     * @param neighbor_address Neighbor address
     * @param link_quality Link quality (0-100)
     * @return bool True if route added or updated
     */
    bool AddDirectRoute(AddressType neighbor_address,
                        uint8_t link_quality = 80);

    /**
     * @brief Join existing network
     * 
     * @param manager_address Network manager address
     * @return Result Success or error
     */
    Result JoinNetwork(AddressType manager_address);

    /**
     * @brief Create a new network
     * 
     * @return Result Success or error
     */
    Result CreateNetwork();

    /**
     * @brief Check if node is synchronized with network
     * 
     * @return bool True if synchronized
     */
    bool IsSynchronized() const { return is_synchronized_; }

   private:
    /**
     * @brief Find node by address
     * 
     * @param node_address Node address
     * @return std::vector<NetworkNodeRoute>::iterator Node iterator
     */
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute>::iterator
    FindNode(AddressType node_address);

    /**
     * @brief Find node by address (const version)
     * 
     * @param node_address Node address
     * @return std::vector<NetworkNodeRoute>::const_iterator Node iterator
     */
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute>::const_iterator
    FindNode(AddressType node_address) const;

    /**
     * @brief Check if limit would be exceeded
     * 
     * @return bool True if limit exceeded
     */
    bool WouldExceedLimit() const;

    /**
     * @brief Remove oldest node to make space
     * 
     * @return bool True if node removed
     */
    bool RemoveOldestNode();

    /**
     * @brief Update network topology after changes
     * 
     * @param notify_superframe Whether to notify superframe
     * @return bool True if topology changed
     */
    bool UpdateNetworkTopology(bool notify_superframe = true);

    /**
     * @brief Schedule routing message expectations
     * 
     * Sets expectations for future routing messages from each node.
     * This should be called on a regular interval to track link quality.
     */
    void ScheduleRoutingMessageExpectations();

    /**
     * @brief Reset link quality statistics
     * 
     * Resets link quality measurement period for all nodes
     */
    void ResetLinkQualityStats();

    AddressType node_address_;
    std::shared_ptr<IMessageQueueService> message_queue_service_;
    std::shared_ptr<ITimeProvider> time_provider_;
    std::shared_ptr<ISuperframeService> superframe_service_;
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute> network_nodes_;
    NetworkConfig config_;
    RouteUpdateCallback route_update_callback_;
    ProtocolState state_ = ProtocolState::INITIALIZING;
    AddressType network_manager_ = 0;
    bool network_found_ = false;
    bool network_creator_ = false;
    bool is_synchronized_ = false;
    uint32_t last_sync_time_ = 0;
    uint8_t table_version_ = 0;
    mutable std::mutex network_mutex_;

    /**
     * @brief Advanced link quality metrics
     */
    struct LinkQualityMetrics {
        // Primary metrics
        uint8_t reception_ratio;  ///< Reception success ratio (0-255)
        uint8_t signal_strength;  ///< Signal strength indicator (0-255)
        uint8_t stability;        ///< Link stability over time (0-255)

        // Calculate combined quality
        uint8_t CalculateCombinedQuality() const;
    };

    /**
     * @brief Calculate comprehensive link quality 
     * 
     * @param node_address Address of node to evaluate
     * @return uint8_t Link quality (0-255)
     */
    uint8_t CalculateComprehensiveLinkQuality(AddressType node_address);

    /**
     * @brief Calculate link stability metric
     * 
     * @param node The node to evaluate
     * @return uint8_t Stability metric (0-255)
     */
    uint8_t CalculateLinkStability(
        const types::protocols::lora_mesh::NetworkNodeRoute& node);
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher