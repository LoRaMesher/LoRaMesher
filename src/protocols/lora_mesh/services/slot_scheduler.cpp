/**
 * @file slot_scheduler.cpp
 * @brief Implementation of the TDMA slot-table scheduler.
 */

#include "slot_scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "utils/logger.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

SlotScheduler::SlotScheduler(Host host) : host_(std::move(host)) {}

Result SlotScheduler::UpdateSlotTableIfDirty(const Context& ctx, bool force) {
    if (!force && !slot_table_dirty_) {
        return Result::Success();
    }
    return UpdateSlotTable_Impl(ctx);
}

std::vector<SlotScheduler::NetworkNodeRoute> SlotScheduler::BuildOrderedNodes(
    const Context& ctx) const {
    // Get all nodes including self ordered.
    std::vector<NetworkNodeRoute> ordered_nodes = host_.get_routing_nodes();

    // Ensure self node is included for CONTROL_TX slot allocation
    // (self may not be in routing table since we removed self-entries)
    bool self_found =
        std::any_of(ordered_nodes.begin(), ordered_nodes.end(),
                    [&ctx](const NetworkNodeRoute& node) {
                        return node.GetAddress() == ctx.node_address;
                    });

    if (!self_found) {
        NetworkNodeRoute self_node(ctx.node_address, 0, false,
                                   ctx.local_capabilities,
                                   ctx.local_allocated_data_slots, 0);
        self_node.is_active = true;
        self_node.is_network_manager =
            (ctx.network_manager == ctx.node_address);
        ordered_nodes.push_back(self_node);
    }

    std::sort(ordered_nodes.begin(), ordered_nodes.end(),
              [](const NetworkNodeRoute& a, const NetworkNodeRoute& b) {
                  // Primary: Network Manager transmits first (has most complete routing info)
                  if (a.is_network_manager != b.is_network_manager) {
                      return a.is_network_manager > b.is_network_manager;
                  }
                  // Secondary: address-based deterministic ordering (same across all nodes)
                  return a.routing_entry.destination <
                         b.routing_entry.destination;
              });

    return ordered_nodes;
}

SlotScheduler::SlotPlan SlotScheduler::ComputeBandSizes(
    const Context& ctx, const std::vector<NetworkNodeRoute>& ordered_nodes) {
    SlotPlan plan;

    // Get the total data slots allocated (includes self's local_allocated_data_slots_)
    plan.total_data_slots = host_.get_allocated_data_slots();

    // Use max_hops from received sync beacons
    int max_hops_count = ctx.current_network_depth;

    if (ctx.network_manager == ctx.node_address) {
        // NM: compute from actual assignments. Ignore any out-of-range index
        // (a corrupted/garbage value would otherwise inflate the control band
        // and overflow the superframe arithmetic).
        uint8_t max_index =
            (ctx.my_control_slot_index != 0xFF) ? ctx.my_control_slot_index : 0;
        for (const auto& node : ordered_nodes) {
            if (node.control_slot_index != 0xFF &&
                node.control_slot_index < ctx.max_network_nodes &&
                node.control_slot_index > max_index) {
                max_index = node.control_slot_index;
            }
        }
        allocated_control_slots_ = static_cast<uint8_t>(
            std::min<uint16_t>(static_cast<uint16_t>(max_index) + 1,
                               static_cast<uint16_t>(ctx.max_network_nodes)));
    } else {
        // Non-NM: use authoritative node_count from sync beacon, clamped to the
        // configured maximum so a corrupt beacon can't inflate the control band.
        allocated_control_slots_ =
            std::min<uint8_t>(ctx.beacon_node_count, ctx.max_network_nodes);
    }

    // Add discovery slots, (max hops + 1) * 2 to get a full round trip message to the request
    allocated_discovery_slots_ = (max_hops_count + 1) * 2;

    // Add sync beacon slots 1 per hop layer
    plan.sync_beacon_slots = (max_hops_count + 1);

    // Calculate active slots (non-sleep). Computed in a wider type so the sum
    // cannot silently wrap uint8_t even if an input slipped past the clamps.
    plan.total_active_slots = static_cast<uint16_t>(plan.sync_beacon_slots) +
                              allocated_control_slots_ +
                              allocated_discovery_slots_ +
                              plan.total_data_slots;

    plan.total_superframe_slots =
        std::max<uint16_t>(ctx.number_of_slots_per_superframe, kMinSlots);

    // TX time used for both NM duty cycle computation and logging
    plan.tx_time_ms = 0;

    if (ctx.network_creator) {
        // Duty cycle applies only to TX time (not RX or sleep slots).
        // NM is the worst case: it transmits sync beacon + routing table + data.

        // Find NM's own data slot allocation
        // TODO: Find the grater node that contains the most allocated data slots. This will be the reference.
        // Or do the duty cycle by device and calculate it someway?
        uint8_t nm_data_slots = ctx.default_data_slots;
        for (const auto& node : ordered_nodes) {
            if (node.routing_entry.destination == ctx.node_address) {
                nm_data_slots = node.routing_entry.allocated_data_slots;
                break;
            }
        }

        // Calculate total NM TX time using Time-on-Air for each packet
        plan.tx_time_ms =
            host_.calculate_nm_tx_time(allocated_control_slots_, nm_data_slots);

        // Compute superframe size: total_tx_time / (slot_duration * duty_cycle)
        uint32_t slot_duration_ms = host_.get_slot_duration();
        if (slot_duration_ms == 0)
            slot_duration_ms = 1000;

        float required_superframe_ms =
            static_cast<float>(plan.tx_time_ms) / ctx.target_duty_cycle;
        uint8_t computed_slots = static_cast<uint8_t>(
            std::min(std::ceil(required_superframe_ms / slot_duration_ms),
                     static_cast<float>(255)));

        uint16_t min_for_sleep = plan.total_active_slots;
        if (ctx.min_sleep_fraction > 0.0f && ctx.min_sleep_fraction < 1.0f) {
            min_for_sleep = static_cast<uint16_t>(
                std::min(std::ceil(static_cast<float>(plan.total_active_slots) /
                                   (1.0f - ctx.min_sleep_fraction)),
                         255.0f));
        }
        uint16_t min_with_margin = std::min<uint16_t>(
            plan.total_active_slots + ctx.churn_margin_slots, 255);
        plan.total_superframe_slots =
            std::max<uint16_t>({static_cast<uint16_t>(computed_slots),
                                kMinSlots, min_for_sleep, min_with_margin});
    }

    // Hard cap: the beacon advertises the superframe size in a uint8_t wire
    // field, so the schedule must never exceed 255 slots. If the active band
    // alone would not fit, refuse to build a corrupt schedule.
    if (plan.total_active_slots > 255) {
        LOG_WARNING(
            "Active slot count %u exceeds wire limit; capping superframe",
            plan.total_active_slots);
    }
    plan.total_superframe_slots =
        std::min<uint16_t>(plan.total_superframe_slots, 255);

    plan.sleep_slots =
        (plan.total_superframe_slots > plan.total_active_slots)
            ? plan.total_superframe_slots - plan.total_active_slots
            : 0;
    uint32_t slot_duration_ms_log = host_.get_slot_duration();
    plan.actual_tx_duty_cycle =
        (slot_duration_ms_log > 0 && plan.tx_time_ms > 0)
            ? static_cast<float>(plan.tx_time_ms) /
                  (plan.total_superframe_slots * slot_duration_ms_log)
            : 0.0f;

    LOG_DEBUG("Total slots in the superframe %d (target TX duty cycle: %.2f%%)",
              plan.total_superframe_slots, ctx.target_duty_cycle * 100.0f);
    LOG_DEBUG("Active slots %d: sync %d, control %d, discovery %d, data %d",
              plan.total_active_slots, plan.sync_beacon_slots,
              allocated_control_slots_, allocated_discovery_slots_,
              plan.total_data_slots);
    LOG_DEBUG("SLEEP slots %d | actual TX duty cycle: %.2f%%", plan.sleep_slots,
              plan.actual_tx_duty_cycle * 100.0f);

    slot_count_ = plan.total_superframe_slots;

    // Ensure we never shrink below the NM-announced superframe size.
    // A stale/incomplete local routing table (e.g. after ApplyPendingJoin) can
    // compute fewer total slots than the NM expects, causing premature superframe
    // end and a 1000ms slot-skip on CalculateNextEventTimeout().
    if (slot_count_ < ctx.number_of_slots_per_superframe) {
        LOG_DEBUG("Clamping slot_count_ from %d to NM-announced %d",
                  slot_count_, ctx.number_of_slots_per_superframe);
        slot_count_ = ctx.number_of_slots_per_superframe;
    }

    return plan;
}

