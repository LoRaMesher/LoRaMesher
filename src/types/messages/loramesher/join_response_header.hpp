/**
 * @file join_response_header.hpp
 * @brief Header definition for network join response messages
 */

#pragma once

#include "types/messages/base_header.hpp"

namespace loramesher {

/**
  * @brief Header for JOIN_RESPONSE messages
  * 
  * Extends BaseHeader with join response specific fields: network ID,
  * allocated slots, and response status.
  */
class JoinResponseHeader : public BaseHeader {
   public:
    /**
      * @brief Response status codes
      */
    enum ResponseStatus : uint8_t {
        ACCEPTED = 0x00,               ///< Join request accepted
        REJECTED = 0x01,               ///< Join request rejected
        CAPACITY_EXCEEDED = 0x02,      ///< Network at capacity
        AUTHENTICATION_FAILED = 0x03,  ///< Authentication failed
        RETRY_LATER = 0x04,            ///< Retry join request later
        RESERVED = 0x05                ///< Reserved for future use
    };

    /**
      * @brief Default constructor
      */
    JoinResponseHeader() = default;

    /**
      * @brief Constructor with all fields
      * 
      * @param dest Destination address (requesting node)
      * @param src Source address (network manager)
      * @param network_id Network identifier
      * @param allocated_slots Number of data slots allocated
      * @param status Response status code
      */
    JoinResponseHeader(AddressType dest, AddressType src, uint16_t network_id,
                       uint8_t allocated_slots, ResponseStatus status);

    /**
      * @brief Gets the network ID
      * 
      * @return uint16_t The network identifier
      */
    uint16_t GetNetworkId() const { return network_id_; }

    /**
      * @brief Gets the allocated data slots
      * 
      * @return uint8_t Number of allocated data slots
      */
    uint8_t GetAllocatedSlots() const { return allocated_slots_; }

    /**
      * @brief Gets the response status
      * 
      * @return ResponseStatus The response status code
      */
    ResponseStatus GetStatus() const { return status_; }

    /**
      * @brief Sets the join response specific information
      * 
      * @param network_id Network identifier
      * @param allocated_slots Number of allocated data slots
      * @param status Response status code
      * @return Result Success if setting succeeded, error code otherwise
      */
    Result SetJoinResponseInfo(uint16_t network_id, uint8_t allocated_slots,
                               ResponseStatus status);

    /**
      * @brief Serializes the header to a byte serializer
      * 
      * Extends the base header serialization with join response specific fields.
      * 
      * @param serializer Serializer to write the header to
      * @return Result Success if serialization succeeded, error code otherwise
      */
    Result Serialize(utils::ByteSerializer& serializer) const override;

    /**
      * @brief Deserializes a join response header from a byte deserializer
      * 
      * @param deserializer Deserializer containing the header data
      * @return std::optional<JoinResponseHeader> Deserialized header if successful,
      *         std::nullopt otherwise
      */
    static std::optional<JoinResponseHeader> Deserialize(
        utils::ByteDeserializer& deserializer);

    /**
      * @brief Calculates the size of the join response specific header extension
      * 
      * @return size_t Size of the join response header fields in bytes
      */
    static constexpr size_t JoinResponseFieldsSize() {
        return sizeof(uint16_t) +  // Network ID
               sizeof(uint8_t) +   // Allocated slots
               sizeof(uint8_t);    // Status
    }

    /**
      * @brief Gets the total size of this header type
      * 
      * @return size_t Size of the header in bytes (base + join response specific fields)
      */
    size_t GetSize() const override {
        return BaseHeader::Size() + JoinResponseFieldsSize();
    }

   private:
    uint16_t network_id_ = 0;           ///< Network identifier
    uint8_t allocated_slots_ = 0;       ///< Number of allocated data slots
    ResponseStatus status_ = ACCEPTED;  ///< Response status code
};

}  // namespace loramesher