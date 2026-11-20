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
 * A secured frame reuses two bits of the type byte on the wire (see the
 * wire type helpers below): bit 7 marks the frame as secured and bit 3
 * carries the parity of the key epoch. For this reason a valid type uses
 * the categories 1 to 5 and the subtypes 1 to 7 only.
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
    KEY_MSG = 0x50,      ///< 0101 xxxx: Key management category

    // Predefined complete message types

    // Data messages (0x1x)
    DATA = 0x11,            ///< 0001 0001: Regular data message
    DATA_BROADCAST = 0x12,  ///< 0001 0010: Broadcast data message
    DATA_RELIABLE = 0x13,   ///< 0001 0011: Reliable (acknowledged) data message
    DATA_GROUP = 0x14,      ///< 0001 0100: Group (multicast) data message

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
    NM_CLAIM = 0x47,         ///< 0100 0111: Network Manager election claim

    // Key management messages (0x5x), wire security specification S3.1
    KEY_UPDATE = 0x51,      ///< 0101 0001: Next network key for one node
    KEY_UPDATE_ACK = 0x52,  ///< 0101 0010: Answer to KEY_UPDATE
    KEY_REQUEST = 0x53,     ///< 0101 0011: Orphan asks for the network key
    KEY_GRANT = 0x54,       ///< 0101 0100: Neighbour hands over the grant
    KEY_RELAY = 0x55,  ///< 0101 0101: Request or grant to or from the gateway
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
 * @brief Check if a type belongs to the Key management category
 *
 * @param type The message type to check
 * @return bool True if it's a Key management message
 */
inline bool IsKeyMessage(MessageType type) {
    return GetMainType(type) == MessageType::KEY_MSG;
}

/// Bit 7 of the type byte on the wire: the frame is secured
constexpr uint8_t kSecuredFlag = 0x80;
/// Bit 3 of the type byte on the wire: parity of the key epoch
constexpr uint8_t kEpochParityFlag = 0x08;
/// Bits of the type byte that hold the category and the subtype
constexpr uint8_t kPlainTypeMask = 0x77;

/**
 * @brief Type byte of a secured frame
 *
 * @param plain_type Message type as the protocol uses it
 * @param epoch Key epoch the frame is sealed under
 * @return uint8_t Type byte as sent on the wire
 */
constexpr uint8_t ToWireType(MessageType plain_type, uint8_t epoch) {
    return static_cast<uint8_t>(
        kSecuredFlag | ((epoch & 1u) != 0 ? kEpochParityFlag : 0u) |
        (static_cast<uint8_t>(plain_type) & kPlainTypeMask));
}

/**
 * @brief Message type carried by the type byte of a secured frame
 */
constexpr MessageType ToPlainType(uint8_t wire_type) {
    return static_cast<MessageType>(wire_type & kPlainTypeMask);
}

/**
 * @brief True when the type byte marks a secured frame
 */
constexpr bool IsSecuredWireType(uint8_t wire_type) {
    return (wire_type & kSecuredFlag) != 0;
}

/**
 * @brief Epoch parity carried by the type byte of a secured frame
 *
 * @return uint8_t 0 or 1
 */
constexpr uint8_t GetEpochParity(uint8_t wire_type) {
    return (wire_type & kEpochParityFlag) != 0 ? 1 : 0;
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
        main_type != MessageType::SYSTEM_MSG &&
        main_type != MessageType::KEY_MSG) {
        return false;
    }

    // Check if subtype is valid (must be between 1-7, 0 is invalid). Bit 3
    // of the type byte is the epoch parity of a secured frame.
    if (subtype == static_cast<MessageType>(0) ||
        static_cast<uint8_t>(subtype) > 7) {
        return false;
    }

    return true;
}

}  // namespace message_type

}  // namespace loramesher
