/**
 * @file network_snapshot.hpp
 * @brief Protocol state kept across a reset of the node
 */

#pragma once

#include <cstdint>
#include <vector>

#include "types/messages/base_header.hpp"

namespace loramesher {
namespace storage {

/**
 * @brief Control slot held by the network manager for a member
 *
 * After a warm restart the network manager keeps each member's control slot
 * free until the member is heard again, so rejoining members get their old
 * slot back and the control band keeps its size.
 */
struct ControlSlotReservation {
    AddressType address = 0;         ///< Member that owned the slot
    uint8_t control_slot_index = 0;  ///< Control slot index of the member

    bool operator==(const ControlSlotReservation& other) const {
        return address == other.address &&
               control_slot_index == other.control_slot_index;
    }
};

/**
 * @brief State a node saves before a reset and restores when it starts again
 *
 * Holds only state that cannot be rebuilt from the network: timing, routes
 * and slot tables are learned again after the restart.
 */
struct NetworkSnapshot {
    /// Address of the node that took the snapshot
    AddressType node_address = 0;
    /// The node was the network manager when the snapshot was taken
    bool was_network_manager = false;
    /// Identifier of the network the node belonged to (0 = none)
    uint16_t network_id = 0;
    /// Last sequence number the node used for its outgoing messages
    uint8_t last_sequence = 0;
    /// Network depth (maximum hops from the network manager)
    uint8_t network_depth = 0;
    /// Superframe duration of the network in milliseconds
    uint32_t superframe_duration_ms = 0;
    /// Members' control slots (network manager only)
    std::vector<ControlSlotReservation> reservations;

    bool operator==(const NetworkSnapshot& other) const {
        return node_address == other.node_address &&
               was_network_manager == other.was_network_manager &&
               network_id == other.network_id &&
               last_sequence == other.last_sequence &&
               network_depth == other.network_depth &&
               superframe_duration_ms == other.superframe_duration_ms &&
               reservations == other.reservations;
    }
};

}  // namespace storage
}  // namespace loramesher