void SlotScheduler::FillSlotTable(
    const Context& ctx, const std::vector<NetworkNodeRoute>& ordered_nodes,
    const SlotPlan& plan) {
    // Single slot_index advances through all allocation phases
    size_t slot_index = 0;
    auto AllocateSlot = [&](SlotAllocation::SlotType type,
                            AddressType addr = 0) {
        slot_table_[slot_index] = SlotAllocation(slot_index, type, addr);
        slot_index++;
    };

    // Determine our hop distance from Network Manager
    uint8_t our_hop_distance = host_.get_hop_distance_to_nm();

    // ── Phase 1: Sync beacon slots (hop-layered forwarding) ──────────────────
    for (size_t hop_layer = 0;
         hop_layer < plan.sync_beacon_slots && slot_index < slot_count_;
         hop_layer++) {
        SlotAllocation::SlotType sync_type;
        if (hop_layer == 0) {
            sync_type = (ctx.in_network_manager_state &&
                         ctx.network_manager == ctx.node_address)
                            ? SlotAllocation::SlotType::SYNC_BEACON_TX
                            : SlotAllocation::SlotType::SYNC_BEACON_RX;
        } else if (our_hop_distance == hop_layer) {
            sync_type = SlotAllocation::SlotType::SYNC_BEACON_TX;
        } else if (our_hop_distance == hop_layer + 1) {
            sync_type = SlotAllocation::SlotType::SYNC_BEACON_RX;
        } else {
            sync_type = SlotAllocation::SlotType::SLEEP;
        }
        AllocateSlot(sync_type, kBroadcastAddress);
    }

    // ── Phase 2: Control slots (join-order indexed TX/RX) ────────────────────
    for (size_t i = 0; i < allocated_control_slots_ && slot_index < slot_count_;
         i++) {
        if (ctx.my_control_slot_index != 0xFF &&
            i == ctx.my_control_slot_index && ctx.network_manager != 0) {
            AllocateSlot(SlotAllocation::SlotType::CONTROL_TX,
                         ctx.node_address);
        } else {
            AllocateSlot(SlotAllocation::SlotType::CONTROL_RX,
                         kBroadcastAddress);
        }
    }

    // ── Phase 3: Data slots (per-node TX/RX/SLEEP) ───────────────────────────
    // Bound the data band to the budget that sized the superframe so a single
    // out-of-range per-node count cannot run past the frame and starve the
    // sleep/discovery tail. In a healthy network these limits are no-ops.
    uint16_t data_allocated = 0;
    for (const auto& node : ordered_nodes) {
        AddressType addr = node.GetAddress();
        uint8_t slot_data_number =
            std::min<uint8_t>(node.GetAllocatedDataSlots(), ctx.max_data_slots);
        for (size_t j = 0; j < slot_data_number; j++) {
            if (data_allocated >= plan.total_data_slots ||
                slot_index >= slot_count_) {
                break;
            }
            if (ctx.node_address == addr) {
                AllocateSlot(SlotAllocation::SlotType::TX, addr);
            } else if (node.IsDirectNeighbor()) {
                AllocateSlot(SlotAllocation::SlotType::RX, addr);
            } else {
                AllocateSlot(SlotAllocation::SlotType::SLEEP, addr);
            }
            data_allocated++;
        }
    }

    // ── Phase 4: Sleep (elastic buffer, shrinks to guarantee discovery tail) ──
    size_t remaining =
        (slot_index < slot_count_) ? slot_count_ - slot_index : 0;
    size_t discovery_reserve =
        std::min(static_cast<size_t>(allocated_discovery_slots_), remaining);
    size_t sleep_to_write = remaining - discovery_reserve;
    for (size_t i = 0; i < sleep_to_write; i++) {
        AllocateSlot(SlotAllocation::SlotType::SLEEP, 0);
    }

    // ── Phase 5: Discovery slots (always last in the superframe) ──────────────
    for (size_t i = 0; i < discovery_reserve; i++) {
        AllocateSlot(SlotAllocation::SlotType::DISCOVERY_RX, 0);
    }
}

