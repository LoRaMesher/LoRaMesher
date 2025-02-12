// src/loramesher/types/messages/routing_message.hpp
#pragma once

#include "message.hpp"

namespace loramesher {

// Routing message header
// TODO: Placeholder
struct RoutingHeader {
    AddressType nextHop;
    uint8_t sequenceId;
    uint16_t number;

    static constexpr size_t size() {
        return sizeof(AddressType) +  // Next hop
               sizeof(uint8_t) +      // Sequence ID
               sizeof(uint16_t);      // Number
    }
};

class RoutingMessage : public BaseMessage {
   public:
    RoutingMessage(AddressType dest, AddressType src,
                   const std::vector<uint8_t>& data)
        : BaseMessage(dest, src, MessageType::DATA, data) {}

    void setRoutingInfo(AddressType nextHop, uint8_t seqId, uint16_t num) {
        routingHeader_.nextHop = nextHop;
        routingHeader_.sequenceId = seqId;
        routingHeader_.number = num;
    }

    /**
     * @brief Get the Routing Header object
     */
    RoutingHeader getRoutingHeader() const { return routingHeader_; }

    /**
     * @brief Get the Total Size object
     */
    size_t getTotalSize() const {
        return RoutingHeader::size() + BaseMessage::getTotalSize();
    }

    /**
     * @brief Serialize the message to a byte array
     */
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> serialized(getTotalSize());
        utils::ByteSerializer serializer(serialized);

        // Serialize headers
        BaseMessage::serialize(serializer);

        // Serialize routing headers
        serializer.writeUint16(routingHeader_.nextHop);
        serializer.writeUint8(routingHeader_.sequenceId);
        serializer.writeUint16(routingHeader_.number);

        // Serialize payload
        serializer.writeBytes(getPayload().data(), getPayload().size());

        return serialized;
    }

    /**
     * @brief Deserialize a byte array to a message
     */
    static std::unique_ptr<RoutingMessage> deserialize(
        const std::vector<uint8_t>& data) {
        utils::ByteDeserializer deserializer(data);
        BaseHeader header = BaseMessage::deserialize(deserializer);

        RoutingHeader routingHeader;
        routingHeader.nextHop = deserializer.readUint16();
        routingHeader.sequenceId = deserializer.readUint8();
        routingHeader.number = deserializer.readUint16();

        std::vector<uint8_t> payload =
            deserializer.readBytes(header.payloadSize);

        auto msg = std::make_unique<RoutingMessage>(header.destination,
                                                    header.source, payload);
        msg->setRoutingInfo(routingHeader.nextHop, routingHeader.sequenceId,
                            routingHeader.number);

        return msg;
    }

   private:
    RoutingHeader routingHeader_;
};

}  // namespace loramesher
