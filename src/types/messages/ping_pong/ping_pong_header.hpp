/**
 * @file ping_pong_header.hpp
 * @brief Header definition for PingPong messages
 */

#pragma once

#include "types/messages/base_header.hpp"

namespace loramesher {

/**
 * @brief Enumeration of different PingPong message subtypes
 */
enum class PingPongSubtype : uint8_t {
    PING = 0x03, /**< Ping request message */
    PONG = 0x04  /**< Pong response message */
};

/**
 * @brief Header for PingPong messages
 * 
 * Extends BaseHeader with PingPong-specific fields: subtype, sequence number,
 * and timestamp for latency calculation.
 */
class PingPongHeader : public BaseHeader {
   public:
    /**
      * @brief Default constructor
      */
    PingPongHeader() = default;

    /**
     * @brief Constructor with all fields
     * 
     * @param dest Destination address
     * @param src Source address
     * @param subtype PingPong subtype (PING or PONG)
     * @param sequence_number Sequence number for matching ping/pong pairs
     * @param timestamp Timestamp for latency calculation
     */
    PingPongHeader(AddressType dest, AddressType src, PingPongSubtype subtype,
                   uint16_t sequence_number, uint32_t timestamp);

    /**
     * @brief Gets the PingPong subtype
     * 
     * @return PingPongSubtype The subtype (PING or PONG)
     */
    PingPongSubtype GetSubtype() const { return subtype_; }

    /**
     * @brief Gets the sequence number
     * 
     * @return uint16_t The sequence number
     */
    uint16_t GetSequenceNumber() const { return sequence_number_; }

    /**
     * @brief Gets the timestamp
     * 
     * @return uint32_t The timestamp
     */
    uint32_t GetTimestamp() const { return timestamp_; }

    /**
     * @brief Sets the PingPong-specific information
     * 
     * @param subtype PingPong subtype (PING or PONG)
     * @param sequence_number Sequence number
     * @param timestamp Timestamp
     * @return Result Success if setting succeeded, error code otherwise
     */
    Result SetPingPongInfo(PingPongSubtype subtype, uint16_t sequence_number,
                           uint32_t timestamp);

    /**
     * @brief Serializes the header to a byte serializer
     * 
     * Extends the base header serialization with PingPong-specific fields.
     * 
     * @param serializer Serializer to write the header to
     * @return Result Success if serialization succeeded, error code otherwise
     */
    Result Serialize(utils::ByteSerializer& serializer) const override;

    /**
     * @brief Deserializes a PingPong header from a byte deserializer
     * 
     * @param deserializer Deserializer containing the header data
     * @return std::optional<PingPongHeader> Deserialized header if successful,
     *         std::nullopt otherwise
     */
    static std::optional<PingPongHeader> Deserialize(
        utils::ByteDeserializer& deserializer);

    /**
     * @brief Calculates the size of the PingPong-specific header extension
     * 
     * @return size_t Size of the PingPong header fields in bytes
     */
    static constexpr size_t PingPongFieldsSize() {
        return sizeof(uint16_t) +  // Sequence number
               sizeof(uint32_t);   // Timestamp
    }

    /**
     * @brief Gets the total size of this header type
     * 
     * @return size_t Size of the header in bytes (base + PingPong-specific fields)
     */
    size_t GetSize() const override {
        return BaseHeader::Size() + PingPongFieldsSize();
    }

    /**
     * @brief Validates a PingPong subtype
     * 
     * @param subtype Subtype to validate
     * @return Result Success if valid, error code otherwise
     */
    static Result IsValidSubtype(PingPongSubtype subtype);

   private:
    PingPongSubtype subtype_ =
        PingPongSubtype::PING;  ///< PingPong message subtype
    uint16_t sequence_number_ =
        0;                    ///< Sequence number for matching ping/pong pairs
    uint32_t timestamp_ = 0;  ///< Timestamp for latency calculation
};

}  // namespace loramesher