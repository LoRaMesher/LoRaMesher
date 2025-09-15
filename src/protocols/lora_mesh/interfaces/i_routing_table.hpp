/**
 * @file i_routing_table.hpp
 * @brief Interface for routing table implementations in mesh networks
 */

#pragma once

#include <functional>
#include <memory>
#include <vector>
#include "types/error_codes/result.hpp"
#include "types/messages/base_header.hpp"
#include "types/messages/loramesher/routing_table_entry.hpp"
#include "types/protocols/lora_mesh/network_node_route.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Interface for routing table implementations
 * 
 * Defines the contract for routing table management in mesh networks.
 * Implementations can provide different routing algorithms (distance-vector,
 * geographic, machine learning-based, etc.) while maintaining a consistent API.
 */
class IRoutingTable {
   public:
    /**
     * @brief Callback for route update notifications
     * 
     * @param route_added True if route was added/updated, false if removed
     * @param destination Destination address affected
     * @param next_hop Next hop address (0 if route removed)
     * @param hop_count Number of hops to destination
     */
    using RouteUpdateCallback =
        std::function<void(bool route_added, AddressType destination,
                           AddressType next_hop, uint8_t hop_count)>;

    /**
     * @brief Virtual destructor
     */
    virtual ~IRoutingTable() = default;

    // Core routing operations

    /**
     * @brief Find the next hop for a given destination
     * 
     * @param destination Target destination address
     * @return AddressType Next hop address, or 0 if no route exists
     */
    virtual AddressType FindNextHop(AddressType destination) const = 0;

    /**
     * @brief Update or add a route entry
     * 
     * @param source Source of the route update
     * @param destination Destination address
     * @param hop_count Number of hops to destination
     * @param link_quality Link quality metric (0-255)
     * @param allocated_data_slots Number of allocated data slots
     * @param current_time Current timestamp
     * @return bool True if the route was updated or added
     */
    virtual bool UpdateRoute(AddressType source, AddressType destination,
                             uint8_t hop_count, uint8_t link_quality,
                             uint8_t allocated_data_slots,
                             uint32_t current_time) = 0;

    /**
     * @brief Add a network node to the routing table
     * 
     * @param node Network node to add
     * @return bool True if the node was added successfully
     */
    virtual bool AddNode(
        const types::protocols::lora_mesh::NetworkNodeRoute& node) = 0;

    /**
     * @brief Update existing node information
     * 
     * @param node_address Node address to update
     * @param battery_level Battery level (0-100%)
     * @param is_network_manager Whether node is network manager
     * @param allocated_data_slots Number of allocated data slots
     * @param capabilities Node capability flags
     * @param current_time Current timestamp
     * @return bool True if the node was updated
     */
    virtual bool UpdateNode(AddressType node_address, uint8_t battery_level,
                            bool is_network_manager,
                            uint8_t allocated_data_slots, uint8_t capabilities,
                            uint32_t current_time) = 0;

    /**
     * @brief Remove a node from the routing table
     * 
     * @param address Address of node to remove
     * @return bool True if the node was removed
     */
    virtual bool RemoveNode(AddressType address) = 0;

    /**
     * @brief Remove inactive nodes based on timeout
     * 
     * @param current_time Current timestamp
     * @param route_timeout_ms Route timeout in milliseconds
     * @param node_timeout_ms Node timeout in milliseconds
     * @return size_t Number of nodes removed
     */
    virtual size_t RemoveInactiveNodes(uint32_t current_time,
                                       uint32_t route_timeout_ms,
                                       uint32_t node_timeout_ms) = 0;

    // Query operations

    /**
     * @brief Check if a node is present in the routing table
     * 
     * @param address Node address to check
     * @return bool True if the node is present
     */
    virtual bool IsNodePresent(AddressType address) const = 0;

    /**
     * @brief Get all network nodes in the routing table
     * 
     * @return const std::vector<NetworkNodeRoute>& Reference to nodes vector
     */
    virtual const std::vector<types::protocols::lora_mesh::NetworkNodeRoute>&
    GetNodes() const = 0;

    /**
     * @brief Get the number of nodes in the routing table
     * 
     * @return size_t Number of nodes
     */
    virtual size_t GetSize() const = 0;

    /**
     * @brief Get routing table entries for network advertisement
     * 
     * @param exclude_address Address to exclude from the entries (typically own address)
     * @return std::vector<RoutingTableEntry> Vector of routing entries
     */
    virtual std::vector<RoutingTableEntry> GetRoutingEntries(
        AddressType exclude_address) const = 0;

    /**
     * @brief Calculate link quality for a specific node
     * 
     * @param node_address Target node address
     * @return uint8_t Link quality (0-255), or 0 if node not found
     */
    virtual uint8_t GetLinkQuality(AddressType node_address) const = 0;

    // Configuration and callbacks

    /**
     * @brief Set the route update callback
     * 
     * @param callback Callback function to notify of route changes
     */
    virtual void SetRouteUpdateCallback(RouteUpdateCallback callback) = 0;

    /**
     * @brief Set maximum number of nodes in routing table
     * 
     * @param max_nodes Maximum number of nodes (0 for unlimited)
     */
    virtual void SetMaxNodes(size_t max_nodes) = 0;

    /**
     * @brief Clear all routes and nodes from the table
     */
    virtual void Clear() = 0;

    // Statistics and diagnostics

    /**
     * @brief Get routing statistics
     * 
     * @return std::string Human-readable routing statistics
     */
    virtual std::string GetStatistics() const = 0;

    /**
     * @brief Update link statistics for periodic measurements
     * 
     * Called periodically to update expected message counts for link quality calculation
     */
    virtual void UpdateLinkStatistics() = 0;

    /**
     * @brief Process a routing table message and update routes
     * 
     * This method processes a received routing table message and updates the routing
     * table with new or better routes. It handles source node updates and route
     * validation according to the distance-vector algorithm.
     * 
     * @param source_address Address of the node that sent the routing message
     * @param entries Vector of routing table entries from the message
     * @param reception_timestamp When the message was received
     * @param local_link_quality Link quality to the source node (0-255)
     * @param max_hops Maximum allowed hop count
     * @return bool True if any routes were updated
     */
    virtual bool ProcessRoutingTableMessage(
        AddressType source_address,
        const std::vector<RoutingTableEntry>& entries,
        uint32_t reception_timestamp, uint8_t local_link_quality,
        uint8_t max_hops) = 0;
};

/**
 * @brief Factory function to create default routing table implementation
 * 
 * @param node_address Local node address
 * @param max_nodes Maximum number of nodes (0 for unlimited)
 * @return std::unique_ptr<IRoutingTable> Routing table instance
 */
std::unique_ptr<IRoutingTable> CreateDistanceVectorRoutingTable(
    AddressType node_address, size_t max_nodes = 50);

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher