/**
 * @file join_request_header.hpp
 * @brief Header definition for network join request messages
 */

#pragma once

#include "types/messages/base_header.hpp"

namespace loramesher {

/**
  * @brief Header for JOIN_REQUEST messages
  * 
  * Extends BaseHeader with join request specific fields: node capabilities,
  * battery level, and requested data slots.
  */
class JoinRequestHeader : public BaseHeader {
   public:
    /**
      * @brief Bit flags for node capabilities
      */
    enum NodeCapabilities : uint8_t {
        NONE = 0x00,              ///< No special capabilities
        ROUTER = 0x01,            ///< Node can route messages
        GATEWAY = 0x02,           ///< Node has internet connectivity
        BATTERY_POWERED = 0x04,   ///< Node runs on battery
        HIGH_BANDWIDTH = 0x08,    ///< Node supports high bandwidth
        TIME_SYNC_SOURCE = 0x10,  ///< Node can provide time synchronization
        SENSOR_NODE = 0x20,       ///< Node has sensors
        RESERVED = 0x40,          ///< Reserved for future use
        EXTENDED_CAPS = 0x80      ///< Has extended capabilities
    };

    /**
      * @brief Default constructor
      */
    JoinRequestHeader() = default;

    /**
      * @brief Constructor with all fields
      *
      * @param dest Destination address (typically broadcast or network manager)
      * @param src Source address
      * @param capabilities Node capabilities bitmap
      * @param battery_level Battery level (0-100%)
      * @param requested_slots Number of data slots requested
      * @param next_hop Next hop for message forwarding (0 for direct)
      * @param additional_info_size To store the payload size in the base message
      * @param sponsor_address Sponsor node address (0 for no sponsor)
      */
    JoinRequestHeader(AddressType dest, AddressType src, uint8_t capabilities,
                      uint8_t battery_level, uint8_t requested_slots,
                      AddressType next_hop = 0,
                      uint8_t additional_info_size = 0,
                      AddressType sponsor_address = 0);

    /**
      * @brief Gets the node capabilities
      * 
      * @return uint8_t The capabilities bitmap
      */
    uint8_t GetCapabilities() const { return capabilities_; }

    /**
      * @brief Gets the battery level
      * 
      * @return uint8_t The battery level (0-100%)
      */
    uint8_t GetBatteryLevel() const { return battery_level_; }

    /**
      * @brief Gets the requested data slots
      * 
      * @return uint8_t Requested number of data slots
      */
    uint8_t GetRequestedSlots() const { return requested_slots_; }

    /**
      * @brief Gets the next hop for message forwarding
      *
      * @return AddressType Next hop address (0 for direct routing)
      */
    AddressType GetNextHop() const { return next_hop_; }

    /**
      * @brief Gets the sponsor address
      *
      * @return AddressType Sponsor address (0 for no sponsor)
      */
    AddressType GetSponsorAddress() const { return sponsor_address_; }

    /**
      * @brief Sets the join request specific information
      * 
      * @param capabilities Node capabilities bitmap
      * @param battery_level Battery level (0-100%)
      * @param requested_slots Number of data slots requested
      * @return Result Success if setting succeeded, error code otherwise
      */
    Result SetJoinRequestInfo(uint8_t capabilities, uint8_t battery_level,
                              uint8_t requested_slots);

    /**
     * @brief Sets the requested data slots
     *
     * @param requested_slots Number of data slots requested
     * @return Result Success if setting succeeded, error code otherwise
     */
    Result SetRequestedSlots(uint8_t requested_slots);

    /**
     * @brief Sets the sponsor address
     *
     * @param sponsor_address Sponsor node address (0 for no sponsor)
     * @return Result Success if setting succeeded, error code otherwise
     */
    Result SetSponsorAddress(AddressType sponsor_address);

    /**
      * @brief Serializes the header to a byte serializer
      * 
      * Extends the base header serialization with join request specific fields.
      * 
      * @param serializer Serializer to write the header to
      * @return Result Success if serialization succeeded, error code otherwise
      */
    Result Serialize(utils::ByteSerializer& serializer) const override;

    /**
      * @brief Deserializes a join request header from a byte deserializer
      * 
      * @param deserializer Deserializer containing the header data
      * @return std::optional<JoinRequestHeader> Deserialized header if successful,
      *         std::nullopt otherwise
      */
    static std::optional<JoinRequestHeader> Deserialize(
        utils::ByteDeserializer& deserializer);

    /**
      * @brief Calculates the size of the join request specific header extension
      *
      * @return size_t Size of the join request header fields in bytes
      */
    static constexpr size_t JoinRequestFieldsSize() {
        return sizeof(uint8_t) +      // Capabilities
               sizeof(uint8_t) +      // Battery level
               sizeof(uint8_t) +      // Requested slots
               sizeof(AddressType) +  // Next hop
               sizeof(AddressType);   // Sponsor address
    }

    /**
      * @brief Gets the total size of this header type
      * 
      * @return size_t Size of the header in bytes (base + join request specific fields)
      */
    size_t GetSize() const override {
        return BaseHeader::Size() + JoinRequestFieldsSize();
    }

   private:
    uint8_t capabilities_ = 0;     ///< Node capabilities bitmap
    uint8_t battery_level_ = 100;  ///< Battery level (0-100%)
    uint8_t requested_slots_ = 1;  ///< Requested number of data slots
    AddressType next_hop_ = 0;     ///< Next hop for message forwarding
    AddressType sponsor_address_ =
        0;  ///< Sponsor node address (0 = no sponsor)
};

}  // namespace loramesher