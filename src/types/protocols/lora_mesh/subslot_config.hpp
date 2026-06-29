/**
 * @file subslot_config.hpp
 * @brief Value types for deterministic subslot-based TDMA collision mitigation
 *
 * Pure data types describing how a TDMA slot is divided into subslots. Kept in
 * the types layer so configuration structures can reference them without
 * depending on the service that consumes them (SubslotScheduler).
 */

#pragma once

#include <cstdint>

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Strategy for assigning a node to a subslot
 */
enum class SubslotAssignment : uint8_t {
    HOP_BASED,       ///< subslot = hop_count (sync beacon forwarding)
    ADDRESS_MODULO,  ///< subslot = address % num_subslots (discovery)
    RANDOM,  ///< caller provides random value; subslot = value % num_subslots
    ADDRESS_HASH  ///< caller mixes address with a per-superframe term; subslot =
                  ///< mix(address, superframe) % num_subslots
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

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher
