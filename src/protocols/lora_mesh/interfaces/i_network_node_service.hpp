
/**
 * @file i_network_node_service.hpp
 * @brief Interface for network node management service
 */

#pragma once

#include <vector>
#include "types/error_codes/result.hpp"
#include "types/messages/base_header.hpp"
#include "types/protocols/lora_mesh/network_node.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Interface for network node service
 * 
 * Handles tracking and management of nodes in the mesh network.
 * This service maintains a registry of all known nodes with their
 * capabilities, status, and communication parameters.
 */
class INetworkNodeService {
   public:
    virtual ~INetworkNodeService() = default;

    /**
     * @brief Add or update a node in the network
     * 
     * If the node already exists, updates its information.
     * If it's a new node, adds it to the network registry.
     * 
     * @param node_address Address of the node
     * @param battery_level Battery level (0-100%)
     * @param is_network_manager Whether this node is a network manager
     * @return bool True if the node was newly added or significantly updated
     */
    virtual bool UpdateNetworkNode(AddressType node_address,
                                   uint8_t battery_level,
                                   bool is_network_manager) = 0;

    /**
     * @brief Check if a node exists in the network registry
     * 
     * @param node_address Address of the node to check
     * @return bool True if node exists in the network
     */
    virtual bool IsNodeInNetwork(AddressType node_address) const = 0;

    /**
     * @brief Get all known network nodes
     * 
     * @return const std::vector<NetworkNode>& Reference to the node list
     */
    virtual const std::vector<types::protocols::lora_mesh::NetworkNode>&
    GetNetworkNodes() const = 0;

    /**
     * @brief Get the number of nodes in the network
     * 
     * @return size_t Number of nodes
     */
    virtual size_t GetNetworkSize() const = 0;

    /**
     * @brief Remove nodes that haven't been seen recently
     * 
     * Removes nodes whose last_seen timestamp is older than the timeout.
     * This helps maintain an accurate view of the current network state.
     * 
     * @param timeout_ms Time in milliseconds after which a node is considered inactive
     * @return size_t Number of nodes removed
     */
    virtual size_t RemoveInactiveNodes(uint32_t timeout_ms) = 0;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher