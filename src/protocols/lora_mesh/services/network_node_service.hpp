/**
 * @file network_node_service.hpp
 * @brief Network node management service implementation
 */

#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include "protocols/lora_mesh/interfaces/i_network_node_service.hpp"
#include "time_provider.hpp"
#include "types/protocols/lora_mesh/network_node.hpp"
#include "utils/logger.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Implementation of network node management service
 * 
 * Manages the collection of known nodes in the mesh network
 */
class NetworkNodeService : public INetworkNodeService {
   public:
    /**
     * @brief Constructor
     * 
     * @param time_provider Time provider for timestamp operations
     * @param max_nodes Maximum number of nodes to track (0 = unlimited)
     */
    explicit NetworkNodeService(std::shared_ptr<ITimeProvider> time_provider,
                                size_t max_nodes = 0);

    /**
     * @brief Virtual destructor
     */
    virtual ~NetworkNodeService() = default;

    // INetworkNodeService interface implementation
    bool UpdateNetworkNode(AddressType node_address, uint8_t battery_level,
                           bool is_network_manager) override;

    bool IsNodeInNetwork(AddressType node_address) const override;

    const std::vector<types::protocols::lora_mesh::NetworkNode>&
    GetNetworkNodes() const override;

    size_t GetNetworkSize() const override;

    size_t RemoveInactiveNodes(uint32_t timeout_ms) override;

    /**
     * @brief Update a node with full information
     * 
     * @param node_address Node address
     * @param battery_level Battery level (0-100%)
     * @param is_network_manager Whether node is network manager
     * @param capabilities Node capabilities bitmap
     * @param allocated_slots Number of allocated slots
     * @return bool True if node was newly added or significantly updated
     */
    bool UpdateNetworkNode(AddressType node_address, uint8_t battery_level,
                           bool is_network_manager, uint8_t capabilities,
                           uint8_t allocated_slots);

    /**
     * @brief Get a specific node by address
     * 
     * @param node_address Address to search for
     * @return const NetworkNode* Pointer to node, or nullptr if not found
     */
    const types::protocols::lora_mesh::NetworkNode* GetNode(
        AddressType node_address) const;

    /**
     * @brief Remove a specific node from the network
     * 
     * @param node_address Address of node to remove
     * @return bool True if node was found and removed
     */
    bool RemoveNode(AddressType node_address);

    /**
     * @brief Get all network manager nodes
     * 
     * @return std::vector<const NetworkNode*> Vector of network manager nodes
     */
    std::vector<const types::protocols::lora_mesh::NetworkNode*>
    GetNetworkManagers() const;

    /**
     * @brief Update node capabilities
     * 
     * @param node_address Node address
     * @param capabilities New capabilities bitmap
     * @return bool True if node was found and updated
     */
    bool UpdateNodeCapabilities(AddressType node_address, uint8_t capabilities);

    /**
     * @brief Update node allocated slots
     * 
     * @param node_address Node address
     * @param allocated_slots New number of allocated slots
     * @return bool True if node was found and updated
     */
    bool UpdateNodeAllocatedSlots(AddressType node_address,
                                  uint8_t allocated_slots);

    /**
     * @brief Get nodes with specific capability
     * 
     * @param capability Capability to search for
     * @return std::vector<const NetworkNode*> Vector of nodes with capability
     */
    std::vector<const types::protocols::lora_mesh::NetworkNode*>
    GetNodesWithCapability(uint8_t capability) const;

    /**
     * @brief Sort nodes by criteria (address, battery, last seen, etc.)
     * 
     * @param sort_by Sorting criteria
     */
    enum class SortCriteria {
        ADDRESS,
        BATTERY_LEVEL,
        LAST_SEEN,
        ALLOCATED_SLOTS
    };
    void SortNodes(SortCriteria sort_by);

    /**
     * @brief Get network statistics
     * 
     * @return NetworkStats Structure with network statistics
     */
    struct NetworkStats {
        size_t total_nodes;
        size_t network_managers;
        size_t active_nodes;
        uint8_t avg_battery_level;
        uint8_t total_allocated_slots;
        uint32_t oldest_node_age_ms;
    };

    NetworkStats GetNetworkStats() const;

    /**
     * @brief Set maximum number of nodes
     * 
     * @param max_nodes Maximum nodes (0 = unlimited)
     */
    void SetMaxNodes(size_t max_nodes) { max_nodes_ = max_nodes; }

    /**
     * @brief Get maximum number of nodes
     * 
     * @return size_t Maximum nodes (0 = unlimited)
     */
    size_t GetMaxNodes() const { return max_nodes_; }

   private:
    /**
     * @brief Find node iterator by address
     * 
     * @param node_address Address to search for
     * @return std::vector<NetworkNode>::iterator Iterator to node
     */
    std::vector<types::protocols::lora_mesh::NetworkNode>::iterator FindNode(
        AddressType node_address);

    /**
     * @brief Find node const iterator by address
     * 
     * @param node_address Address to search for
     * @return std::vector<NetworkNode>::const_iterator Const iterator to node
     */
    std::vector<types::protocols::lora_mesh::NetworkNode>::const_iterator
    FindNode(AddressType node_address) const;

    /**
     * @brief Check if adding a new node would exceed limits
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

    std::shared_ptr<ITimeProvider> time_provider_;
    std::vector<types::protocols::lora_mesh::NetworkNode> network_nodes_;
    size_t max_nodes_;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher