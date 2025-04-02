/**
 * @file message.hpp
 * @brief Definition of base message structure for the communication system
 */

#pragma once

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base_header.hpp"
#include "types/error_codes/result.hpp"
#include "utils/byte_operations.h"

namespace loramesher {

/**
 * @brief Base message class for all message communications
 * 
 * This class provides common message functionality for serialization,
 * deserialization, and basic message operations. It is not intended to be
 * a base class for inheritance, but rather a standard message format that
 * all specific message types can be converted to.
 */
class BaseMessage {
   public:
    /**
     * @brief Maximum allowed payload size (255 bytes due to uint8_t payload_size in header)
     */
    static constexpr size_t kMaxPayloadSize =
        std::numeric_limits<uint8_t>::max();

    /**
     * @brief Creates a new Base Message
     * 
     * @param dest Destination address for the message
     * @param src Source address of the message
     * @param type Type of the message
     * @param data Payload data for the message
     * @return std::optional<BaseMessage> Valid message if creation succeeded,
     *         std::nullopt otherwise
     */
    static std::optional<BaseMessage> Create(AddressType dest, AddressType src,
                                             MessageType type,
                                             const std::vector<uint8_t>& data);

    /**
     * @brief Creates a new Base Message from serialized data
     * 
     * @param data Raw byte vector containing the serialized message
     * @return std::optional<BaseMessage> Valid message if creation succeeded, 
     *         std::nullopt otherwise
     */
    static std::optional<BaseMessage> CreateFromSerialized(
        const std::vector<uint8_t>& data);

    /**
     * @brief Constructor
     * 
     * @param dest Destination address
     * @param src Source address
     * @param type Message type
     * @param data Payload data
     */
    BaseMessage(AddressType dest, AddressType src, MessageType type,
                const std::vector<uint8_t>& data);

    /**
     * @brief Copy constructor
     * 
     * @param other Message to copy from
     */
    BaseMessage(const BaseMessage& other);

    /**
     * @brief Copy assignment operator
     * 
     * @param other Message to copy from
     * @return Reference to this message after copying
     */
    BaseMessage& operator=(const BaseMessage& other);

    /**
     * @brief Move constructor
     * 
     * @param other Message to move from
     */
    BaseMessage(BaseMessage&& other) noexcept;

    /**
     * @brief Move assignment operator
     * 
     * @param other Message to move from
     * @return Reference to this message after moving
     */
    BaseMessage& operator=(BaseMessage&& other) noexcept;

    /**
     * @brief Destructor
     */
    ~BaseMessage() = default;

    /**
     * @brief Sets the message header
     * 
     * @param header New header for the message
     * @return Result Success if setting the header succeeded, error code otherwise
     */
    Result SetHeader(const BaseHeader& header);

    /**
     * @brief Sets message header and payload
     * 
     * @param dest Destination address
     * @param src Source address
     * @param type Message type
     * @param data Payload data
     * @return Result Success if setting the header succeeded, error code otherwise
     */
    Result SetMessage(AddressType dest, AddressType src, MessageType type,
                      const std::vector<uint8_t>& data);

    /**
     * @brief Gets the message header
     * 
     * @return Const reference to the message header
     */
    const BaseHeader& GetHeader() const { return header_; }

    /**
     * @brief Gets the message payload
     * 
     * @return Const reference to the payload data
     */
    const std::vector<uint8_t>& GetPayload() const { return payload_; }

    /**
     * @brief Gets the total size of the message
     * 
     * @return Size in bytes including header and payload
     */
    size_t GetTotalSize() const { return header_.GetSize() + payload_.size(); }

    /**
     * @brief Serializes the complete message
     * 
     * Serializes both header and payload into a byte vector.
     * 
     * @return std::optional<std::vector<uint8_t>> Serialized message if successful,
     *         std::nullopt otherwise
     */
    std::optional<std::vector<uint8_t>> Serialize() const;

   private:
    /**
     * @brief Validates all input parameters
     * 
     * @param dest Destination address
     * @param src Source address
     * @param type Message type
     * @param data Payload data
     * @return Result Success if validation passed, error code otherwise
     */
    static Result ValidateInputs(AddressType dest, AddressType src,
                                 MessageType type,
                                 const std::vector<uint8_t>& data);

    BaseHeader header_;             ///< Message header
    std::vector<uint8_t> payload_;  ///< Message payload
};

/**
 * @brief Interface for message types that can be converted to BaseMessage
 * 
 * All specific message types should implement this interface to provide
 * a standard way to convert them to the BaseMessage format for transmission.
 */
class IConvertibleToBaseMessage {
   public:
    /**
     * @brief Virtual destructor
     */
    virtual ~IConvertibleToBaseMessage() = default;

    /**
     * @brief Converts the specific message to a BaseMessage
     * 
     * @return BaseMessage representing this message for transmission
     */
    virtual BaseMessage ToBaseMessage() const = 0;

    /**
     * @brief Serializes the message to a byte vector
     * 
     * @return std::optional<std::vector<uint8_t>> Serialized message if successful,
     *         std::nullopt otherwise
     */
    virtual std::optional<std::vector<uint8_t>> Serialize() const = 0;
};

}  // namespace loramesher