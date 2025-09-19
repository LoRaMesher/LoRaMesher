/**
 * @file application_types.hpp
 * @brief Data structures for application layer interface
 */

#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include "protocols/lora_mesh/interfaces/i_network_service.hpp"
#include "types/messages/base_header.hpp"

namespace loramesher {

/**
 * @brief Raw routing table entry for application access
 */
struct RouteEntry {
    AddressType destination;  ///< Destination node address
    AddressType next_hop;     ///< Next hop address for reaching destination
    uint8_t hop_count;        ///< Number of hops to destination
    uint8_t link_quality;     ///< Link quality metric (0-255)
    uint32_t last_seen_ms;    ///< Timestamp when route was last updated
    bool is_valid;            ///< Whether route is currently valid
};

/**
 * @brief Network status information for application access
 */
struct NetworkStatus {
    loramesher::protocols::lora_mesh::INetworkService::ProtocolState
        current_state;                 ///< Current protocol state
    AddressType network_manager;       ///< Network manager address
    uint16_t current_slot;             ///< Current slot number
    bool is_synchronized;              ///< Whether node is synchronized
    uint32_t time_since_last_sync_ms;  ///< Time since last sync beacon
    size_t connected_nodes;            ///< Number of known nodes in network
};

/**
 * @brief Simple data callback type for application layer
 *
 * @note Recommendation: Forward to separate task for processing
 */
using DataReceivedCallback =
    std::function<void(AddressType source, const std::vector<uint8_t>& data)>;

}  // namespace loramesher