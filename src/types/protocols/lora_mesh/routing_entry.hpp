/**
 * @file routing_entry.hpp
 * @brief Definition of routing table entry for LoRaMesh protocol
 */

#pragma once

#include <cstdint>
#include "types/error_codes/result.hpp"
#include "types/messages/base_header.hpp"
#include "utils/byte_operations.h"

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Structure representing a routing table entry
 * 
 * Contains information about a route to a destination node in the mesh network.
 */
struct RoutingEntry {
    AddressType destination = 0;  ///< Destination node address
    AddressType next_hop = 0;     ///< Next hop to reach destination
    uint8_t hop_count = 0;        ///< Number of hops to destination
    uint8_t allocated_slots = 0;  ///< Number of data slots allocated
    uint8_t link_quality = 0;     ///< Link quality metric (0-100%)
    uint32_t last_updated = 0;    ///< Timestamp of last update
    bool is_active = false;       ///< Whether this route is active

    /**
     * @brief Default constructor
     */
    RoutingEntry() = default;

    /**
     * @brief Constructor with all fields
     * 
     * @param dest Destination address
     * @param next Next hop address
     * @param hops Hop count to destination
     * @param slots Allocated data slots
     * @param quality Link quality metric (0-100%)
     * @param updated Last update timestamp
     * @param active Whether the route is active
     */
    RoutingEntry(AddressType dest, AddressType next, uint8_t hops,
                 uint8_t slots, uint8_t quality, uint32_t updated, bool active)
        : destination(dest),
          next_hop(next),
          hop_count(hops),
          allocated_slots(slots),
          link_quality(quality),
          last_updated(updated),
          is_active(active) {}

    /**
     * @brief Check if this route is better than another route
     * 
     * @param other The other route to compare against
     * @return bool True if this route is better
     */
    bool IsBetterThan(const RoutingEntry& other) const;

    /**
     * @brief Check if this route is expired based on a timeout
     * 
     * @param current_time Current timestamp
     * @param timeout_ms Timeout in milliseconds
     * @return bool True if the route is expired
     */
    bool IsExpired(uint32_t current_time, uint32_t timeout_ms) const;

    /**
     * @brief Update the route with new information
     * 
     * @param new_next_hop New next hop address
     * @param new_hop_count New hop count
     * @param new_link_quality New link quality
     * @param new_allocated_slots New allocated slots
     * @param current_time Current timestamp for update
     */
    void Update(AddressType new_next_hop, uint8_t new_hop_count,
                uint8_t new_link_quality, uint8_t new_allocated_slots,
                uint32_t current_time);

    /**
     * @brief Serialize the entry to a byte serializer
     * 
     * Used for creating routing table messages.
     * 
     * @param serializer The serializer to write to
     * @return Result Success if serialization succeeded
     */
    Result Serialize(utils::ByteSerializer& serializer) const;

    /**
     * @brief Deserialize an entry from a byte deserializer
     * 
     * Used for parsing routing table messages.
     * 
     * @param deserializer The deserializer to read from
     * @return std::optional<RoutingEntry> The entry if successful, nullopt otherwise
     */
    static std::optional<RoutingEntry> Deserialize(
        utils::ByteDeserializer& deserializer);

    /**
     * @brief Size of an entry when serialized
     * 
     * @return size_t Size of the entry in bytes
     */
    static constexpr size_t SerializedSize() {
        return sizeof(AddressType) +  // Destination address
               sizeof(AddressType) +  // Next hop address
               sizeof(uint8_t) +      // Hop count
               sizeof(uint8_t) +      // Allocated slots
               sizeof(uint8_t) +      // Link quality
               sizeof(uint32_t) +     // Last updated
               sizeof(uint8_t);       // Is active (as uint8_t)
    }

    /**
     * @brief Equality operator
     * 
     * @param other The other routing entry to compare
     * @return bool True if entries are equal
     */
    bool operator==(const RoutingEntry& other) const;

    /**
     * @brief Inequality operator
     * 
     * @param other The other routing entry to compare
     * @return bool True if entries are not equal
     */
    bool operator!=(const RoutingEntry& other) const;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher