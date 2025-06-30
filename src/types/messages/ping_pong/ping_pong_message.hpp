/**
 * @file ping_pong_message.hpp
 * @brief Definition of PingPong message types for connectivity testing
 */

#pragma once

#include "ping_pong_header.hpp"
#include "types/messages/base_message.hpp"
#include "utils/byte_operations.h"
#include "utils/logger.hpp"

namespace loramesher {

/**
 * @brief PingPong message for connectivity testing
 * 
 * Implements the IConvertibleToBaseMessage interface to provide conversion
 * to the BaseMessage format for transmission. Uses PingPongHeader for
 * header information.
 */
class PingPongMessage : public IConvertibleToBaseMessage {
   public:
    /**
     * @brief Creates a new PingPong message
     * 
     * @param dest Destination address for the message
     * @param src Source address of the message
     * @param subtype PingPong subtype (PING or PONG)
     * @param sequence_number Sequence number for matching requests/responses
     * @param timestamp Timestamp for latency calculation
     * @return std::optional<PingPongMessage> Valid message if creation succeeded,
     *         std::nullopt otherwise
     */
    static std::optional<PingPongMessage> Create(AddressType dest,
                                                 AddressType src,
                                                 PingPongSubtype subtype,
                                                 uint16_t sequence_number,
                                                 uint32_t timestamp);

    /**
     * @brief Creates a PingPong message from serialized data
     * 
     * @param data Serialized message data
     * @return std::optional<PingPongMessage> Deserialized message if successful,
     *         std::nullopt otherwise
     */
    static std::optional<PingPongMessage> CreateFromSerialized(
        const std::vector<uint8_t>& data);

    /**
     * @brief Sets PingPong information for the message
     * 
     * @param subtype Message subtype (PING or PONG)
     * @param sequence_number Sequence number
     * @param timestamp Timestamp
     * @return Result Success if setting the info succeeded, error code otherwise
     */
    Result SetInfo(PingPongSubtype subtype, uint16_t sequence_number,
                   uint32_t timestamp);

    /**
     * @brief Gets the subtype of the PingPong message
     * 
     * @return PingPongSubtype The message subtype (PING or PONG)
     */
    PingPongSubtype GetSubtype() const;

    /**
     * @brief Gets the sequence number
     * 
     * @return uint16_t The sequence number
     */
    uint16_t GetSequenceNumber() const;

    /**
     * @brief Gets the timestamp
     * 
     * @return uint32_t The timestamp
     */
    uint32_t GetTimestamp() const;

    /**
     * @brief Gets the source address
     * 
     * @return AddressType The source address
     */
    AddressType GetSource() const;

    /**
     * @brief Gets the destination address
     * 
     * @return AddressType The destination address
     */
    AddressType GetDestination() const;

    /**
     * @brief Gets the PingPong header
     * 
     * @return const PingPongHeader& The PingPong header
     */
    const PingPongHeader& GetHeader() const;

    /**
     * @brief Calculates the round-trip time against a reference timestamp
     * 
     * @param reference_timestamp The reference timestamp to compare against
     * @return uint32_t The calculated round-trip time
     */
    uint32_t CalculateRTT(uint32_t reference_timestamp) const;

    /**
     * @brief Gets the total size of the serialized message
     * 
     * @return size_t Total size in bytes
     */
    size_t GetTotalSize() const;

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
     * @param header The PingPong header with all necessary fields
     */
    explicit PingPongMessage(const PingPongHeader& header);

    PingPongHeader header_;  ///< PingPong header with all necessary fields
};

}  // namespace loramesher