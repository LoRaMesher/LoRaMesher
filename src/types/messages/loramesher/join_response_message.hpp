/**
 * @file join_response_message.hpp
 * @brief Definition of join response message for mesh networking
 */

#pragma once

#include "join_response_header.hpp"
#include "types/messages/base_message.hpp"
#include "utils/byte_operations.h"
#include "utils/logger.hpp"

namespace loramesher {

/**
  * @brief Message for responding to network join requests
  * 
  * Implements the IConvertibleToBaseMessage interface to provide conversion
  * to the BaseMessage format for transmission. Uses JoinResponseHeader for
  * header information.
  */
class JoinResponseMessage : public IConvertibleToBaseMessage {
   public:
    /**
      * @brief Creates a new join response message
      *
      * @param dest Destination address (requesting node)
      * @param src Source address (network manager)
      * @param network_id Network identifier
      * @param allocated_slots Number of data slots allocated
      * @param status Response status code
      * @param superframe_info Optional superframe configuration information
      * @param next_hop Next hop for message forwarding (0 for direct)
      * @param sponsor_address Sponsor node address (0 for no sponsor)
      * @return std::optional<JoinResponseMessage> Valid message if creation succeeded,
      *         std::nullopt otherwise
      */
    static std::optional<JoinResponseMessage> Create(
        AddressType dest, AddressType src, uint16_t network_id,
        uint8_t allocated_slots, JoinResponseHeader::ResponseStatus status,
        const std::vector<uint8_t>& superframe_info = {},
        AddressType next_hop = 0, AddressType target_address = 0);

    /**
      * @brief Creates a join response message from serialized data
      * 
      * @param data Serialized message data
      * @return std::optional<JoinResponseMessage> Deserialized message if successful,
      *         std::nullopt otherwise
      */
    static std::optional<JoinResponseMessage> CreateFromSerialized(
        const std::vector<uint8_t>& data);

    /**
      * @brief Gets the network ID
      * 
      * @return uint16_t Network identifier
      */
    uint16_t GetNetworkId() const;

    /**
      * @brief Gets the allocated data slots
      * 
      * @return uint8_t Number of allocated data slots
      */
    uint8_t GetAllocatedSlots() const;

    /**
      * @brief Gets the response status
      * 
      * @return JoinResponseHeader::ResponseStatus Response status code
      */
    JoinResponseHeader::ResponseStatus GetStatus() const;

    /**
      * @brief Gets the superframe information
      * 
      * @return const std::vector<uint8_t>& Superframe configuration information
      */
    const std::vector<uint8_t>& GetSuperframeInfo() const;

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
      * @brief Gets the join response header
      * 
      * @return const JoinResponseHeader& The join response header
      */
    const JoinResponseHeader& GetHeader() const;

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
      * @param header The join response header
      * @param superframe_info Optional superframe configuration information
      */
    JoinResponseMessage(const JoinResponseHeader& header,
                        const std::vector<uint8_t>& superframe_info = {});

    JoinResponseHeader header_;  ///< Join response message header
    std::vector<uint8_t>
        superframe_info_;  ///< Superframe configuration (optional)
};

}  // namespace loramesher