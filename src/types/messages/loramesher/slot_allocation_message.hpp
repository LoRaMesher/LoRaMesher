/**
 * @file slot_allocation_message.hpp
 * @brief Definition of slot allocation message for mesh networking
 */

#pragma once

#include "types/messages/base_message.hpp"
#include "utils/byte_operations.h"
#include "utils/logger.hpp"

namespace loramesher {

/**
  * @brief Message for allocating data slots in the mesh network
  * 
  * Implements the IConvertibleToBaseMessage interface to provide conversion
  * to the BaseMessage format for transmission.
  */
class SlotAllocationMessage : public IConvertibleToBaseMessage {
   public:
    /**
      * @brief Creates a new slot allocation message
      * 
      * @param dest Destination address of the node
      * @param src Source address (network manager)
      * @param network_id Network identifier
      * @param allocated_slots Number of data slots allocated to the node
      * @param total_nodes Total number of nodes in the network
      * @return std::optional<SlotAllocationMessage> Valid message if creation succeeded,
      *         std::nullopt otherwise
      */
    static std::optional<SlotAllocationMessage> Create(AddressType dest,
                                                       AddressType src,
                                                       uint16_t network_id,
                                                       uint8_t allocated_slots,
                                                       uint8_t total_nodes);

    /**
      * @brief Creates a slot allocation message from serialized data
      * 
      * @param data Serialized message data
      * @return std::optional<SlotAllocationMessage> Deserialized message if successful,
      *         std::nullopt otherwise
      */
    static std::optional<SlotAllocationMessage> CreateFromSerialized(
        const std::vector<uint8_t>& data);

    /**
      * @brief Gets the network ID
      * 
      * @return uint16_t Network identifier
      */
    uint16_t GetNetworkId() const { return network_id_; }

    /**
      * @brief Gets the number of allocated data slots
      * 
      * @return uint8_t Number of data slots allocated
      */
    uint8_t GetAllocatedSlots() const { return allocated_slots_; }

    /**
      * @brief Gets the total number of nodes in the network
      * 
      * @return uint8_t Total number of nodes
      */
    uint8_t GetTotalNodes() const { return total_nodes_; }

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
      * @param network_id Network identifier
      * @param allocated_slots Number of data slots allocated
      * @param total_nodes Total number of nodes in the network
      */
    SlotAllocationMessage(AddressType dest, AddressType src,
                          uint16_t network_id, uint8_t allocated_slots,
                          uint8_t total_nodes);

    AddressType destination_;  ///< Destination address
    AddressType source_;       ///< Source address
    uint16_t network_id_;      ///< Network identifier
    uint8_t allocated_slots_;  ///< Number of data slots allocated
    uint8_t total_nodes_;      ///< Total number of nodes in the network
};

}  // namespace loramesher