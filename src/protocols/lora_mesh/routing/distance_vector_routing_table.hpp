/**
 * @file distance_vector_routing_table.hpp
 * @brief Distance-vector routing table implementation
 */

#pragma once

#include <algorithm>
#include <array>
#include <mutex>
#include <vector>
#include "protocols/lora_mesh/interfaces/i_routing_table.hpp"
#include "types/messages/loramesher/routing_table_entry.hpp"
#include "types/protocols/lora_mesh/network_node_route.hpp"
#include "utils/compat/span.hpp"
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
                     uint8_t allocated_data_slots, uint8_t capabilities,
                     uint32_t current_time) override;

    bool AddNode(
        const types::protocols::lora_mesh::NetworkNodeRoute& node) override;

    bool UpdateNode(AddressType node_address, uint8_t battery_level,
                    bool is_network_manager, uint8_t allocated_data_slots,
                    uint8_t capabilities, uint32_t current_time) override;

    bool RemoveNode(AddressType address) override;

    bool RefreshRoute(AddressType destination, uint32_t current_time) override;

    size_t RemoveInactiveNodes(uint32_t current_time, uint32_t route_timeout_ms,
                               uint32_t node_timeout_ms) override;

    // Query operations

    bool IsNodePresent(AddressType address) const override;

    const std::vector<types::protocols::lora_mesh::NetworkNodeRoute>& GetNodes()
        const override;

    std::vector<types::protocols::lora_mesh::NetworkNodeRoute> GetNodesCopy()
        const override;

    size_t GetSize() const override;

    std::vector<RoutingTableEntry> GetRoutingEntries(
        AddressType exclude_address) const override;

    std::vector<RoutingTableEntry> GetNextBroadcastSlice(
        AddressType exclude_address, size_t max_entries) override;

    uint8_t GetLinkQuality(AddressType node_address) const override;

    uint8_t GetDirectLinkQuality(AddressType node_address) const override;

    bool HasUnidirectionalRisk(AddressType node_address) const override;

    void DegradeRouteQuality(AddressType destination, uint8_t quality) override;

    // Configuration and callbacks

    void SetLogRoutingCapabilities(bool enable) override;

    /**
     * @brief Whether RTENTRY log lines currently include capability/slot fields.
     */
    bool IsLoggingCapabilities() const { return log_capabilities_; }

    /**
     * @brief Format a single RTENTRY log line for a route entry.
     *
     * When @p include_caps is true the line carries the node's capabilities and
     * allocated data slots (`cap=0x.. slots=..`), which tools can parse to
     * reconstruct gateway roles and per-node slot allocations.
     *
     * @param node The route entry to format
     * @param include_caps Whether to append capability/slot fields
     * @return std::string The formatted RTENTRY line
     */
    static std::string FormatRouteEntry(
        const types::protocols::lora_mesh::NetworkNodeRoute& node,
        bool include_caps);

    void SetRouteUpdateCallback(RouteUpdateCallback callback) override;

    void SetMaxNodes(size_t max_nodes) override;

    bool SetControlSlotIndex(AddressType node_address,
                             uint8_t control_slot_index) override;

    void Clear() override;

    // Statistics and diagnostics

    std::string GetStatistics() const override;

    void UpdateLinkStatistics() override;

    void SetLinkQualityParams(uint8_t ewma_alpha_fixed,
                              uint8_t inactivation_threshold,
                              uint8_t reactivation_threshold) override;

    bool ProcessRoutingTableMessage(
        AddressType source_address, std::span<const RoutingTableEntry> entries,
        uint32_t reception_timestamp, uint8_t local_link_quality,
        uint8_t max_hops, uint8_t source_capabilities = 0,
        uint8_t source_allocated_data_slots = 0, float rssi = 0.0f,
        float snr = 0.0f, uint8_t remote_absent_threshold = 1) override;

   private:
    // Internal helper methods

    /**
     * @brief Find a node by address
     * 
     * @param node_address Address to search for
     * @return Iterator to the node, or end() if not found
     */
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute>::iterator
    GetNode(AddressType node_address);

    /**
     * @brief Find a node by address (const version)
     * 
     * @param node_address Address to search for
     * @return Const iterator to the node, or end() if not found
     */
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute>::const_iterator
    GetNode(AddressType node_address) const override;

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

    void LogRouteEntry(
        const types::protocols::lora_mesh::NetworkNodeRoute& node);

    /**
     * @brief Record the last-known non-zero capabilities for a destination
     *
     * Capabilities are remembered independently of the route entry so that a
     * node erased by aging and later re-learned (possibly from a relay whose
     * rotating slice has not yet refreshed the capability bit) restores its
     * last-known capabilities instead of reverting to unknown (0x00).
     *
     * Backed by a fixed-capacity table to avoid runtime heap growth: when the
     * table is full the farthest (highest hop count) remembered node is
     * evicted in favour of a closer one.
     *
     * @param destination Destination address
     * @param capabilities Capabilities bitmap; ignored when 0
     * @param hop_count Hop count to the destination (used for eviction)
     */
    void RememberCapabilities(AddressType destination, uint8_t capabilities,
                              uint8_t hop_count);

    /**
     * @brief Recall the last-known capabilities for a destination
     *
     * @param destination Destination address
     * @return uint8_t Last-known capabilities, or 0 if none recorded
     */
    uint8_t RecallCapabilities(AddressType destination) const;

    // Link quality parameters (configurable via SetLinkQualityParams)

    uint8_t ewma_alpha_fixed_ = 77;  ///< EWMA alpha in fixed-point (0.30 * 256)
    /// Consecutive misses before hard invalidation (is_active = false)
    uint8_t inactivation_threshold_ = 10;
    /// Minimum received messages before invalidation applies
    static constexpr uint8_t kMinMessagesBeforeInvalidation = 1;
    /// Consecutive receptions required to re-activate an inactive route
    uint8_t reactivation_threshold_ = 2;
    /// Max superframes to keep probing an inactivated direct neighbor
    static constexpr uint8_t kMaxInactiveProbes = 32;
    /// Minimum quality to re-activate a probing neighbor (~25% PDR)
    static constexpr uint8_t kReactivationQualityThreshold = 64;

    // Member variables

    AddressType node_address_;        ///< Local node address
    mutable std::mutex table_mutex_;  ///< Thread safety mutex
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute>
        nodes_;                           ///< Routing table
    size_t max_nodes_;                    ///< Maximum number of nodes
    RouteUpdateCallback route_callback_;  ///< Route update callback
    /// When true, RTENTRY log lines include capabilities and data-slot fields.
    bool log_capabilities_ = false;

    // Statistics
    mutable uint32_t lookup_count_;       ///< Number of route lookups
    mutable uint32_t update_count_;       ///< Number of route updates
    mutable uint32_t last_cleanup_time_;  ///< Last cleanup timestamp

    /// Rotation cursor for sliced routing broadcasts; advances by slice
    /// size and wraps when the active set is exhausted. Reset on Clear().
    size_t next_broadcast_offset_ = 0;

    /// Rotation cursor for capability-bearing entries when there are more of
    /// them than fit in the per-slice priority reservation. Reset on Clear().
    size_t next_priority_offset_ = 0;

    /// Fixed-capacity memory of last-known capabilities for the closest
    /// capability-bearing nodes. Survives route aging so a re-learned node
    /// restores its capabilities, without allocating at runtime.
    static constexpr size_t kMaxRememberedCapabilities = 16;

    struct RememberedCapability {
        AddressType address = 0;
        uint8_t capabilities = 0;
        uint8_t hop_count = 0xFF;
        bool valid = false;
    };

    std::array<RememberedCapability, kMaxRememberedCapabilities>
        remembered_capabilities_{};
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher