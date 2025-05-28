/**
 * @file routing_table_message.hpp
 * @brief Definition of routing table message for mesh networking
 */

#pragma once

#include <vector>
#include "routing_table_header.hpp"
#include "types/messages/base_message.hpp"
#include "types/protocols/lora_mesh/network_node_route.hpp"
#include "utils/byte_operations.h"
#include "utils/logger.hpp"

namespace loramesher {

/**
   * @brief Message for exchanging routing table information
   * 
   * Implements the IConvertibleToBaseMessage interface to provide conversion
   * to the BaseMessage format for transmission. Uses RoutingTableHeader for
   * header information and contains a list of network node routes.
   */
class RoutingTableMessage : public IConvertibleToBaseMessage {
   public:
    /**
     * @brief Creates a new routing table message
     * 
     * @param dest Destination address for the message
     * @param src Source address of the message
     * @param network_manager_addr Network manager address
     * @param table_version Version of the routing table
     * @param entries Vector of network node routes
     * @return std::optional<RoutingTableMessage> Valid message if creation succeeded,
     *         std::nullopt otherwise
     */
    static std::optional<RoutingTableMessage> Create(
        AddressType dest, AddressType src, AddressType network_manager_addr,
        uint8_t table_version,
        const std::vector<types::protocols::lora_mesh::NetworkNodeRoute>&
            entries);

    /**
     * @brief Creates a routing table message from serialized data
     * 
     * @param data Serialized message data
     * @return std::optional<RoutingTableMessage> Deserialized message if successful,
     *         std::nullopt otherwise
     */
    static std::optional<RoutingTableMessage> CreateFromSerialized(
        const std::vector<uint8_t>& data);

    /**
     * @brief Gets the network manager address
     * 
     * @return AddressType The network manager address
     */
    AddressType GetNetworkManager() const;

    /**
     * @brief Gets the table version
     * 
     * @return uint8_t The table version
     */
    uint8_t GetTableVersion() const;

    /**
     * @brief Gets the list of network node routes
     * 
     * @return const std::vector<NetworkNodeRoute>& The network node routes
     */
    const std::vector<types::protocols::lora_mesh::NetworkNodeRoute>&
    GetEntries() const;

    /**
     * @brief Gets the source address
     * 
     * @return AddressType The source address
     */
    AddressType GetSource() const;

    /**
     * @brief Gets the destination address
     * 
     * @return AddressType The destination address
     */
    AddressType GetDestination() const;

    /**
     * @brief Gets the routing table header
     * 
     * @return const RoutingTableHeader& The routing table header
     */
    const RoutingTableHeader& GetHeader() const;

    /**
     * @brief Gets the total size of the serialized message
     * 
     * @return size_t Total size in bytes
     */
    size_t GetTotalSize() const;

    /**
     * @brief Gets link quality for a specific node
     * 
     * @param node_address Node address to check
     * @return uint8_t Link quality (0-255) or 0 if not found
     */
    uint8_t GetLinkQualityFor(AddressType node_address) const;

    /**
     * @brief Converts to a BaseMessage for transmission
     * 
     * @return BaseMessage The converted base message
     */
    BaseMessage ToBaseMessage() const override;

    /**
     * @brief Serializes the complete message
     * 
     * @return std::optional<std::vector<uint8_t>> Serialized message if successful,
     *         std::nullopt otherwise
     */
    std::optional<std::vector<uint8_t>> Serialize() const override;

   private:
    /**
     * @brief Private constructor
     * 
     * @param header The routing table header
     * @param network_manager_addr Network manager address
     * @param entries The network node routes
     */
    RoutingTableMessage(
        const RoutingTableHeader& header,
        const std::vector<types::protocols::lora_mesh::NetworkNodeRoute>&
            entries);

    RoutingTableHeader header_;  ///< Routing table message header
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute>
        entries_;  ///< Network node routes
};

}  // namespace loramesher