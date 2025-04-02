/**
 * @file base_header.hpp
 * @brief Definition of common header functionality for message types
 */

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "message_type.hpp"
#include "types/error_codes/result.hpp"
#include "utils/byte_operations.h"
#include "utils/logger.hpp"

namespace loramesher {

/** Type alias for address representations */
using AddressType = uint16_t;

/**
 * @brief Base header class for all message types
 * 
 * This class encapsulates the common header fields and operations
 * that are used by all message types in the system. It can be inherited
 * by specific message headers to extend functionality.
 */
class BaseHeader {
   public:
    /**
     * @brief Default constructor
     */
    BaseHeader() = default;

    /**
     * @brief Constructor with all fields
     * 
     * @param dest Destination address
     * @param src Source address
     * @param type Message type
     * @param payload_size Size of the message payload
     */
    BaseHeader(AddressType dest, AddressType src, MessageType type,
               uint8_t payload_size);

    /**
     * @brief Virtual destructor to support inheritance
     */
    virtual ~BaseHeader() = default;

    /**
     * @brief Gets the destination address
     * 
     * @return AddressType The destination address
     */
    AddressType GetDestination() const { return destination_; }

    /**
     * @brief Gets the source address
     * 
     * @return AddressType The source address
     */
    AddressType GetSource() const { return source_; }

    /**
     * @brief Gets the message type
     * 
     * @return MessageType The message type
     */
    MessageType GetType() const { return type_; }

    /**
     * @brief Gets the payload size
     * 
     * @return uint8_t The payload size in bytes
     */
    uint8_t GetPayloadSize() const { return payload_size_; }

    /**
     * @brief Sets the header fields
     * 
     * @param dest Destination address
     * @param src Source address
     * @param type Message type
     * @param payload_size Size of the message payload
     * @return Result Success if setting the header succeeded, error code otherwise
     */
    Result SetHeader(AddressType dest, AddressType src, MessageType type,
                     uint8_t payload_size);

    /**
     * @brief Serializes the header to a byte serializer
     * 
     * Base implementation for all header types. Derived classes should
     * call this base method and then add their own serialization.
     * 
     * @param serializer Serializer to write the header to
     * @return Result Success if serialization succeeded, error code otherwise
     */
    virtual Result Serialize(utils::ByteSerializer& serializer) const;

    /**
     * @brief Serializes the header to a byte vector
     * 
     * @return std::vector<uint8_t> Serialized header
     */
    std::vector<uint8_t> Serialize() const;

    /**
     * @brief Deserializes a header from a byte deserializer
     * 
     * @param deserializer Deserializer containing the header data
     * @return std::optional<BaseHeader> Deserialized header if successful,
     *         std::nullopt otherwise
     */
    static std::optional<BaseHeader> Deserialize(
        utils::ByteDeserializer& deserializer);

    /**
     * @brief Calculates the size of the header in bytes
     * 
     * @return size_t Size of the header in bytes
     */
    static constexpr size_t Size() {
        return sizeof(AddressType) +  // Destination
               sizeof(AddressType) +  // Source
               sizeof(MessageType) +  // Type
               sizeof(uint8_t);       // Payload size
    }

    /**
     * @brief Gets the total size of this header type
     * 
     * Virtual method to be overridden by derived header classes.
     * 
     * @return size_t Size of the header in bytes
     */
    virtual size_t GetSize() const { return Size(); }

    /**
     * @brief Validates a message type
     * 
     * @param type Message type to validate
     * @return Result Success if the message type is valid, error code otherwise
     */
    static Result IsValidMessageType(MessageType type);

   protected:
    AddressType destination_ = 0;               ///< Destination address
    AddressType source_ = 0;                    ///< Source address
    MessageType type_ = MessageType::DATA_MSG;  ///< Message type
    uint8_t payload_size_ = 0;  ///< Size of the payload in bytes
};

}  // namespace loramesher