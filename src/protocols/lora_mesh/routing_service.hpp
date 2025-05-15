/**
 * @file routing_service.hpp
 * @brief Interface for routing table management
 */

#pragma once

#include <vector>
#include "types/error_codes/result.hpp"
#include "types/messages/base_message.hpp"

namespace loramesher {
namespace protocols {

/**
 * @brief Structure representing a routing table entry
 */
struct RoutingEntry {
    AddressType destination;  ///< Destination node address
    AddressType next_hop;     ///< Next hop to reach destination
    uint8_t hop_count;        ///< Number of hops to destination
    uint8_t allocated_slots;  ///< Number of data slots allocated
    uint8_t link_quality;     ///< Link quality metric (0-100%)
    uint32_t last_updated;    ///< Timestamp of last update
    bool is_active;           ///< Whether this route is active
};

/**
 * @brief Interface for routing service
 * 
 * Handles routing table management and route discovery
 */
class IRoutingService {
   public:
    virtual ~IRoutingService() = default;

    /**
     * @brief Process a routing table message
     * 
     * @param message The routing table message
     * @return Result Success if processed correctly, error details otherwise
     */
    virtual Result ProcessRoutingTableMessage(const BaseMessage& message) = 0;

    /**
     * @brief Send a routing table update
     * 
     * @return Result Success if update sent, error details otherwise
     */
    virtual Result SendRoutingTableUpdate() = 0;

    /**
     * @brief Find the best route to a destination
     * 
     * @param destination Destination address
     * @return AddressType Next hop address, or 0 if no route found
     */
    virtual AddressType FindNextHop(AddressType destination) const = 0;

    /**
     * @brief Update a routing entry based on received information
     * 
     * @param source Source of the routing update
     * @param destination Destination address in the route
     * @param hop_count Hop count reported by the source
     * @param link_quality Link quality reported by the source
     * @param allocated_slots Slots allocated to the destination
     * @return bool True if routing table was changed significantly
     */
    virtual bool UpdateRoutingEntry(AddressType source, AddressType destination,
                                    uint8_t hop_count, uint8_t link_quality,
                                    uint8_t allocated_slots) = 0;

    /**
     * @brief Get the current routing table entries
     * 
     * @return const std::vector<RoutingEntry>& Reference to routing entries
     */
    virtual const std::vector<RoutingEntry>& GetRoutingEntries() const = 0;
};

}  // namespace protocols
}  // namespace loramesher