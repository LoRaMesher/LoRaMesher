/**
 * @file data_message.hpp
 * @brief Definition of data message for mesh networking with next-hop routing
 */

#pragma once

#include "data_header.hpp"
#include "types/messages/base_message.hpp"
#include "utils/byte_operations.h"
#include "utils/logger.hpp"

namespace loramesher {

/**
 * @brief Message for transmitting user data through the mesh network
 *
 * Implements the IConvertibleToBaseMessage interface to provide conversion
 * to the BaseMessage format for transmission. Uses DataHeader for
 * header information including next-hop routing.
 */
class DataMessage : public IConvertibleToBaseMessage {
   public:
    /**
     * @brief Creates a new data message
     *
     * @param dest Final destination address
     * @param src Source address (original sender)
     * @param next_hop Next hop address for routing
     * @param payload User data payload
     * @return std::optional<DataMessage> Valid message if creation succeeded,
     *         std::nullopt otherwise
     */
    static std::optional<DataMessage> Create(
        AddressType dest, AddressType src, AddressType next_hop,
        const std::vector<uint8_t>& payload, uint8_t ttl = 0,
        uint8_t seq_num = 0);

    /**
     * @brief Creates a data message from serialized data
     *
     * @param data Serialized message data
     * @return std::optional<DataMessage> Deserialized message if successful,
     *         std::nullopt otherwise
     */
    static std::optional<DataMessage> CreateFromSerialized(
        const std::vector<uint8_t>& data);

    /**
     * @brief Gets the final destination address
     *
     * @return AddressType Final destination address
     */
    AddressType GetDestination() const;

    /**
     * @brief Gets the source address (original sender)
     *
     * @return AddressType Source address
     */
    AddressType GetSource() const;

    /**
     * @brief Creates a forwarded copy with decremented TTL
     *
     * @param original The original data message to forward
     * @param new_next_hop Next hop address for the forwarded message
     * @return std::optional<DataMessage> Forwarded message, or nullopt if TTL expired
     */
    static std::optional<DataMessage> CreateForwarded(
        const DataMessage& original, AddressType new_next_hop);

    /**
     * @brief Gets the next hop address for routing
     *
     * @return AddressType Next hop address
     */
    AddressType GetNextHop() const;

    uint8_t GetTTL() const;
    uint8_t GetSeqNum() const;

    /**
     * @brief Gets the user data payload
     *
     * @return const std::vector<uint8_t>& User data payload
     */
    const std::vector<uint8_t>& GetPayload() const;

    /**
     * @brief Gets the data header
     *
     * @return const DataHeader& The data header
     */
    const DataHeader& GetHeader() const;

    /**
     * @brief Gets a mutable reference to the data header
     *
     * @return DataHeader& Mutable reference to the data header
     */
    DataHeader& GetMutableHeader();

    /**
     * @brief Gets the total size of the serialized message
     *
     * @return size_t Total size in bytes
     */
    size_t GetTotalSize() const;

    /**
     * @brief Sets the next hop address
     *
     * @param next_hop New next hop address
     * @return Result Success if setting succeeded, error code otherwise
     */
    Result SetNextHop(AddressType next_hop);

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
     * @param header The data header
     * @param payload The user data payload
     */
    DataMessage(const DataHeader& header, const std::vector<uint8_t>& payload);

    DataHeader header_;             ///< Data message header
    std::vector<uint8_t> payload_;  ///< User data payload
};

}  // namespace loramesher
