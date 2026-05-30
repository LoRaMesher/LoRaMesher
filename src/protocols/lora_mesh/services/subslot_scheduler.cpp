/**
 * @file subslot_scheduler.cpp
 * @brief Implementation of deterministic subslot-based collision mitigation
 */

#include "subslot_scheduler.hpp"

#include <algorithm>

#include "utils/logger.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

SubslotTiming SubslotScheduler::ComputeTiming(uint32_t slot_duration_ms,
                                              const SubslotConfig& config,
                                              uint16_t node_identifier,
                                              uint32_t toa_ms) {
    SubslotTiming timing;

    if (config.num_subslots == 0 || slot_duration_ms == 0) {
        LOG_WARNING("Invalid subslot config: num_subslots=%d, slot_duration=%u",
                    config.num_subslots, slot_duration_ms);
        return timing;  // is_valid = false
    }

    // Reserve a trailing guard so the last subslot's TX completes before the
    // slot boundary, leaving the superframe task time to re-schedule.
    uint32_t trailing_guard =
        std::min(kTrailingGuardMs, slot_duration_ms / 10u);

    // Fit as many subslots as the slot allows. Each subslot must hold one
    // transmission (guard + ToA), so when the time-on-air is a large fraction
    // of the slot (high spreading factors) the count collapses toward a single
    // subslot. With ToA unknown (0) the configured count is kept.
    uint8_t effective_subslots = config.num_subslots;
    if (toa_ms > 0) {
        uint32_t per_subslot = toa_ms + config.guard_time_ms;
        uint32_t usable = (slot_duration_ms > trailing_guard)
                              ? slot_duration_ms - trailing_guard
                              : 0;
        uint32_t fit = (per_subslot > 0) ? usable / per_subslot : 0;
        if (fit < 1)
            fit = 1;
        if (fit < effective_subslots)
            effective_subslots = static_cast<uint8_t>(fit);
    }

    // All assignment strategies map the identifier onto the feasible subslots.
    timing.assigned_subslot =
        static_cast<uint8_t>(node_identifier % effective_subslots);

    // Each subslot = guard_time + tx_window; the windows plus guards must fit
    // within slot_duration_ms.
    uint32_t total_guard_time =
        static_cast<uint32_t>(effective_subslots) * config.guard_time_ms;

    if (total_guard_time >= slot_duration_ms) {
        LOG_WARNING("Guard times alone (%u ms) exceed slot duration (%u ms)",
                    total_guard_time, slot_duration_ms);
        return timing;  // is_valid = false
    }

    if (total_guard_time + trailing_guard >= slot_duration_ms) {
        trailing_guard = 0;  // degrade gracefully for very short slots
    }
    uint32_t available_tx_time =
        slot_duration_ms - total_guard_time - trailing_guard;
    timing.tx_window_ms = available_tx_time / effective_subslots;
    timing.subslot_duration_ms = config.guard_time_ms + timing.tx_window_ms;

    if (timing.tx_window_ms == 0) {
        LOG_WARNING("TX window is zero: slot_duration=%u, subslots=%d",
                    slot_duration_ms, effective_subslots);
        return timing;  // is_valid = false
    }

    // TX start = subslot_index * subslot_duration + guard_time
    timing.tx_start_offset_ms = static_cast<uint32_t>(timing.assigned_subslot) *
                                    timing.subslot_duration_ms +
                                config.guard_time_ms;

    // With a known ToA, confirm the transmission completes within the slot.
    if (toa_ms > 0 && timing.tx_start_offset_ms + toa_ms + trailing_guard >
                          slot_duration_ms) {
        LOG_WARNING(
            "Subslot infeasible: offset=%u + ToA=%u + trailing=%u > "
            "slot=%u",
            timing.tx_start_offset_ms, toa_ms, trailing_guard,
            slot_duration_ms);
        return timing;  // is_valid = false
    }

    timing.is_valid = true;

    LOG_DEBUG(
        "Subslot timing: node_id=%u, subslot=%d/%d, tx_start=%u ms, "
        "tx_window=%u ms",
        node_identifier, timing.assigned_subslot, effective_subslots,
        timing.tx_start_offset_ms, timing.tx_window_ms);

    return timing;
}

Result SubslotScheduler::ValidateConfig(uint32_t slot_duration_ms,
                                        const SubslotConfig& config,
                                        uint32_t estimated_toa_ms) {
    if (config.num_subslots == 0) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Number of subslots must be > 0");
    }

    if (slot_duration_ms == 0) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Slot duration must be > 0");
    }

    uint32_t total_guard_time =
        static_cast<uint32_t>(config.num_subslots) * config.guard_time_ms;

    if (total_guard_time >= slot_duration_ms) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Guard times exceed slot duration");
    }

    uint32_t available_tx_time = slot_duration_ms - total_guard_time;
    uint32_t trailing_guard =
        std::min(kTrailingGuardMs, slot_duration_ms / 10u);
    if (total_guard_time + trailing_guard < slot_duration_ms) {
        available_tx_time -= trailing_guard;
    }
    uint32_t tx_window_ms = available_tx_time / config.num_subslots;

    if (tx_window_ms == 0) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "TX window would be zero");
    }

    if (estimated_toa_ms > tx_window_ms) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Estimated ToA exceeds TX window per subslot");
    }

    return Result::Success();
}

bool SubslotScheduler::IsSubslottedSlotType(
    types::protocols::lora_mesh::SlotAllocation::SlotType slot_type) {
    using SlotType = types::protocols::lora_mesh::SlotAllocation::SlotType;
    return slot_type == SlotType::SYNC_BEACON_TX ||
           slot_type == SlotType::DISCOVERY_TX ||
           slot_type == SlotType::DISCOVERY_RX;
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
