/**
 * @file join_request_message.hpp
 * @brief Definition of join request message for mesh networking
 */

#pragma once

#include "join_request_header.hpp"
#include "types/messages/base_message.hpp"
#include "utils/byte_operations.h"
#include "utils/logger.hpp"

namespace loramesher {

/**
  * @brief Message for requesting to join a mesh network
  * 
  * Implements the IConvertibleToBaseMessage interface to provide conversion
  * to the BaseMessage format for transmission. Uses JoinRequestHeader for
  * header information.
  */
class JoinRequestMessage : public IConvertibleToBaseMessage {
   public:
    /**
      * @brief Creates a new join request message
      * 
      * @param dest Destination address (network manager or broadcast)
      * @param src Source address of the message
      * @param capabilities Node capabilities bitmap
      * @param battery_level Battery level (0-100%)
      * @param requested_slots Number of data slots requested
      * @param additional_info Optional additional information to include
      * @param next_hop Next hop for message forwarding (0 for direct)
      * @return std::optional<JoinRequestMessage> Valid message if creation succeeded,
      *         std::nullopt otherwise
      */
    static std::optional<JoinRequestMessage> Create(
        AddressType dest, AddressType src, uint8_t capabilities,
        uint8_t battery_level, uint8_t requested_slots,
        const std::vector<uint8_t>& additional_info = {},
        AddressType next_hop = 0);

    /**
      * @brief Creates a join request message from serialized data
      * 
      * @param data Serialized message data
      * @return std::optional<JoinRequestMessage> Deserialized message if successful,
      *         std::nullopt otherwise
      */
    static std::optional<JoinRequestMessage> CreateFromSerialized(
        const std::vector<uint8_t>& data);

    /**
      * @brief Gets the capabilities bitmap
      * 
      * @return uint8_t Node capabilities bitmap
      */
    uint8_t GetCapabilities() const;

    /**
      * @brief Gets the battery level
      * 
      * @return uint8_t Battery level (0-100%)
      */
    uint8_t GetBatteryLevel() const;

    /**
      * @brief Gets the requested data slots
      * 
      * @return uint8_t Number of data slots requested
      */
    uint8_t GetRequestedSlots() const;

    /**
      * @brief Gets any additional information included in the message
      * 
      * @return const std::vector<uint8_t>& Additional information
      */
    const std::vector<uint8_t>& GetAdditionalInfo() const;

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
      * @brief Gets the join request header
      * 
      * @return const JoinRequestHeader& The join request header
      */
    const JoinRequestHeader& GetHeader() const;

    /**
      * @brief Gets the total size of the serialized message
      * 
      * @return size_t Total size in bytes
      */
    size_t GetTotalSize() const;

    /**
     * @brief Sets the requested data slots
     * 
     * @param requested_slots Number of data slots requested
     * @return Result Status of the operation
     */
    Result SetRequestedSlots(uint8_t requested_slots);

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
      * @param header The join request header
      * @param additional_info Any additional information to include
      */
    JoinRequestMessage(const JoinRequestHeader& header,
                       const std::vector<uint8_t>& additional_info = {});

    JoinRequestHeader header_;  ///< Join request message header
    std::vector<uint8_t>
        additional_info_;  ///< Additional information (optional)
};

}  // namespace loramesher