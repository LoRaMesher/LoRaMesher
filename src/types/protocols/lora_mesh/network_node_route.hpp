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
#include "types/messages/loramesher/routing_table_entry.hpp"
#include "types/protocols/lora_mesh/sliding_window_pdr.hpp"
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
        /// Minimum direct receptions before EWMA/window quality is reported
        static constexpr uint32_t kMinSamplesForQuality = 3;
        /// Quality reported below the sample threshold (~ETX 1024)
        static constexpr uint8_t kProvisionalQuality = 64;

        uint32_t messages_expected = 0;   ///< Expected messages count
        uint32_t messages_received = 0;   ///< Received messages count
        uint32_t last_message_time = 0;   ///< Last message received time
        uint8_t remote_link_quality = 0;  ///< Link quality as reported by peer
        uint8_t consecutive_missed = 0;   ///< Consecutive missed messages
        uint8_t ewma_quality =
            kProvisionalQuality;       ///< EWMA-smoothed link quality (0-255)
        uint8_t recovery_counter = 0;  ///< Messages received since inactivation
        uint8_t inactive_probe_count =
            0;                        ///< Superframes probed since inactivation
        uint8_t ewma_alpha = 77;      ///< EWMA alpha fixed-point (0.30 * 256)
        float last_rssi = 0.0f;       ///< Last RSSI from direct reception (dBm)
        float last_snr = 0.0f;        ///< Last SNR from direct reception (dB)
        SlidingWindowPDR<16> window;  ///< Sliding window PDR tracker

        /**
         * @brief Calculate link quality (0-255)
         *
         * Returns the EWMA-smoothed quality, optionally averaged with
         * remote link quality if available.
         *
         * @return uint8_t Calculated link quality
         */
        uint8_t CalculateQuality() const;

        /**
         * @brief Reset statistics
         */
        void Reset();

        /**
         * @brief Register expected message and decay EWMA quality
         */
        void ExpectMessage();

        /**
         * @brief Register received message and boost EWMA quality
         *
         * @param current_time Current timestamp
         * @param rssi RSSI of received packet (dBm), 0 = unknown
         * @param snr SNR of received packet (dB), 0 = unknown
         */
        void ReceivedMessage(uint32_t current_time, float rssi = 0.0f,
                             float snr = 0.0f);

        /**
         * @brief Update remote link quality
         *
         * @param quality Link quality as reported by peer
         */
        void UpdateRemoteQuality(uint8_t quality);
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
     * @brief Complete constructor with all node fields and hop count
     *
     * @param addr Node address
     * @param battery Battery level (0-100%)
     * @param time Current timestamp
     * @param is_manager Whether this is a network manager
     * @param caps Node capabilities bitmap
     * @param slots Number of allocated slots
     * @param hops Hop count to destination
     */
    NetworkNodeRoute(AddressType addr, uint8_t battery, uint32_t time,
                     bool is_manager, uint8_t caps, uint8_t slots,
                     uint8_t hops);

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
     * @brief Get the address of this node/route
     * @return AddressType Node address
     */
    AddressType GetAddress() const { return routing_entry.destination; }

    /**
     * @brief Get the allocated data slots of this node
     * @return Allocated data slots
     */
    uint8_t GetAllocatedDataSlots() const {
        return routing_entry.allocated_data_slots;
    }

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
     * Uses a composite cost metric: cost = hop_count × 35 + (255 - link_quality).
     * Lower cost is better. See PROTOCOL_SPEC.md Section 4.1.
     *
     * @param other The other route to compare against
     * @return bool True if this route is better
     */
    bool IsBetterRouteThan(const NetworkNodeRoute& other) const;

    /**
     * @brief Calculate route cost from hop count and link quality
     *
     * ETX-inspired metric: cost = hop_count × 65536 / max(quality, 1).
     * Approximates Expected Transmission Count (RFC 6551). Each hop adds
     * at least 256 to the cost, naturally penalizing longer paths.
     * Lower cost indicates a better route.
     *
     * @param hop_count Number of hops to destination
     * @param link_quality Link quality metric (0-255, higher is better)
     * @return uint16_t Route cost (lower is better)
     */
    static uint16_t CalculateRouteCost(uint8_t hop_count, uint8_t link_quality);

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
     * @param data_slots Allocated data slots
     * @param current_time Current timestamp
     * @return bool True if significant updates were made
     */
    bool UpdateNodeInfo(uint8_t battery, bool is_manager, uint8_t caps,
                        uint8_t data_slots, uint32_t current_time);

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
    bool UpdateFromRoutingTableEntry(const RoutingTableEntry& entry,
                                     AddressType next_hop,
                                     uint32_t current_time);

    /**
     * @brief Update battery level
     * 
     * @param new_battery New battery level (0-100%)
     * @param current_time Current timestamp
     * 
     * @return bool True if battery level changed
     */
    bool UpdateBatteryLevel(uint8_t new_battery, uint32_t current_time);

    /**
     * @brief Update allocated slots for this node
     * 
     * @param new_slots New number of allocated slots
     * @param current_time Current timestamp
     * 
     * @return bool True if slots changed
     */
    bool UpdateAllocatedSlots(uint8_t new_slots, uint32_t current_time);

    /**
     * @brief Update node capabilities
     * 
     * @param new_capabilities New capabilities bitmap
     * @param current_time Current timestamp
     * 
     * @return bool True if capabilities changed
     */
    bool UpdateCapabilities(uint8_t new_capabilities, uint32_t current_time);

    /**
     * @brief Check if node has a specific capability
     * @param capability Capability to check
     * 
     * @return bool True if node has the capability
     */
    bool HasCapability(uint8_t capability) const {
        return (routing_entry.capabilities & capability) != 0;
    }

    /**
     * @brief Get the capabilities bitmap
     * 
     * @return uint8_t Capabilities bitmap
     */
    uint8_t GetCapabilities() const { return routing_entry.capabilities; }

    /**
     * @brief Create routing table entry from this node
     * 
     * @return RoutingTableEntry Routing entry for network sharing
     */
    RoutingTableEntry ToRoutingTableEntry() const;

    /**
     * @brief Register expected routing message
     */
    void ExpectRoutingMessage();

    /**
     * @brief Register received routing message
     *
     * @param remote_quality Link quality reported by remote
     * @param current_time Current timestamp
     * @param rssi RSSI of received packet (dBm), 0 = unknown
     * @param snr SNR of received packet (dB), 0 = unknown
     */
    void ReceivedRoutingMessage(uint8_t remote_quality, uint32_t current_time,
                                float rssi = 0.0f, float snr = 0.0f);

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
     * @brief Size of a network node route when serialized
     *
     * @return size_t Size in bytes
     */
    static constexpr size_t SerializedSize() {
        return sizeof(AddressType) +  // Address
               sizeof(uint8_t) +      // Battery level
               sizeof(uint32_t) +     // Last seen
               sizeof(uint8_t) +      // Is network manager (as uint8_t)
               sizeof(AddressType) +  // Next hop
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
    RoutingTableEntry routing_entry;  ///< Routing entry for this node

    // Node status information
    uint8_t battery_level = 100;      ///< Battery level (0-100%)
    uint32_t last_seen = 0;           ///< Last time node was seen
    bool is_network_manager = false;  ///< Whether node is network manager

    // Routing information
    AddressType next_hop = 0;   ///< Next hop to reach this node
    uint32_t last_updated = 0;  ///< Last route update time
    bool is_active = false;     ///< Whether route is active

    // Link quality statistics (not serialized for network transmission)
    LinkQualityStats link_stats;  ///< Link quality statistics

    // Control slot tracking (NM-local, not serialized for network transmission)
    uint8_t control_slot_index =
        0xFF;  ///< Assigned control slot index (0xFF = unassigned)

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