/**
 * @file routing_table_header.hpp
 * @brief Header definition for routing table messages
 */

#pragma once

#include "types/messages/base_header.hpp"

namespace loramesher {

/**
  * @brief Header for routing table messages
  * 
  * Extends BaseHeader with routing table specific fields: network ID,
  * table version, and entry count.
  */
class RoutingTableHeader : public BaseHeader {
   public:
    /**
      * @brief Default constructor
      */
    RoutingTableHeader() = default;

    /**
      * @brief Constructor with all fields
      * 
      * @param dest Destination address
      * @param src Source address
      * @param network_manager_addr Network identifier
      * @param table_version Version of the routing table (increments with changes)
      * @param entry_count Number of routing entries in the message
      */
    RoutingTableHeader(AddressType dest, AddressType src,
                       AddressType network_manager_addr, uint8_t table_version,
                       uint8_t entry_count);

    /**
     * @brief Get the network manager address
     * 
     * @return AddressType The network manager address
     */
    AddressType GetNetworkManager() const { return network_manager_addr_; }

    /**
      * @brief Gets the table version
      * 
      * @return uint8_t The table version
      */
    uint8_t GetTableVersion() const { return table_version_; }

    /**
      * @brief Gets the entry count
      * 
      * @return uint8_t Number of routing table entries
      */
    uint8_t GetEntryCount() const { return entry_count_; }

    /**
      * @brief Sets the routing table specific information
      * 
      * @param network_manager_addr Network manager address
      * @param table_version Version of the routing table
      * @param entry_count Number of routing entries
      * @return Result Success if setting succeeded, error code otherwise
      */
    Result SetRoutingTableInfo(AddressType network_manager_addr,
                               uint8_t table_version, uint8_t entry_count);

    /**
      * @brief Serializes the header to a byte serializer
      * 
      * Extends the base header serialization with routing table specific fields.
      * 
      * @param serializer Serializer to write the header to
      * @return Result Success if serialization succeeded, error code otherwise
      */
    Result Serialize(utils::ByteSerializer& serializer) const override;

    /**
      * @brief Deserializes a routing table header from a byte deserializer
      * 
      * @param deserializer Deserializer containing the header data
      * @return std::optional<RoutingTableHeader> Deserialized header if successful,
      *         std::nullopt otherwise
      */
    static std::optional<RoutingTableHeader> Deserialize(
        utils::ByteDeserializer& deserializer);

    /**
      * @brief Calculates the size of the routing table specific header extension
      * 
      * @return size_t Size of the routing table header fields in bytes
      */
    static constexpr size_t RoutingTableFieldsSize() {
        return sizeof(AddressType) +  // Network manager address
               sizeof(uint8_t) +      // Table version
               sizeof(uint8_t);       // Entry count
    }

    /**
      * @brief Gets the total size of this header type
      * 
      * @return size_t Size of the header in bytes (base + routing table specific fields)
      */
    size_t GetSize() const override {
        return BaseHeader::Size() + RoutingTableFieldsSize();
    }

   private:
    AddressType network_manager_addr_ = 0;  ///< Network manager address
    uint8_t table_version_ = 0;             ///< Version of the routing table
    uint8_t entry_count_ = 0;               ///< Number of routing entries
};

}  // namespace loramesher