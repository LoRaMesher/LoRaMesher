// src/loramesher/types/messages/routing_message.hpp
#pragma once

#include "message.hpp"

namespace loramesher {

/**
 * @brief Header structure for routing-specific information
 */
struct RoutingHeader {
    AddressType next_hop; /**< Next hop address */
    uint8_t sequence_id;  /**< Sequence identifier */
    uint16_t number;      /**< Message number */

    /**
     * @brief Calculate the size of the routing header in bytes
     * @return Size of the header structure in bytes
     */
    static constexpr size_t size() {
        return sizeof(AddressType) +  // Next hop
               sizeof(uint8_t) +      // Sequence ID
               sizeof(uint16_t);      // Number
    }
};

/**
 * @brief Specialized message class for routing operations
 * 
 * Extends BaseMessage with routing-specific functionality and header information.
 */
class RoutingMessage : public BaseMessage {
   public:
    /**
     * @brief Create a new RoutingMessage instance
     * 
     * @param dest Destination address
     * @param src Source address
     * @param data Payload data
     * @return std::optional<RoutingMessage> Valid message if creation succeeded,
     *         std::nullopt otherwise
     */
    static std::optional<RoutingMessage> Create(
        AddressType dest, AddressType src, const std::vector<uint8_t>& data) {
        auto base_msg =
            BaseMessage::Create(dest, src, MessageType::ROUTING_MSG, data);
        if (!base_msg) {
            return std::nullopt;
        }

        return RoutingMessage(std::move(*base_msg));
    }

    /**
     * @brief Set routing information for the message
     * 
     * @param next_hop Next hop address
     * @param seq_id Sequence identifier
     * @param num Message number
     */
    void setRoutingInfo(AddressType next_hop, uint8_t seq_id, uint16_t num) {
        routing_header_.next_hop = next_hop;
        routing_header_.sequence_id = seq_id;
        routing_header_.number = num;
    }

    /**
     * @brief Get the routing header
     * @return const RoutingHeader& Reference to the routing header
     */
    const RoutingHeader& getRoutingHeader() const { return routing_header_; }

    /**
     * @brief Get the total size of the message including all headers
     * @return Total size in bytes
     */
    size_t getTotalSize() const {
        return RoutingHeader::size() + BaseMessage::getTotalSize();
    }

    /**
     * @brief Serialize the message to a byte vector
     * 
     * @return std::optional<std::vector<uint8_t>> Serialized message if successful,
     *         std::nullopt otherwise
     */
    std::optional<std::vector<uint8_t>> Serialize() const {
        std::vector<uint8_t> serialized(getTotalSize());
        utils::ByteSerializer serializer(serialized);

        // Serialize base message
        auto result = BaseMessage::Serialize(serializer);
        if (!result) {
            return std::nullopt;
        }

        // Serialize routing header
        serializer.WriteUint16(routing_header_.next_hop);
        serializer.WriteUint8(routing_header_.sequence_id);
        serializer.WriteUint16(routing_header_.number);

        // Serialize payload
        serializer.WriteBytes(getPayload().data(), getPayload().size());

        return serialized;
    }

    /**
     * @brief Create a RoutingMessage from serialized data
     * 
     * @param data Serialized message data
     * @return std::optional<RoutingMessage> Deserialized message if successful,
     *         std::nullopt otherwise
     */
    static std::optional<RoutingMessage> CreateFromSerialized(
        const std::vector<uint8_t>& data) {
        if (data.size() < BaseHeader::size() + RoutingHeader::size()) {
            return std::nullopt;
        }

        utils::ByteDeserializer deserializer(data);

        auto base_header = BaseMessage::Deserialize(deserializer);
        if (!base_header) {
            return std::nullopt;
        }

        // Read routing header
        auto next_hop = deserializer.ReadUint16();
        auto seq_id = deserializer.ReadUint8();
        auto number = deserializer.ReadUint16();

        if (!next_hop || !seq_id || !number) {
            return std::nullopt;
        }

        auto payload = deserializer.ReadBytes(base_header->payloadSize);
        if (!payload) {
            return std::nullopt;
        }

        auto msg =
            Create(base_header->destination, base_header->source, *payload);
        if (!msg) {
            return std::nullopt;
        }

        msg->setRoutingInfo(*next_hop, *seq_id, *number);
        return msg;
    }

   private:
    /**
     * @brief Private constructor from BaseMessage
     * 
     * @param base_msg Base message to construct from
     */
    explicit RoutingMessage(BaseMessage&& base_msg)
        : BaseMessage(std::move(base_msg)), routing_header_() {}

    RoutingHeader routing_header_; /**< Routing-specific header information */
};

}  // namespace loramesher