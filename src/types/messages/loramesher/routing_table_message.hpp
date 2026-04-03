/**
 * @file routing_table_message.hpp
 * @brief Definition of routing table message for mesh networking
 */

#pragma once

#include <array>
#include <vector>
#include "routing_table_entry.hpp"
#include "routing_table_header.hpp"
#include "types/messages/base_message.hpp"
#include "utils/byte_operations.h"
#include "utils/compat/span.hpp"
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
     * @brief Creates a RoutingTableMessage from a received BaseMessage
     *
     * Reads directly from the BaseMessage payload without an intermediate
     * Serialize/CreateFromSerialized round-trip.  Returns std::nullopt on
     * type mismatch, truncated payload, or any deserialization error.
     *
     * @param message The received base message
     * @return std::optional<RoutingTableMessage> Deserialized message if successful,
     *         std::nullopt otherwise
     */
    static std::optional<RoutingTableMessage> CreateFromBaseMessage(
        const BaseMessage& message);

    /**
     * @brief Creates a new routing table message
     *
     * @param dest Destination address for the message
     * @param src Source address of the message
     * @param network_manager_addr Network manager address
     * @param table_version Version of the routing table
     * @param entries Vector of network node routes
     * @param source_capabilities Source node capabilities bitmap
     * @param source_allocated_data_slots Source node's allocated data slots
     * @return std::optional<RoutingTableMessage> Valid message if creation succeeded,
     *         std::nullopt otherwise
     */
    static std::optional<RoutingTableMessage> Create(
        AddressType dest, AddressType src, AddressType network_manager_addr,
        uint8_t table_version, const std::vector<RoutingTableEntry>& entries,
        uint8_t source_capabilities = 0,
        uint8_t source_allocated_data_slots = 0);

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
     * @brief Maximum number of routing table entries that fit in one message
     *
     * (255 - BaseHeader::Size(6) - RoutingTableFieldsSize(6)) / RoutingTableEntry::Size(10) = 24
     */
    // TODO: Make it programatically
    static constexpr uint8_t kMaxRoutingEntries = 24;

    /**
     * @brief Gets the list of routing table entries as a span
     *
     * @return Span over the routing table entries (valid for message lifetime)
     */
    std::span<const RoutingTableEntry> GetEntries() const;

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
     * @brief Gets the source node capabilities
     *
     * @return uint8_t Capabilities bitmap of the source node
     */
    uint8_t GetSourceCapabilities() const;

    /**
     * @brief Gets the source node's allocated data slots
     *
     * @return uint8_t Number of allocated data slots for the source node
     */
    uint8_t GetSourceAllocatedDataSlots() const;

    /**
     * @brief Gets the total size of the payload message
     * 
     * @return size_t Total payload size in bytes
     */
    size_t GetTotalPayloadSize() const;

    /**
     * @brief Gets the sender's raw EWMA reception quality for a direct neighbor
     *
     * Returns the reception_quality field only for entries with hop_count == 1.
     * Multi-hop entries are ignored to enable unidirectional link detection.
     *
     * @param node_address Node address to check
     * @return uint8_t Reception quality (0-255) for hop_count==1 entry, or 0 if not found
     */
    uint8_t GetReceptionQualityFor(AddressType node_address) const;

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
     * @param entries The network node routes as a span
     */
    RoutingTableMessage(const RoutingTableHeader& header,
                        std::span<const RoutingTableEntry> entries);

    RoutingTableHeader header_;  ///< Routing table message header
    std::array<RoutingTableEntry, kMaxRoutingEntries>
        entries_{};            ///< Network node routes
    uint8_t entry_count_ = 0;  ///< Number of valid entries
};

}  // namespace loramesher