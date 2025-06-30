/**
 * @file slot_request_message.hpp
 * @brief Definition of slot request message for mesh networking
 */

#pragma once

#include "types/messages/base_message.hpp"
#include "utils/byte_operations.h"
#include "utils/logger.hpp"

namespace loramesher {

/**
  * @brief Message for requesting data slots in the mesh network
  * 
  * Implements the IConvertibleToBaseMessage interface to provide conversion
  * to the BaseMessage format for transmission.
  */
class SlotRequestMessage : public IConvertibleToBaseMessage {
   public:
    /**
      * @brief Creates a new slot request message
      * 
      * @param dest Destination address (network manager)
      * @param src Source address of the requesting node
      * @param requested_slots Number of data slots requested
      * @return std::optional<SlotRequestMessage> Valid message if creation succeeded,
      *         std::nullopt otherwise
      */
    static std::optional<SlotRequestMessage> Create(AddressType dest,
                                                    AddressType src,
                                                    uint8_t requested_slots);

    /**
      * @brief Creates a slot request message from serialized data
      * 
      * @param data Serialized message data
      * @return std::optional<SlotRequestMessage> Deserialized message if successful,
      *         std::nullopt otherwise
      */
    static std::optional<SlotRequestMessage> CreateFromSerialized(
        const std::vector<uint8_t>& data);

    /**
      * @brief Gets the number of requested data slots
      * 
      * @return uint8_t Number of data slots requested
      */
    uint8_t GetRequestedSlots() const { return requested_slots_; }

    /**
      * @brief Gets the source address
      * 
      * @return AddressType The source address
      */
    AddressType GetSource() const { return source_; }

    /**
      * @brief Gets the destination address
      * 
      * @return AddressType The destination address
      */
    AddressType GetDestination() const { return destination_; }

    /**
      * @brief Gets the total size of the serialized message
      * 
      * @return size_t Total size in bytes
      */
    size_t GetTotalSize() const;

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
      * @param dest Destination address
      * @param src Source address
      * @param requested_slots Number of data slots requested
      */
    SlotRequestMessage(AddressType dest, AddressType src,
                       uint8_t requested_slots);

    AddressType destination_;  ///< Destination address
    AddressType source_;       ///< Source address
    uint8_t requested_slots_;  ///< Number of data slots requested
};

}  // namespace loramesher