/**
 * @file resume_snapshot.hpp
 * @brief Member state kept across a deep sleep
 */

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "types/messages/base_header.hpp"
#include "types/power/sleep_clock_calibration.hpp"
#include "types/protocols/lora_mesh/network_node_route.hpp"
#include "types/protocols/lora_mesh/slot_allocation.hpp"
#include "types/storage/network_snapshot.hpp"

namespace loramesher {
namespace storage {

/**
 * @brief Superframe schedule and clocks when the snapshot was taken
 *
 * Tick values use the protocol clock (RTOS tick count in milliseconds), which
 * continues across the sleep: the node restores it from the persistent clock
 * so every saved timestamp keeps its meaning.
 */
struct ResumeTiming {
    /// Protocol clock when the snapshot was taken
    uint32_t tick_at_save_ms = 0;
    /// Persistent clock (runs during deep sleep) when the snapshot was taken
    uint64_t persistent_time_at_save_us = 0;
    /// Protocol clock by which the node must run again (next active slot)
    uint32_t wake_deadline_ms = 0;
    /// Start of the superframe the snapshot was taken in
    uint32_t superframe_start_ms = 0;
    /// Superframes completed when that superframe started
    uint32_t superframes_completed = 0;
    /// Duration of each slot in milliseconds
    uint32_t slot_duration_ms = 0;
    /// Number of slots in the superframe
    uint16_t total_slots = 0;

    bool operator==(const ResumeTiming& other) const {
        return tick_at_save_ms == other.tick_at_save_ms &&
               persistent_time_at_save_us == other.persistent_time_at_save_us &&
               wake_deadline_ms == other.wake_deadline_ms &&
               superframe_start_ms == other.superframe_start_ms &&
               superframes_completed == other.superframes_completed &&
               slot_duration_ms == other.slot_duration_ms &&
               total_slots == other.total_slots;
    }
};

/**
 * @brief Membership of the node in its network
 *
 * Together with the routes, it determines the node's slot table, so the node
 * rebuilds the same table when it resumes.
 */
struct MemberState {
    AddressType network_manager = 0;     ///< Network manager of the network
    uint8_t slots_per_superframe = 0;    ///< Slot count from the sync beacon
    uint8_t beacon_node_count = 1;       ///< Node count from the sync beacon
    uint8_t control_slot_index = 0xFF;   ///< Control slot of the node
    uint8_t allocated_data_slots = 0;    ///< Data slots of the node
    uint8_t table_version = 0;           ///< Version of the last routing table
    uint32_t last_sync_time_ms = 0;      ///< Last synchronization (tick)
    uint32_t last_sync_beacon_ms = 0;    ///< Last sync beacon received (tick)
    uint32_t last_route_cleanup_ms = 0;  ///< Last route cleanup (tick)
    /// Learned error of the sleep clock in light sleep
    power::SleepClockCalibration light_sleep_clock;
    /// Learned error of the sleep clock in deep sleep
    power::SleepClockCalibration deep_sleep_clock;
    /// How far ahead of the network the schedule was at the last beacon
    int16_t schedule_offset_ms = 0;

    bool operator==(const MemberState& other) const {
        return network_manager == other.network_manager &&
               slots_per_superframe == other.slots_per_superframe &&
               beacon_node_count == other.beacon_node_count &&
               control_slot_index == other.control_slot_index &&
               allocated_data_slots == other.allocated_data_slots &&
               table_version == other.table_version &&
               last_sync_time_ms == other.last_sync_time_ms &&
               last_sync_beacon_ms == other.last_sync_beacon_ms &&
               last_route_cleanup_ms == other.last_route_cleanup_ms &&
               light_sleep_clock == other.light_sleep_clock &&
               deep_sleep_clock == other.deep_sleep_clock &&
               schedule_offset_ms == other.schedule_offset_ms;
    }
};

/**
 * @brief Slot table of the node, restored as it was so the node follows the
 *        same schedule until the rebuild it was waiting for
 */
struct SlotSchedule {
    /// Slots of the table, in table order
    std::vector<types::protocols::lora_mesh::SlotAllocation> slots;
    uint8_t control_slots = 0;     ///< Control slots of the network
    uint16_t discovery_slots = 0;  ///< Discovery slots of the network
    bool rebuild_pending = false;  ///< A change waits for the next rebuild
};

/**
 * @brief Next reliable message sequence toward one unicast destination
 */
struct SequenceStream {
    AddressType destination = 0;  ///< Unicast destination of the stream
    uint8_t next = 0;             ///< Next sequence the stream allocates

    bool operator==(const SequenceStream& other) const {
        return destination == other.destination && next == other.next;
    }
};

/**
 * @brief Reliable messages already delivered from one sender stream
 */
struct DeliveryStream {
    AddressType source = 0;  ///< Sender of the stream
    bool group = false;      ///< Group stream (otherwise unicast to the node)
    uint8_t highest = 0;     ///< Highest sequence delivered
    uint32_t bitmap = 0;     ///< Bit b set: sequence highest - b delivered
    uint32_t last_send_ts = 0;  ///< Newest sender timestamp seen

    bool operator==(const DeliveryStream& other) const {
        return source == other.source && group == other.group &&
               highest == other.highest && bitmap == other.bitmap &&
               last_send_ts == other.last_send_ts;
    }
};

/**
 * @brief Message recently received, kept to discard its duplicates
 */
struct SeenMessage {
    AddressType source = 0;  ///< Sender of the message
    uint8_t sequence = 0;    ///< Sequence number of the message

    bool operator==(const SeenMessage& other) const {
        return source == other.source && sequence == other.sequence;
    }
};

/**
 * @brief State a member saves before a deep sleep to resume without rejoining
 *
 * The embedded NetworkSnapshot lets the node fall back to a warm restart when
 * it cannot resume, for example because it woke up too late.
 */
struct ResumeSnapshot {
    /// Warm restart state of the node (a member: no reservations)
    NetworkSnapshot network;
    ResumeTiming timing;
    MemberState member;
    SlotSchedule schedule;
    /// Routing table, including the node's own entry and link statistics
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute> routes;
    /// Reliable unicast sequence streams
    std::vector<SequenceStream> sequence_streams;
    /// Next sequence of the reliable group stream (empty: not started)
    std::optional<uint8_t> group_next_sequence;
    /// Delivered reliable messages, least recently used stream first
    std::vector<DeliveryStream> delivery_streams;
    /// Recently received messages, oldest first
    std::vector<SeenMessage> seen_messages;
};

}  // namespace storage
}  // namespace loramesher