Result SlotScheduler::UpdateSlotTable_Impl(const Context& ctx) {
    // Clear existing table
    slot_count_ = 0;

    std::vector<NetworkNodeRoute> ordered_nodes = BuildOrderedNodes(ctx);
    SlotPlan plan = ComputeBandSizes(ctx, ordered_nodes);
    FillSlotTable(ctx, ordered_nodes, plan);

    LogSlotTable(ctx);

    LOG_INFO(
        "Updated slot table: %d total (%d active: %d sync + %d ctrl + %d disc "
        "+ %d data, %d sleep, %.1f%% TX duty cycle)",
        plan.total_superframe_slots, plan.total_active_slots,
        plan.sync_beacon_slots, allocated_control_slots_,
        allocated_discovery_slots_, plan.total_data_slots, plan.sleep_slots,
        plan.actual_tx_duty_cycle * 100.0f);

    // Notify superframe service of new slot table
    Result result = host_.notify_superframe(slot_count_);
    if (!result) {
        LOG_ERROR("Failed to update superframe service with new slot table");
        return result;
    }

    slot_table_dirty_ = false;
    return Result::Success();
}

void SlotScheduler::LogSlotTable(const Context& ctx) const {
#if LORAMESHER_LOG_LEVEL > 0
    (void)ctx;
    return;
#else
    // 256 slots * 3 chars + row prefixes + header + detail ≈ 1024 max
    static char buf[1024];
    constexpr size_t kSlotsPerRow = 20;
    constexpr size_t kBufSize = sizeof(buf);

    auto Abbrev = [](SlotAllocation::SlotType t) -> const char* {
        switch (t) {
            case SlotAllocation::SlotType::SYNC_BEACON_TX:
                return "ST";
            case SlotAllocation::SlotType::SYNC_BEACON_RX:
                return "SR";
            case SlotAllocation::SlotType::CONTROL_TX:
                return "CT";
            case SlotAllocation::SlotType::CONTROL_RX:
                return "CR";
            case SlotAllocation::SlotType::TX:
                return "TX";
            case SlotAllocation::SlotType::RX:
                return "RX";
            case SlotAllocation::SlotType::SLEEP:
                return "..";
            case SlotAllocation::SlotType::DISCOVERY_RX:
                return "DR";
            case SlotAllocation::SlotType::DISCOVERY_TX:
                return "DT";
            default:
                return "??";
        }
    };

    size_t off = 0;
    auto Append = [&](const char* fmt,
                      ...) __attribute__((format(printf, 2, 3))) {
        if (off >= kBufSize)
            return;
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(buf + off, kBufSize - off, fmt, args);
        va_end(args);
        if (n > 0)
            off += std::min(static_cast<size_t>(n), kBufSize - off);
    };

    Append("SlotTable[%u] NM=%04X hop=%u:\n", slot_count_, ctx.network_manager,
           host_.get_hop_distance_to_nm());

    // Grid rows, 20 slots per row
    for (size_t row_start = 0; row_start < slot_count_;
         row_start += kSlotsPerRow) {
        Append("%02zu|", row_start);
        size_t row_end = std::min(row_start + kSlotsPerRow,
                                  static_cast<size_t>(slot_count_));
        for (size_t i = row_start; i < row_end; i++) {
            Append(i > row_start ? " %s" : "%s", Abbrev(slot_table_[i].type));
        }
        Append("\n");
    }

    // Data slot detail: group consecutive same-type slots with addresses
    bool has_detail = false;
    size_t i = 0;
    while (i < slot_count_) {
        const auto& slot = slot_table_[i];
        if (slot.type != SlotAllocation::SlotType::TX &&
            slot.type != SlotAllocation::SlotType::RX) {
            i++;
            continue;
        }

        size_t run_start = i;
        while (i + 1 < slot_count_ && slot_table_[i + 1].type == slot.type &&
               slot_table_[i + 1].target_address == slot.target_address) {
            i++;
        }

        if (!has_detail)
            Append("  ");
        else
            Append(" ");
        has_detail = true;

        if (slot.type == SlotAllocation::SlotType::TX) {
            if (run_start == i)
                Append("TX:#%zu(self)", run_start);
            else
                Append("TX:#%zu-%zu(self)", run_start, i);
        } else {
            if (run_start == i)
                Append("RX:#%zu(%04X)", run_start, slot.target_address);
            else
                Append("RX:#%zu-%zu(%04X)", run_start, i, slot.target_address);
        }
        i++;
    }

    loramesher::LOG.LogRaw(LogLevel::kDebug, buf);
#endif
}

Result SlotScheduler::SetDiscoverySlots() {
    // Clear existing discovery slots
    allocated_discovery_slots_ =
        std::max(ISuperframeService::DEFAULT_DISCOVERY_SLOT_COUNT,
                 static_cast<uint32_t>(slot_count_));

    slot_count_ = static_cast<uint16_t>(allocated_discovery_slots_);
    for (size_t i = 0; i < allocated_discovery_slots_; i++) {
        SlotAllocation slot;
        slot.slot_number = i;
        slot.target_address = kBroadcastAddress;  // Discovery to broadcast
        slot.type = SlotAllocation::SlotType::DISCOVERY_RX;

        slot_table_[i] = slot;
    }

    LOG_INFO("Updated discovery slots to %d", allocated_discovery_slots_);
    return Result::Success();
}

Result SlotScheduler::SetJoiningSlots(const Context& ctx) {
    // Use the same slot structure as the network but only listen to necessary slots
    // This ensures synchronization with the network manager's timing

    // First, use the normal slot allocation algorithm to get the network structure
    Result result = UpdateSlotTableIfDirty(ctx, true);
    if (!result.IsSuccess()) {
        LOG_ERROR("Failed to update slot table for joining: %s",
                  result.GetErrorMessage().c_str());
        return result;
    }

    // Modify slots for joining behavior:
    // - Convert most slots to SLEEP for power efficiency
    // - Keep essential CONTROL_RX slots for routing table updates
    // - Keep DISCOVERY_RX slots for receiving join responses
    // - Add DISCOVERY_TX slot for join requests

    size_t discovery_tx_added = 0;
    size_t active_slots = 0;

    for (auto& slot : slot_table_) {
        switch (slot.type) {
            case SlotAllocation::SlotType::SYNC_BEACON_RX:
                // Keep sync beacon slots active for synchronization
                active_slots++;
                break;

            case SlotAllocation::SlotType::SYNC_BEACON_TX:
                // Do not send sync beacon when joining
                slot.type = SlotAllocation::SlotType::SYNC_BEACON_RX;
                active_slots++;
                break;

            case SlotAllocation::SlotType::CONTROL_RX:
                // Keep control RX slots for join responses and network monitoring
                active_slots++;
                break;

            case SlotAllocation::SlotType::CONTROL_TX:
                // Convert TX slots into RX slots, we want to still listen TX slots.
                slot.type = SlotAllocation::SlotType::CONTROL_RX;
                active_slots++;
                break;

            case SlotAllocation::SlotType::DISCOVERY_RX:
                // Keep discovery RX slots for network monitoring
                active_slots++;
                // Convert first discovery TX slot to send join requests to network manager
                if (discovery_tx_added == 0) {
                    LOG_DEBUG(
                        "Converting slot %d from DISCOVERY_RX to DISCOVERY_TX "
                        "for joining",
                        slot.slot_number);
                    slot.target_address = ctx.network_manager;
                    slot.type = SlotAllocation::SlotType::DISCOVERY_TX;
                    discovery_tx_added++;
                } else {
                    LOG_DEBUG("Keeping slot %d as DISCOVERY_RX for joining",
                              slot.slot_number);
                }
                break;

            case SlotAllocation::SlotType::DISCOVERY_TX:
                // Keep discovery TX slots for waiting join response messages.
                active_slots++;
                break;

            case SlotAllocation::SlotType::TX:
            case SlotAllocation::SlotType::RX:
                // Convert data slots to sleep (no data transmission while joining)
                slot.type = SlotAllocation::SlotType::SLEEP;
                slot.target_address = 0;
                break;

            case SlotAllocation::SlotType::SLEEP:
                // Already sleep, no change
                break;
        }
    }

    float duty_cycle = (float)active_slots / slot_count_ * 100.0f;

    LOG_INFO(
        "Set joining slots: %zu active + %zu sleep = %zu total (%.1f%% duty "
        "cycle) - synchronized with network",
        active_slots, slot_count_ - active_slots, slot_count_, duty_cycle);

    return Result::Success();
}

void SlotScheduler::ExpandSyncBeaconListening(const Context& ctx) {
    uint8_t sync_beacon_slots =
        static_cast<uint8_t>(ctx.current_network_depth + 1);
    uint16_t limit =
        std::min(static_cast<uint16_t>(sync_beacon_slots), slot_count_);

    for (uint16_t i = 0; i < limit; i++) {
        auto& slot = slot_table_[i];
        if (slot.type == SlotAllocation::SlotType::SLEEP ||
            slot.type == SlotAllocation::SlotType::SYNC_BEACON_TX) {
            slot.type = SlotAllocation::SlotType::SYNC_BEACON_RX;
        }
    }

    LOG_WARNING(
        "Expanded sync beacon listening to all %d sync slots after %d "
        "missed beacons",
        limit, ctx.no_received_sync_beacon_count);
}

void SlotScheduler::RestoreSyncBeaconTxSlot(const Context& ctx) {
    if (ctx.no_received_sync_beacon_count < kExpandListeningThreshold) {
        // No expansion happened this superframe, so nothing to restore.
        // Guards against flipping a slot that is legitimately RX because the
        // slot table is stale w.r.t. our current hop distance (routing table
        // updates do not trigger a slot table rebuild).
        return;
    }
    uint8_t our_hop_distance = host_.get_hop_distance_to_nm();
    if (our_hop_distance == 0) {
        return;
    }
    uint16_t tx_index = static_cast<uint16_t>(our_hop_distance);
    if (tx_index >= slot_count_) {
        return;
    }
    auto& slot = slot_table_[tx_index];
    using SlotType = SlotAllocation::SlotType;
    if (slot.type == SlotType::SYNC_BEACON_RX) {
        slot.type = SlotType::SYNC_BEACON_TX;
        LOG_DEBUG(
            "Restored SYNC_BEACON_TX at slot %u after receiving beacon in "
            "expanded-listening mode",
            tx_index);
    }
}

bool SlotScheduler::ScheduleDiscoverySlotForwarding(
    AddressType network_manager) {
    // Find the next DISCOVERY_RX slot and temporarily convert it to TX
    // When next slot allocation the DISCOVERY_TX slot will be replaced by
    // a DISCOVERY_RX as previously set.
    for (auto& slot : slot_table_) {
        if (slot.type == SlotAllocation::SlotType::DISCOVERY_RX) {
            // Temporarily convert this slot to TX for forwarding
            slot.type = SlotAllocation::SlotType::DISCOVERY_TX;
            slot.target_address = network_manager;

            LOG_DEBUG("Scheduled discovery slot %d for forwarding to 0x%04X",
                      slot.slot_number, network_manager);

            return true;
        }
    }

    LOG_WARNING("No available DISCOVERY_RX slots found for forwarding");
    return false;
}

bool SlotScheduler::IsTDMANeighbor(AddressType address) const {
    for (size_t i = 0; i < slot_count_; ++i) {
        if (slot_table_[i].type == SlotAllocation::SlotType::RX &&
            slot_table_[i].target_address == address) {
            return true;
        }
    }
    return false;
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
