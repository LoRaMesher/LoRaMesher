// src/loramesher/types/messages/message.hpp
#pragma once

#include "build_options.hpp"
#include "utilities/byte_operations.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace loramesher {

using AddressType = uint16_t;

// Forward declarations of message types
enum class MessageType : uint8_t {
    DATA = 0x01,     // Regular data message
    XL_DATA = 0x02,  // Large data message
    HELLO = 0x03,    // Hello packet for routing
    ACK = 0x04,      // Acknowledgment
    LOST = 0x05,     // Packet loss notification
    SYNC = 0x06,     // Synchronization packet
    NEED_ACK = 0x07  // Request for acknowledgment
};

struct BaseHeader {
    AddressType destination;  // Destination address
    AddressType source;       // Source address
    MessageType type;         // Message type
    uint8_t payloadSize;      // Size of payload

    static constexpr size_t size() {
        return sizeof(AddressType) +  // Destination
               sizeof(AddressType) +  // Source
               sizeof(MessageType) +  // Type
               sizeof(uint8_t);       // Payload size
    }
};

class BaseMessage {
   public:
    /**
    * @brief Construct a new Base Message object
    */
    BaseMessage(AddressType dest, AddressType src, MessageType type,
                const std::vector<uint8_t>& data)
        : m_baseHeader{dest, src, type, static_cast<uint8_t>(data.size())},
          m_payload(data) {}

    /**
     * @brief Destroy the Base Message object
     */
    virtual ~BaseMessage() = default;

    // Getters
    /**
     * @brief Get the Base Header object
     */
    const BaseHeader& getBaseHeader() const { return m_baseHeader; }

    /**
     * @brief Get the Payload object
     */
    const std::vector<uint8_t>& getPayload() const { return m_payload; }

    // Utility methods
    /**
     * @brief Get the Total Size object
     */
    size_t getTotalSize() const {
        return BaseHeader::size() + m_payload.size();
    }

    // Serialization
    /**
     * @brief Serialize a BaseMessage object to a byte array
     */
    void serialize(ByteSerializer& serializer) const {
        serializer.writeUint16(m_baseHeader.destination);
        serializer.writeUint16(m_baseHeader.source);
        serializer.writeUint8(static_cast<uint8_t>(m_baseHeader.type));
        serializer.writeUint8(m_baseHeader.payloadSize);
    }

    /**
     * @brief Serialize a BaseMessage object to a byte array
     */
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> serialized(getTotalSize());
        ByteSerializer serializer(serialized);

        // Serialize headers
        serialize(serializer);

        // Serialize payload
        serializer.writeBytes(m_payload.data(), m_payload.size());

        return serialized;
    }

    static BaseHeader deserialize(ByteDeserializer& deserializer) {
        BaseHeader header;
        header.destination = deserializer.readUint16();
        header.source = deserializer.readUint16();
        header.type = static_cast<MessageType>(deserializer.readUint8());
        header.payloadSize = deserializer.readUint8();
        return header;
    }

    /**
     * @brief Deserialize a byte array to a BaseMessage object
     * 
     * @param data 
     * @return std::unique_ptr<BaseMessage> 
     */
    static std::unique_ptr<BaseMessage> deserialize(
        const std::vector<uint8_t>& data) {
        ByteDeserializer deserializer(data);
        BaseHeader header = deserialize(deserializer);
        std::vector<uint8_t> payload =
            deserializer.readBytes(header.payloadSize);

        return std::make_unique<BaseMessage>(header.destination, header.source,
                                             header.type, payload);
    }

   protected:
    BaseHeader m_baseHeader;
    std::vector<uint8_t> m_payload;
};

}  // namespace loramesher