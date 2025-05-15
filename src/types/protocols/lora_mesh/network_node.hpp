/**
 * @file network_node.hpp
 * @brief Definition of network node information for LoRaMesh protocol
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "types/error_codes/result.hpp"
#include "types/messages/base_header.hpp"
#include "utils/byte_operations.h"

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Structure representing a node in the mesh network
 * 
 * Contains information about a node that is part of the mesh network.
 */
struct NetworkNode {
    AddressType address = 0;    ///< Node address
    uint8_t battery_level = 0;  ///< Battery level (0-100%)
    uint32_t last_seen = 0;     ///< Timestamp when last heard from
    bool is_network_manager =
        false;                 ///< Whether this node is a network manager
    uint8_t capabilities = 0;  ///< Node capabilities bitmap
    uint8_t allocated_slots =
        0;  ///< Number of data slots allocated to this node

    /**
     * @brief Default constructor
     */
    NetworkNode() = default;

    /**
     * @brief Constructor with all fields
     * 
     * @param addr Node address
     * @param battery Battery level (0-100%)
     * @param last_seen_time Timestamp when last seen
     * @param is_manager Whether this is a network manager
     * @param caps Node capabilities bitmap
     * @param slots Number of allocated slots
     */
    NetworkNode(AddressType addr, uint8_t battery, uint32_t last_seen_time,
                bool is_manager = false, uint8_t caps = 0, uint8_t slots = 0)
        : address(addr),
          battery_level(battery),
          last_seen(last_seen_time),
          is_network_manager(is_manager),
          capabilities(caps),
          allocated_slots(slots) {}

    /**
     * @brief Check if the node is expired based on a timeout
     * 
     * @param current_time Current timestamp
     * @param timeout_ms Timeout in milliseconds
     * @return bool True if the node is expired
     */
    bool IsExpired(uint32_t current_time, uint32_t timeout_ms) const;

    /**
     * @brief Update the last seen timestamp
     * 
     * @param current_time Current timestamp
     */
    void UpdateLastSeen(uint32_t current_time);

    /**
     * @brief Update battery level
     * 
     * @param new_battery_level New battery level (0-100%)
     * @param current_time Current timestamp
     * @return bool True if battery level was actually updated
     */
    bool UpdateBatteryLevel(uint8_t new_battery_level, uint32_t current_time);

    /**
     * @brief Update capabilities
     * 
     * @param new_capabilities New capabilities bitmap
     * @param current_time Current timestamp
     */
    void UpdateCapabilities(uint8_t new_capabilities, uint32_t current_time);

    /**
     * @brief Update allocated slots
     * 
     * @param new_slots New number of allocated slots
     * @param current_time Current timestamp
     */
    void UpdateAllocatedSlots(uint8_t new_slots, uint32_t current_time);

    /**
     * @brief Check if node has a specific capability
     * 
     * @param capability The capability to check (bit flag)
     * @return bool True if the node has the capability
     */
    bool HasCapability(uint8_t capability) const;

    /**
     * @brief Get a string representation of the node capabilities
     * 
     * @return std::string Human-readable capabilities string
     */
    std::string GetCapabilitiesString() const;

    /**
     * @brief Serialize the network node
     * 
     * @param serializer The serializer to write to
     * @return Result Success if serialization succeeded
     */
    Result Serialize(utils::ByteSerializer& serializer) const;

    /**
     * @brief Deserialize a network node
     * 
     * @param deserializer The deserializer to read from
     * @return std::optional<NetworkNode> The network node if successful
     */
    static std::optional<NetworkNode> Deserialize(
        utils::ByteDeserializer& deserializer);

    /**
     * @brief Size of a network node when serialized
     * 
     * @return size_t Size in bytes
     */
    static constexpr size_t SerializedSize() {
        return sizeof(AddressType) +  // Address
               sizeof(uint8_t) +      // Battery level
               sizeof(uint32_t) +     // Last seen
               sizeof(uint8_t) +      // Is network manager (as uint8_t)
               sizeof(uint8_t) +      // Capabilities
               sizeof(uint8_t);       // Allocated slots
    }

    /**
     * @brief Equality operator
     * 
     * @param other The other network node to compare
     * @return bool True if nodes are equal (based on address)
     */
    bool operator==(const NetworkNode& other) const;

    /**
     * @brief Inequality operator
     * 
     * @param other The other network node to compare
     * @return bool True if nodes are not equal
     */
    bool operator!=(const NetworkNode& other) const;

    /**
     * @brief Less than operator for sorting (based on address)
     * 
     * @param other The other network node to compare
     * @return bool True if this node's address is less than the other's
     */
    bool operator<(const NetworkNode& other) const;
};

/**
 * @brief Helper functions for network node operations
 */
namespace node_utils {

/**
 * @brief Convert capabilities bitmap to string representation
 * 
 * @param capabilities The capabilities bitmap
 * @return std::string Human-readable capabilities string
 */
std::string CapabilitiesToString(uint8_t capabilities);

/**
 * @brief Find a node in a vector by address
 * 
 * @param nodes Vector of network nodes
 * @param address Address to search for
 * @return std::vector<NetworkNode>::iterator Iterator to the node, or end() if not found
 */
std::vector<NetworkNode>::iterator FindNodeByAddress(
    std::vector<NetworkNode>& nodes, AddressType address);

/**
 * @brief Find a node in a vector by address (const version)
 * 
 * @param nodes Vector of network nodes
 * @param address Address to search for
 * @return std::vector<NetworkNode>::const_iterator Iterator to the node, or end() if not found
 */
std::vector<NetworkNode>::const_iterator FindNodeByAddress(
    const std::vector<NetworkNode>& nodes, AddressType address);

/**
 * @brief Remove expired nodes from a vector
 * 
 * @param nodes Vector of network nodes
 * @param current_time Current timestamp
 * @param timeout_ms Timeout in milliseconds
 * @return size_t Number of nodes removed
 */
size_t RemoveExpiredNodes(std::vector<NetworkNode>& nodes,
                          uint32_t current_time, uint32_t timeout_ms);

}  // namespace node_utils

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher