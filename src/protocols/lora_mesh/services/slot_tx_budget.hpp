/**
 * @file slot_tx_budget.hpp
 * @brief Time budget of a transmission inside a TDMA slot
 */

#pragma once

#include <cstdint>

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/// Time kept free at the end of a slot for the receiver to process the packet
/// (SPI transfer, interrupt latency, queue insertion), in ms
inline constexpr uint32_t kRxProcessingMarginMs = 20;

/// Delay from a slot's start until the node gets to transmit in it (slot
/// detection, task scheduling, logging) that messages built to fill a slot
/// must tolerate, in ms
inline constexpr uint32_t kTxStartLatencyBudgetMs = 100;

/**
 * @brief Whether a transmission fits in a slot
 *
 * @param time_in_slot_ms Time from the slot's start until the transmission
 *        is attempted
 * @param guard_ms Guard time waited before transmitting
 * @param toa_ms Time on air of the packet
 * @param slot_ms Slot duration
 * @return true if the packet and the receiver's processing margin end within
 *         the slot
 */
inline constexpr bool FitsInSlot(uint32_t time_in_slot_ms, uint32_t guard_ms,
                                 uint32_t toa_ms, uint32_t slot_ms) {
    return static_cast<uint64_t>(time_in_slot_ms) + guard_ms + toa_ms +
               kRxProcessingMarginMs <=
           slot_ms;
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
