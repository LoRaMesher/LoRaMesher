/**
 * @file subslot_scheduler.hpp
 * @brief Deterministic subslot-based collision mitigation for TDMA slots
 *
 * Provides timing calculations to divide a single TDMA slot into subslots,
 * allowing nodes at different hop distances (or with different addresses)
 * to transmit at non-overlapping times within the same slot.
 *
 * See PROTOCOL_SPEC.md Section 10.1.3: Collision Mitigation for Same-Hop
 * Forwarders.
 */

#pragma once

#include <cstdint>

#include "types/error_codes/result.hpp"
#include "types/protocols/lora_mesh/slot_allocation.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Strategy for assigning a node to a subslot
 */
enum class SubslotAssignment : uint8_t {
    HOP_BASED,       ///< subslot = hop_count (sync beacon forwarding)
    ADDRESS_MODULO,  ///< subslot = address % num_subslots (discovery)
    RANDOM  ///< caller provides random value; subslot = value % num_subslots
};

/**
 * @brief Configuration for subslot division within a slot
 */
struct SubslotConfig {
    uint8_t num_subslots = 5;  ///< Number of subslots to divide the slot into
    uint32_t guard_time_ms = 10;  ///< Guard time between subslots in ms
    SubslotAssignment strategy = SubslotAssignment::HOP_BASED;
};

/**
 * @brief Computed timing for a node's subslot within a slot
 */
struct SubslotTiming {
    uint8_t assigned_subslot = 0;     ///< Which subslot this node TXs in
    uint32_t tx_start_offset_ms = 0;  ///< Delay from slot start to TX
    uint32_t subslot_duration_ms =
        0;                      ///< Duration per subslot (guard + TX window)
    uint32_t tx_window_ms = 0;  ///< Actual TX time within subslot
    bool is_valid = false;      ///< Whether timing is feasible
};

/**
 * @brief Stateless utility for deterministic subslot timing calculations
 *
 * Divides a TDMA slot into subslots so that nodes transmit at different
 * offsets, avoiding collisions. Two assignment strategies are supported:
 * - HOP_BASED: subslot index = hop count (for sync beacon forwarding)
 * - ADDRESS_MODULO: subslot index = address % num_subslots (for discovery)
 *
 * Slot layout:
 * |Guard|Subslot0_TX|Guard|Subslot1_TX|...|Guard|SubslotN-1_TX|RX_Tail|
 */
class SubslotScheduler {
   public:
    /**
     * @brief Trailing guard margin reserved at the end of the last subslot's TX window.
     *
     * Ensures the last-subslot TX completes before the slot boundary, leaving
     * the superframe task enough time to re-schedule the next slot without
     * missing it.
     */
    static constexpr uint32_t kTrailingGuardMs = 30;
    /**
     * @brief Compute timing for a node's subslot within a slot
     *
     * @param slot_duration_ms Total slot duration in milliseconds
     * @param config Subslot configuration (num_subslots, guard_time, strategy)
     * @param node_identifier Node-specific value used for subslot assignment:
     *        - HOP_BASED: hop count to network manager
     *        - ADDRESS_MODULO: node address
     * @param toa_ms Time-on-air of the message to transmit. When > 0, the
     *        number of subslots is reduced so each subslot can hold one
     *        transmission (guard + ToA); at high spreading factors, where one
     *        message fills most of the slot, this collapses to a single
     *        subslot (transmit at slot start). When 0, the configured
     *        num_subslots is used (ToA unknown).
     * @return SubslotTiming Computed timing (check is_valid before use)
     */
    static SubslotTiming ComputeTiming(uint32_t slot_duration_ms,
                                       const SubslotConfig& config,
                                       uint16_t node_identifier,
                                       uint32_t toa_ms = 0);

    /**
     * @brief Validate that a subslot configuration is feasible for a slot
     *
     * Checks that subslots fit within the slot duration and that each
     * subslot has enough time for the estimated time-on-air.
     *
     * @param slot_duration_ms Total slot duration in milliseconds
     * @param config Subslot configuration to validate
     * @param estimated_toa_ms Estimated time-on-air for a single transmission
     * @return Result Success if valid, error with explanation if not
     */
    static Result ValidateConfig(uint32_t slot_duration_ms,
                                 const SubslotConfig& config,
                                 uint32_t estimated_toa_ms);

    /**
     * @brief Check if a slot type should use subslot scheduling
     *
     * Only SYNC_BEACON_TX and DISCOVERY_RX (which also transmits) use
     * subslot-based collision mitigation.
     *
     * @param slot_type The slot type to check
     * @return bool True if this slot type uses subslot scheduling
     */
    static bool IsSubslottedSlotType(
        types::protocols::lora_mesh::SlotAllocation::SlotType slot_type);
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
