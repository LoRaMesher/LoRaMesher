/**
 * @file message_type.hpp
 * @brief Optimized message type definitions for LoRa communication
 */

#pragma once

#include <cstdint>

namespace loramesher {

/**
 * @brief Optimized message type enumeration using bit-fields
 * 
 * The message type is organized as follows:
 * - Bits 7-4 (high nibble): Main message category
 * - Bits 3-0 (low nibble): Subtype within the category
 * 
 * This allows for 16 main categories with 16 subtypes each, all within a single byte.
 * 
 * @attention When adding more types, update the IsValidMessageType function
 * in base_header.cpp accordingly.
 */
enum class MessageType : uint8_t {
    ANY = 0x00,  ///< 0000 0000: Any message type (not used in practice)

    // Main categories (high nibble)
    DATA_MSG = 0x10,     ///< 0001 xxxx: Data message category
    CONTROL_MSG = 0x20,  ///< 0010 xxxx: Control message category
    ROUTING_MSG = 0x30,  ///< 0011 xxxx: Routing message category
    SYSTEM_MSG = 0x40,   ///< 0100 xxxx: System message category

    // Predefined complete message types

    // Data messages (0x1x)
    DATA = 0x11,  ///< 0001 0001: Regular data message
    // XL_DATA = 0x12,  ///< 0001 0010: Large data message

    // Control messages (0x2x)
    ACK = 0x21,  ///< 0010 0001: Acknowledgment
    // NEED_ACK = 0x22,  ///< 0010 0010: Request for acknowledgment
    PING = 0x23,  ///< 0010 0011: Ping request
    PONG = 0x24,  ///< 0010 0100: Pong response

    // Routing messages (0x3x)
    HELLO = 0x31,        ///< 0011 0001: Hello packet for routing
    ROUTE_TABLE = 0x32,  ///< 0011 0010: Routing table update

    // System messages (0x4x)
    SYNC = 0x41,             ///< 0100 0001: Synchronization packet
    JOIN_REQUEST = 0x42,     ///< 0100 0010: Request to join network
    JOIN_RESPONSE = 0x43,    ///< 0100 0011: Response to join request
    SLOT_REQUEST = 0x44,     ///< 0100 0100: Request for slot allocation
    SLOT_ALLOCATION = 0x45,  ///< 0100 0101: Slot allocation response
    SYNC_BEACON = 0x46,      ///< 0100 0110: Multi-hop sync beacon
};

/**
 * @brief Helper functions for working with the bit-field message types
 */
namespace message_type {
/**
 * @brief Get the main type (high 4 bits)
 * 
 * @param type The message type to extract from
 * @return MessageType The main type category (0x10, 0x20, etc.)
 */
inline MessageType GetMainType(MessageType type) {
    return static_cast<MessageType>(static_cast<uint8_t>(type) & 0xF0);
}

/**
 * @brief Get the subtype (low 4 bits)
 * 
 * @param type The message type to extract from
 * @return MessageType The subtype (0x01, 0x02, etc.)
 */
inline MessageType GetSubtype(MessageType type) {
    return static_cast<MessageType>(static_cast<uint8_t>(type) & 0x0F);
}

/**
 * @brief Check if a type belongs to the Data category
 * 
 * @param type The message type to check
 * @return bool True if it's a Data message
 */
inline bool IsDataMessage(MessageType type) {
    return GetMainType(type) == MessageType::DATA_MSG;
}

/**
 * @brief Check if a type belongs to the Control category
 * 
 * @param type The message type to check
 * @return bool True if it's a Control message
 */
inline bool IsControlMessage(MessageType type) {
    return GetMainType(type) == MessageType::CONTROL_MSG;
}

/**
 * @brief Check if a type belongs to the Routing category
 * 
 * @param type The message type to check
 * @return bool True if it's a Routing message
 */
inline bool IsRoutingMessage(MessageType type) {
    return GetMainType(type) == MessageType::ROUTING_MSG;
}

/**
 * @brief Check if a type belongs to the System category
 * 
 * @param type The message type to check
 * @return bool True if it's a System message
 */
inline bool IsSystemMessage(MessageType type) {
    return GetMainType(type) == MessageType::SYSTEM_MSG;
}

/**
 * @brief Create a new message type by combining a main type and a subtype
 * 
 * @param main_type The main message type category (e.g., MessageType::DATA_MSG)
 * @param subtype The message subtype value (e.g., 0x01)
 * @return MessageType The combined message type
 */
inline MessageType CreateType(MessageType main_type, MessageType subtype) {
    return static_cast<MessageType>((static_cast<uint8_t>(main_type) & 0xF0) |
                                    (static_cast<uint8_t>(subtype) & 0x0F));
}

/**
 * @brief Check if a message type is valid
 * 
 * @param type The message type to validate
 * @return bool True if the type is valid
 */
inline bool IsValidType(MessageType type) {
    MessageType main_type = GetMainType(type);
    MessageType subtype = GetSubtype(type);

    // Check if main type is valid
    if (main_type != MessageType::DATA_MSG &&
        main_type != MessageType::CONTROL_MSG &&
        main_type != MessageType::ROUTING_MSG &&
        main_type != MessageType::SYSTEM_MSG) {
        return false;
    }

    // Check if subtype is valid (must be between 1-15, 0 is invalid)
    if (subtype == static_cast<MessageType>(0)) {
        return false;
    }

    return true;
}

}  // namespace message_type

}  // namespace loramesher