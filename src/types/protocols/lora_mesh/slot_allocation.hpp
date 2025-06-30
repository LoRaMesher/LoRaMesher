/**
 * @file slot_allocation.hpp
 * @brief Definition of slot allocation for TDMA scheduling in LoRaMesh protocol
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "types/error_codes/result.hpp"
#include "types/messages/base_message.hpp"
#include "utils/byte_operations.h"

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {

/**
  * @brief Structure representing a slot allocation in the TDMA schedule
  * 
  * Defines what type of operation should be performed in a specific time slot.
  */
struct SlotAllocation {
    /**
     * @brief Types of slots in the TDMA schedule
     */
    enum class SlotType : uint8_t {
        TX = 0x01,            ///< Transmission slot
        RX = 0x02,            ///< Reception slot
        SLEEP = 0x03,         ///< Sleep slot (radio off)
        DISCOVERY_RX = 0x04,  ///< Discovery reception slot
        DISCOVERY_TX = 0x05,  ///< Discovery transmission slot
        CONTROL_RX = 0x06,    ///< Control message reception slot
        CONTROL_TX = 0x07,    ///< Control message transmission slot
    };

    uint16_t slot_number = 0;         ///< Slot number in the superframe
    SlotType type = SlotType::SLEEP;  ///< Type of slot
    AddressType target_address =
        0;  ///< Target address for TX slots (0 for broadcast/RX slots)

    /**
     * @brief Default constructor
     */
    SlotAllocation() = default;

    /**
     * @brief Constructor with slot number and type
     * 
     * @param slot_num Slot number in the superframe
     * @param slot_type Type of slot
     * @param target Target address (default 0 for broadcast/RX)
     */
    SlotAllocation(uint16_t slot_num, SlotType slot_type,
                   AddressType target = 0)
        : slot_number(slot_num), type(slot_type), target_address(target) {}

    /**
     * @brief Check if this is a transmission slot
     * 
     * @return bool True if this is any type of TX slot
     */
    bool IsTxSlot() const;

    /**
     * @brief Check if this is a reception slot
     * 
     * @return bool True if this is any type of RX slot
     */
    bool IsRxSlot() const;

    /**
     * @brief Check if this is a control slot
     * 
     * @return bool True if this is a control RX or TX slot
     */
    bool IsControlSlot() const;

    /**
     * @brief Check if this is a discovery slot
     * 
     * @return bool True if this is a discovery RX or TX slot
     */
    bool IsDiscoverySlot() const;

    /**
     * @brief Get the string representation of the slot type
     * 
     * @return std::string String representation of slot type
     */
    std::string GetTypeString() const;

    /**
     * @brief Serialize the slot allocation
     * 
     * @param serializer The serializer to write to
     * @return Result Success if serialization succeeded
     */
    Result Serialize(utils::ByteSerializer& serializer) const;

    /**
     * @brief Deserialize a slot allocation
     * 
     * @param deserializer The deserializer to read from
     * @return std::optional<SlotAllocation> The slot allocation if successful
     */
    static std::optional<SlotAllocation> Deserialize(
        utils::ByteDeserializer& deserializer);

    /**
     * @brief Size of a slot allocation when serialized
     * 
     * @return size_t Size in bytes
     */
    static constexpr size_t SerializedSize() {
        return sizeof(uint16_t) +    // Slot number
               sizeof(uint8_t) +     // Slot type
               sizeof(AddressType);  // Target address
    }

    /**
     * @brief Equality operator
     * 
     * @param other The other slot allocation to compare
     * @return bool True if slot allocations are equal
     */
    bool operator==(const SlotAllocation& other) const;

    /**
     * @brief Inequality operator
     * 
     * @param other The other slot allocation to compare
     * @return bool True if slot allocations are not equal
     */
    bool operator!=(const SlotAllocation& other) const;

    /**
     * @brief Less than operator for sorting
     * 
     * @param other The other slot allocation to compare
     * @return bool True if this slot comes before the other
     */
    bool operator<(const SlotAllocation& other) const;
};

/**
  * @brief Helper functions for slot type operations
  */
namespace slot_utils {

/**
  * @brief Convert SlotType enum to string
  * 
  * @param type The slot type to convert
  * @return std::string String representation
  */
std::string SlotTypeToString(SlotAllocation::SlotType type);

/**
  * @brief Convert string to SlotType enum
  * 
  * @param type_str String representation of slot type
  * @return std::optional<SlotAllocation::SlotType> The slot type if valid
  */
std::optional<SlotAllocation::SlotType> StringToSlotType(
    const std::string& type_str);

/**
  * @brief Check if a slot type is valid
  * 
  * @param type The slot type to validate
  * @return bool True if the slot type is valid
  */
bool IsValidSlotType(SlotAllocation::SlotType type);

}  // namespace slot_utils

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher