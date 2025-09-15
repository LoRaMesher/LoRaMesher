/**
 * @file distance_vector_routing_table.hpp
 * @brief Distance-vector routing table implementation
 */

#pragma once

#include <algorithm>
#include <mutex>
#include <vector>
#include "protocols/lora_mesh/interfaces/i_routing_table.hpp"
#include "types/messages/loramesher/routing_table_entry.hpp"
#include "types/protocols/lora_mesh/network_node_route.hpp"
#include "utils/logger.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Distance-vector routing table implementation
 * 
 * Implements the Bellman-Ford distance-vector routing algorithm optimized for
 * wireless mesh networks. Features include:
 * - Hop count and link quality-based route selection
 * - Route aging and cleanup mechanisms
 * - Thread-safe operations with mutex protection
 * - Support for direct neighbor detection
 * - Link quality tracking and statistics
 */
class DistanceVectorRoutingTable : public IRoutingTable {
   public:
    /**
     * @brief Constructor
     * 
     * @param node_address Local node address
     * @param max_nodes Maximum number of nodes in routing table (0 = unlimited)
     */
    explicit DistanceVectorRoutingTable(AddressType node_address,
                                        size_t max_nodes = 50);

    /**
     * @brief Destructor
     */
    ~DistanceVectorRoutingTable() override = default;

    // Core routing operations (IRoutingTable interface)

    AddressType FindNextHop(AddressType destination) const override;

    bool UpdateRoute(AddressType source, AddressType destination,
                     uint8_t hop_count, uint8_t link_quality,
                     uint8_t allocated_data_slots,
                     uint32_t current_time) override;

    bool AddNode(
        const types::protocols::lora_mesh::NetworkNodeRoute& node) override;

    bool UpdateNode(AddressType node_address, uint8_t battery_level,
                    bool is_network_manager, uint8_t allocated_data_slots,
                    uint8_t capabilities, uint32_t current_time) override;

    bool RemoveNode(AddressType address) override;

    size_t RemoveInactiveNodes(uint32_t current_time, uint32_t route_timeout_ms,
                               uint32_t node_timeout_ms) override;

    // Query operations

    bool IsNodePresent(AddressType address) const override;

    const std::vector<types::protocols::lora_mesh::NetworkNodeRoute>& GetNodes()
        const override;

    size_t GetSize() const override;

    std::vector<RoutingTableEntry> GetRoutingEntries(
        AddressType exclude_address) const override;

    uint8_t GetLinkQuality(AddressType node_address) const override;

    // Configuration and callbacks

    void SetRouteUpdateCallback(RouteUpdateCallback callback) override;

    void SetMaxNodes(size_t max_nodes) override;

    void Clear() override;

    // Statistics and diagnostics

    std::string GetStatistics() const override;

    void UpdateLinkStatistics() override;

    bool ProcessRoutingTableMessage(
        AddressType source_address,
        const std::vector<RoutingTableEntry>& entries,
        uint32_t reception_timestamp, uint8_t local_link_quality,
        uint8_t max_hops) override;

   private:
    // Internal helper methods

    /**
     * @brief Find a node by address
     * 
     * @param node_address Address to search for
     * @return Iterator to the node, or end() if not found
     */
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute>::iterator
    FindNode(AddressType node_address);

    /**
     * @brief Find a node by address (const version)
     * 
     * @param node_address Address to search for
     * @return Const iterator to the node, or end() if not found
     */
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute>::const_iterator
    FindNode(AddressType node_address) const;

    /**
     * @brief Check if adding a node would exceed the limit
     * 
     * @return bool True if limit would be exceeded
     */
    bool WouldExceedLimit() const;

    /**
     * @brief Remove the oldest non-manager node to make space
     * 
     * @return bool True if a node was removed
     */
    bool RemoveOldestNode();

    /**
     * @brief Calculate comprehensive link quality for a node
     * 
     * @param node_address Target node address
     * @return uint8_t Calculated link quality (0-255)
     */
    uint8_t CalculateComprehensiveLinkQuality(AddressType node_address) const;

    /**
     * @brief Check if a potential route is better than the current route
     * 
     * @param current Current route
     * @param potential Potential new route
     * @return bool True if potential route is better
     */
    bool IsBetterRoute(
        const types::protocols::lora_mesh::NetworkNodeRoute& current,
        const types::protocols::lora_mesh::NetworkNodeRoute& potential) const;

    /**
     * @brief Notify callback of route changes
     * 
     * @param route_added True if route was added/updated
     * @param destination Destination address
     * @param next_hop Next hop address
     * @param hop_count Number of hops
     */
    void NotifyRouteUpdate(bool route_added, AddressType destination,
                           AddressType next_hop, uint8_t hop_count);

    // Member variables

    AddressType node_address_;        ///< Local node address
    mutable std::mutex table_mutex_;  ///< Thread safety mutex
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute>
        nodes_;                           ///< Routing table
    size_t max_nodes_;                    ///< Maximum number of nodes
    RouteUpdateCallback route_callback_;  ///< Route update callback

    // Statistics
    mutable uint32_t lookup_count_;       ///< Number of route lookups
    mutable uint32_t update_count_;       ///< Number of route updates
    mutable uint32_t last_cleanup_time_;  ///< Last cleanup timestamp
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher