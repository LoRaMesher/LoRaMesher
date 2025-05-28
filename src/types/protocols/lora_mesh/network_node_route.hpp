/**
 * @file network_node_route.hpp
 * @brief Definition of combined network node and routing information
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include "types/error_codes/result.hpp"
#include "types/messages/base_header.hpp"
#include "utils/byte_operations.h"

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Combined structure representing a node and its routing information
 * 
 * Integrates network node information with routing data, providing a unified
 * view of nodes in the mesh network including their routing properties.
 */
class NetworkNodeRoute {
   public:
    /**
     * @brief Link quality statistics structure
     */
    struct LinkQualityStats {
        uint32_t messages_expected = 0;   ///< Expected messages count
        uint32_t messages_received = 0;   ///< Received messages count
        uint32_t last_message_time = 0;   ///< Last message received time
        uint8_t remote_link_quality = 0;  ///< Link quality as reported by peer

        /**
         * @brief Calculate link quality (0-255)
         * 
         * @return uint8_t Calculated link quality
         */
        uint8_t CalculateQuality() const;

        /**
         * @brief Reset statistics
         */
        void Reset();

        /**
         * @brief Register expected message
         */
        void ExpectMessage();

        /**
         * @brief Register received message
         * 
         * @param current_time Current timestamp
         */
        void ReceivedMessage(uint32_t current_time);

        /**
         * @brief Update remote link quality
         * 
         * @param quality Link quality as reported by peer
         */
        void UpdateRemoteQuality(uint8_t quality);
    };

    /**
     * @brief Routing table entry structure for updates
     */
    struct RoutingTableEntry {
        AddressType destination;  ///< Destination address
        uint8_t hop_count;        ///< Number of hops to destination
        uint8_t link_quality;     ///< Link quality metric (0-255)
        uint8_t allocated_slots;  ///< Number of data slots allocated

        /**
         * @brief Constructor
         */
        RoutingTableEntry(AddressType dest, uint8_t hops, uint8_t quality,
                          uint8_t slots)
            : destination(dest),
              hop_count(hops),
              link_quality(quality),
              allocated_slots(slots) {}

        /**
         * @brief Default constructor
         */
        RoutingTableEntry() = default;
    };

    /**
     * @brief Default constructor
     */
    NetworkNodeRoute() = default;

    /**
     * @brief Constructor with essential fields
     * 
     * @param addr Node address
     * @param time Current timestamp
     */
    NetworkNodeRoute(AddressType addr, uint32_t time);

    /**
     * @brief Complete constructor with all node fields
     * 
     * @param addr Node address
     * @param battery Battery level (0-100%)
     * @param time Current timestamp
     * @param is_manager Whether this is a network manager
     * @param caps Node capabilities bitmap
     * @param slots Number of allocated slots
     */
    NetworkNodeRoute(AddressType addr, uint8_t battery, uint32_t time,
                     bool is_manager = false, uint8_t caps = 0,
                     uint8_t slots = 0);

    /**
     * @brief Constructor with routing information
     * 
     * @param dest Destination address
     * @param next Next hop address
     * @param hops Hop count to destination
     * @param quality Link quality metric (0-255)
     * @param time Current timestamp
     */
    NetworkNodeRoute(AddressType dest, AddressType next, uint8_t hops,
                     uint8_t quality, uint32_t time);

    /**
     * @brief Check if this node/route is expired
     * 
     * @param current_time Current timestamp
     * @param timeout_ms Timeout in milliseconds
     * @return bool True if expired
     */
    bool IsExpired(uint32_t current_time, uint32_t timeout_ms) const;

    /**
     * @brief Check if this is a direct neighbor (hop count = 1)
     * 
     * @return bool True if direct neighbor
     */
    bool IsDirectNeighbor() const;

    /**
     * @brief Check if this route is better than another route
     * 
     * @param other The other route to compare against
     * @return bool True if this route is better
     */
    bool IsBetterRouteThan(const NetworkNodeRoute& other) const;

    /**
     * @brief Update the last seen timestamp
     * 
     * @param current_time Current timestamp
     */
    void UpdateLastSeen(uint32_t current_time);

    /**
     * @brief Update node information
     * 
     * @param battery Battery level (0-100%)
     * @param is_manager Whether node is network manager
     * @param caps Capabilities bitmap
     * @param slots Allocated slots
     * @param current_time Current timestamp
     * @return bool True if significant updates were made
     */
    bool UpdateNodeInfo(uint8_t battery, bool is_manager, uint8_t caps,
                        uint8_t slots, uint32_t current_time);

