// src/types/messages/message.hpp
#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "utilities/byte_operations.h"

namespace loramesher {

/** Type alias for address representations */
using AddressType = uint16_t;

/**
 * @brief Enumeration of possible message types in the system
 */
enum class MessageType : uint8_t {
    DATA = 0x01,    /**< Regular data message */
    XL_DATA = 0x02, /**< Large data message */
    HELLO = 0x03,   /**< Hello packet for routing */
    ACK = 0x04,     /**< Acknowledgment */
    LOST = 0x05,    /**< Packet loss notification */
    SYNC = 0x06,    /**< Synchronization packet */
    NEED_ACK = 0x07 /**< Request for acknowledgment */
};

/**
 * @brief Header structure for all message types
 * 
 * Contains common fields that are present in all message types including
 * addressing information and payload metadata.
 */
struct BaseHeader {
    AddressType destination; /**< Destination address */
    AddressType source;      /**< Source address */
    MessageType type;        /**< Message type identifier */
    uint8_t payloadSize;     /**< Size of the payload in bytes */

    /**
     * @brief Calculate the size of the header in bytes
     * @return Size of the header structure in bytes
     */
    static constexpr size_t size() {
        return sizeof(AddressType) +  // Destination
               sizeof(AddressType) +  // Source
               sizeof(MessageType) +  // Type
               sizeof(uint8_t);       // Payload size
    }
};

/**
 * @brief Base class for all message types in the system
 * 
 * Provides common functionality for message handling including serialization,
 * deserialization, and basic message operations. All specific message types
 * should inherit from this class.
 */
class BaseMessage {
   public:
    /**
     * @brief Maximum allowed payload size (255 bytes due to uint8_t payloadSize in header)
     */
    static constexpr size_t MAX_PAYLOAD_SIZE =
        std::numeric_limits<uint8_t>::max();

    /**
     * @brief Construct a new Base Message
     * 
     * @param dest Destination address for the message
     * @param src Source address of the message
     * @param type Type of the message
     * @param data Payload data for the message
     * 
     * @throws std::length_error If data size exceeds maximum allowed size
     * @throws std::invalid_argument If addresses are invalid or message type is unsupported
     */
    BaseMessage(AddressType dest, AddressType src, MessageType type,
                const std::vector<uint8_t>& data);

    /**
     * @brief Construct a new Base Message from serialized data
     * 
     * @param data Raw byte vector containing the serialized message
     * 
     * @throws std::runtime_error If deserialization fails or data is invalid
     * @throws std::length_error If payload size exceeds MAX_PAYLOAD_SIZE
     * @throws std::invalid_argument If addresses are invalid or message type is unsupported
     * @throws std::out_of_range If data vector is too short to contain a valid message
     */
    BaseMessage(const std::vector<uint8_t>& data);

    /**
     * @brief Copy constructor
     * 
     * Creates a deep copy of the message including its payload.
     * 
     * @param other Message to copy from
     */
    BaseMessage(const BaseMessage& other);

    /**
     * @brief Copy assignment operator
     * 
     * Performs a deep copy of the message including its payload.
     * 
     * @param other Message to copy from
     * @return Reference to this message after copying
     */
    BaseMessage& operator=(const BaseMessage& other);

    /**
     * @brief Move constructor
     * 
     * Transfers ownership of the message payload efficiently.
     * 
     * @param other Message to move from
     */
    BaseMessage(BaseMessage&& other) noexcept;

    /**
     * @brief Move assignment operator
     * 
     * Transfers ownership of the message payload efficiently.
     * 
     * @param other Message to move from
     * @return Reference to this message after moving
     */
    BaseMessage& operator=(BaseMessage&& other) noexcept;

    /**
     * @brief Virtual destructor to ensure proper cleanup of derived classes
     */
    virtual ~BaseMessage() = default;

    /**
     * @brief Get the message header
     * @return Const reference to the message header
     */
    const BaseHeader& getBaseHeader() const { return baseHeader_; }

    /**
     * @brief Get the message payload
     * @return Const reference to the payload data
     */
    const std::vector<uint8_t>& getPayload() const { return payload_; }

    /**
     * @brief Get the total size of the message
     * @return Size in bytes including header and payload
     */
    size_t getTotalSize() const { return BaseHeader::size() + payload_.size(); }

    /**
     * @brief Serialize the message header
     * 
     * @param serializer Serializer object to write to
     * @throws std::runtime_error If serialization fails
     */
    void serialize(ByteSerializer& serializer) const;

    /**
     * @brief Serialize the complete message
     * 
     * Serializes both header and payload into a byte vector.
     * 
     * @return Vector containing the serialized message
     * @throws std::runtime_error If serialization fails
     */
    std::vector<uint8_t> serialize() const;

    /**
     * @brief Deserialize a message header from raw data
     * 
     * @param deserializer Deserializer containing the raw data
     * @return Deserialized header structure
     * @throws std::runtime_error If deserialization fails or data is invalid
     */
    static BaseHeader deserialize(ByteDeserializer& deserializer);

    /**
     * @brief Create a message from serialized data
     * 
     * @param data Raw byte vector containing the serialized message
     * @return Unique pointer to the deserialized message
     * @throws std::runtime_error If deserialization fails or data is invalid
     */
    static std::unique_ptr<BaseMessage> deserialize(
        const std::vector<uint8_t>& data);

   private:
    /**
     * @brief Validate all input parameters
     * 
     * @param dest Destination address
     * @param src Source address
     * @param type Message type
     * @param data Payload data
     * @throws std::length_error If payload size exceeds MAX_PAYLOAD_SIZE
     * @throws std::invalid_argument If addresses are invalid or message type is unsupported
     */
    void validateInputs(AddressType dest, AddressType src, MessageType type,
                        const std::vector<uint8_t>& data) const;

   protected:
    BaseHeader baseHeader_;        /**< Message header */
    std::vector<uint8_t> payload_; /**< Message payload */
};

}  // namespace loramesher