/**
 * @file slot_scheduler.hpp
 * @brief TDMA slot-table scheduler extracted from NetworkService.
 *
 * Owns the superframe slot table and all of the slot-shaping operations
 * (rebuild, discovery/joining layouts, sync-beacon listening expansion,
 * discovery-forwarding). The owning NetworkService supplies the coordinator
 * state it needs as a read-only @ref Context snapshot plus a @ref Host bundle
 * of closures; the scheduler never reaches back into the coordinator directly.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "protocols/lora_mesh/interfaces/i_superframe_service.hpp"
#include "types/error_codes/result.hpp"
#include "types/messages/base_header.hpp"
#include "types/protocols/lora_mesh/network_node_route.hpp"
#include "types/protocols/lora_mesh/slot_allocation.hpp"
#include "utils/compat/span.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/// Missed beacons before expanding all sync slots to RX.
static const uint8_t kExpandListeningThreshold = 2;

/// Minimum number of slots in a superframe.
static const uint8_t kMinSlots = 16;

/**
 * @brief Owns and shapes the TDMA slot table for a single node.
 *
 * All operations run on the protocol task, so no internal synchronization is
 * required (matching the previously lock-free slot-table access in
 * NetworkService).
 */
class SlotScheduler {
   public:
    using SlotAllocation = types::protocols::lora_mesh::SlotAllocation;
    using NetworkNodeRoute = types::protocols::lora_mesh::NetworkNodeRoute;

    /**
     * @brief Read-only snapshot of coordinator state needed to build the table.
     *
     * Cheap to construct (scalars only); built per call by the coordinator.
     */
    struct Context {
        AddressType node_address = 0;
        AddressType network_manager = 0;
        bool in_network_manager_state = false;
        bool network_creator = false;
        uint8_t current_network_depth = 0;
        uint8_t number_of_slots_per_superframe = 0;
        uint8_t beacon_node_count = 1;
        uint8_t my_control_slot_index = 0xFF;
        uint8_t local_allocated_data_slots = 0;
        uint8_t local_capabilities = 0;
        uint8_t no_received_sync_beacon_count = 0;
        uint8_t max_network_nodes = 0;
        uint8_t max_data_slots = 0;
        uint8_t default_data_slots = 0;
        float target_duty_cycle = 0.01f;
        float min_sleep_fraction = 0.30f;
        uint8_t churn_margin_slots = 2;
    };

    /**
     * @brief Closures bound to the owning NetworkService.
     */
    struct Host {
        /// All routing nodes (ordered as the routing table returns them).
        std::function<std::vector<NetworkNodeRoute>()> get_routing_nodes;
        /// This node's hop distance to the Network Manager (0 if NM).
        std::function<uint8_t()> get_hop_distance_to_nm;
        /// Sum of active data-slot allocations (clamped to the budget).
        std::function<uint8_t()> get_allocated_data_slots;
        /// Slot duration in ms (returns a sane fallback when unavailable).
        std::function<uint32_t()> get_slot_duration;
        /// Total NM TX time (ms) for the given control/data slot counts.
        std::function<uint32_t(uint8_t control_slots, uint8_t data_slots)>
            calculate_nm_tx_time;
        /// Push the new total slot count to the superframe service.
        std::function<Result(uint16_t total_slots)> notify_superframe;
    };

    explicit SlotScheduler(Host host);

    // --- Dirty tracking / rebuild ------------------------------------------

    /// Mark the slot table dirty so the next rebuild regenerates it.
    void MarkDirty() { slot_table_dirty_ = true; }

    /// Clear the slot table (used when the node leaves/resets the network).
    void Reset() { slot_count_ = 0; }

    /// Rebuild when dirty, or unconditionally when @p force is set.
    Result UpdateSlotTableIfDirty(const Context& ctx, bool force);

    // --- Slot-table layouts -------------------------------------------------

    /// Lay out a discovery-only slot table (used before joining a network).
    Result SetDiscoverySlots();

    /// Lay out the power-efficient JOINING slot table (RX-mostly + 1 join TX).
    Result SetJoiningSlots(const Context& ctx);

    /// Expand sync-beacon slots to RX after repeated missed beacons.
    void ExpandSyncBeaconListening(const Context& ctx);

    /// Restore the SYNC_BEACON_TX slot demoted by ExpandSyncBeaconListening().
    void RestoreSyncBeaconTxSlot(const Context& ctx);

    /// Temporarily flip a DISCOVERY_RX slot to DISCOVERY_TX for forwarding.
    bool ScheduleDiscoverySlotForwarding(AddressType network_manager);

    // --- Queries ------------------------------------------------------------

    /// True if @p address has an RX slot allocated to it (TDMA reachable).
    bool IsTDMANeighbor(AddressType address) const;

    /// Span over the active slot allocations (valid for object lifetime).
    std::span<const SlotAllocation> GetSlotTable() const {
        return {slot_table_.data(), slot_count_};
    }

    /// Number of valid slots in the slot table.
    uint16_t GetSlotCount() const { return slot_count_; }

    /// Number of control slots allocated in the slot table.
    uint8_t GetAllocatedControlSlots() const {
        return allocated_control_slots_;
    }

   private:
    /// Aggregated sizing produced by ComputeBandSizes(), consumed downstream.
    struct SlotPlan {
        uint8_t sync_beacon_slots = 0;
        uint8_t total_data_slots = 0;
        uint16_t total_active_slots = 0;
        uint16_t total_superframe_slots = 0;
        uint16_t sleep_slots = 0;
        uint32_t tx_time_ms = 0;
        float actual_tx_duty_cycle = 0.0f;
    };

    Result UpdateSlotTable_Impl(const Context& ctx);

    /// Gather all nodes (including self) and sort into deterministic TX order.
    std::vector<NetworkNodeRoute> BuildOrderedNodes(const Context& ctx) const;

    /// Compute band sizes + superframe length; sets allocated_*_slots_ and
    /// slot_count_. Returns the derived plan used to fill the table.
    SlotPlan ComputeBandSizes(
        const Context& ctx, const std::vector<NetworkNodeRoute>& ordered_nodes);

    /// Fill the slot table phases (sync / control / data / sleep / discovery).
    void FillSlotTable(const Context& ctx,
                       const std::vector<NetworkNodeRoute>& ordered_nodes,
                       const SlotPlan& plan);

    /// Emit a debug rendering of the current slot table.
    void LogSlotTable(const Context& ctx) const;

    static constexpr size_t kMaxSlots = 256;

    Host host_;

    /// Fixed-size slot table — max 256 slots, no heap allocation.
    std::array<SlotAllocation, kMaxSlots> slot_table_{};
    uint16_t slot_count_ = 0;  ///< Number of valid slots in slot_table_
    uint8_t allocated_control_slots_ =
        ISuperframeService::DEFAULT_CONTROL_SLOT_COUNT;
    uint8_t allocated_discovery_slots_ =
        ISuperframeService::DEFAULT_DISCOVERY_SLOT_COUNT;

    /// Set when any input to the slot table changes. Protocol-task only.
    bool slot_table_dirty_ = true;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