    /**
     * @brief Update routing information
     * 
     * @param new_next_hop Next hop address
     * @param new_hop_count Hop count
     * @param new_link_quality Link quality
     * @param current_time Current timestamp
     * @return bool True if significant updates were made
     */
    bool UpdateRouteInfo(AddressType new_next_hop, uint8_t new_hop_count,
                         uint8_t new_link_quality, uint32_t current_time);

    /**
     * @brief Update routing information from routing table entry
     * 
     * @param entry Routing table entry with updated information
     * @param next_hop Next hop address (usually the source of the routing update)
     * @param current_time Current timestamp
     * @return bool True if significant updates were made
     */
    bool UpdateFromRoutingEntry(const RoutingTableEntry& entry,
                                AddressType next_hop, uint32_t current_time);

    /**
     * @brief Create routing table entry from this node
     * 
     * @return RoutingTableEntry Routing entry for network sharing
     */
    RoutingTableEntry ToRoutingEntry() const;

    /**
     * @brief Register expected routing message
     */
    void ExpectRoutingMessage();

    /**
     * @brief Register received routing message
     * 
     * @param remote_quality Link quality reported by remote
     * @param current_time Current timestamp
     */
    void ReceivedRoutingMessage(uint8_t remote_quality, uint32_t current_time);

    /**
     * @brief Get current link quality
     * 
     * @return uint8_t Current link quality (0-255)
     */
    uint8_t GetLinkQuality() const;

    /**
     * @brief Get link quality as reported by remote node
     * 
     * @return uint8_t Remote link quality (0-255)
     */
    uint8_t GetRemoteLinkQuality() const;

    /**
     * @brief Reset link statistics for a new measurement period
     */
    void ResetLinkStats();

    /**
     * @brief Check if node has a specific capability
     * 
     * @param capability The capability to check (bit flag)
     * @return bool True if the node has the capability
     */
    bool HasCapability(uint8_t capability) const;

    /**
     * @brief Get capabilities as a string
     * 
     * @return std::string Human-readable capabilities
     */
    std::string GetCapabilitiesString() const;

    /**
     * @brief Size of a network node route when serialized
     * 
     * @return size_t Size in bytes
     */
    static constexpr size_t SerializedSize() {
        return sizeof(AddressType) +  // Address
               sizeof(uint8_t) +      // Battery level
               sizeof(uint32_t) +     // Last seen
               sizeof(uint8_t) +      // Is network manager (as uint8_t)
               sizeof(uint8_t) +      // Capabilities
               sizeof(uint8_t) +      // Allocated slots
               sizeof(AddressType) +  // Next hop
               sizeof(uint8_t) +      // Hop count
               sizeof(uint8_t) +      // Link quality
               sizeof(uint32_t) +     // Last updated
               sizeof(uint8_t);       // Is active (as uint8_t)
    }

    /**
     * @brief Serialize the network node route to a byte serializer
     * 
     * @param serializer The serializer to write to
     * @return Result Success if serialization succeeded
     */
    Result Serialize(utils::ByteSerializer& serializer) const;

    /**
     * @brief Deserialize a network node route from a byte deserializer
     * 
     * @param deserializer The deserializer to read from
     * @return std::optional<NetworkNodeRoute> The network node route if successful
     */
    static std::optional<NetworkNodeRoute> Deserialize(
        utils::ByteDeserializer& deserializer);

    // Node identity information
    AddressType address = 0;  ///< Node address (destination for routes)

    // Node status information
    uint8_t battery_level = 100;      ///< Battery level (0-100%)
    uint32_t last_seen = 0;           ///< Last time node was seen
    bool is_network_manager = false;  ///< Whether node is network manager
    uint8_t capabilities = 0;         ///< Node capabilities bitmap
    uint8_t allocated_slots = 0;      ///< Number of slots allocated to node

    // Routing information
    AddressType next_hop = 0;   ///< Next hop to reach this node
    uint8_t hop_count = 0;      ///< Number of hops to this node
    uint8_t link_quality = 0;   ///< Link quality metric (0-255)
    uint32_t last_updated = 0;  ///< Last route update time
    bool is_active = false;     ///< Whether route is active

    // Link quality statistics (not serialized for network transmission)
    LinkQualityStats link_stats;  ///< Link quality statistics

    /**
     * @brief Equality operator (based on address)
     */
    bool operator==(const NetworkNodeRoute& other) const;

    /**
     * @brief Inequality operator (based on address)
     */
    bool operator!=(const NetworkNodeRoute& other) const;

    /**
     * @brief Less than operator for sorting
     */
    bool operator<(const NetworkNodeRoute& other) const;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher