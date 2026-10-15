/**
 * @file join_request_header.hpp
 * @brief Header definition for network join request messages
 */

#pragma once

#include "types/messages/base_header.hpp"

namespace loramesher {

/**
  * @brief Header for JOIN_REQUEST messages
  *
  * Extends BaseHeader with join request specific fields:
  * requested data slots, routing, and sponsor information.
  */
class JoinRequestHeader : public BaseHeader {
   public:
    /**
      * @brief Default constructor
      */
    JoinRequestHeader() = default;

    /**
      * @brief Constructor with all fields
      *
      * @param dest Destination address (typically broadcast or network manager)
      * @param src Source address
      * @param requested_slots Number of data slots requested
      * @param next_hop Next hop for message forwarding (0 for direct)
      * @param additional_info_size To store the payload size in the base message
      * @param sponsor_address Sponsor node address (0 for no sponsor)
      * @param hop_count Number of hops from joining node (0 for direct)
      */
    JoinRequestHeader(AddressType dest, AddressType src,
                      uint8_t requested_slots, AddressType next_hop = 0,
                      uint8_t additional_info_size = 0,
                      AddressType sponsor_address = 0, uint8_t hop_count = 0);

    /**
      * @brief Gets the requested data slots
      * 
      * @return uint8_t Requested number of data slots
      */
    uint8_t GetRequestedSlots() const { return requested_slots_; }

    /**
      * @brief Gets the next hop for message forwarding
      *
      * @return AddressType Next hop address (0 for direct routing)
      */
    AddressType GetNextHop() const { return next_hop_; }

    /**
      * @brief Gets the sponsor address
      *
      * @return AddressType Sponsor address (0 for no sponsor)
      */
    AddressType GetSponsorAddress() const { return sponsor_address_; }

    /**
      * @brief Gets the hop count from joining node
      *
      * @return uint8_t Number of hops from joining node
      */
    uint8_t GetHopCount() const { return hop_count_; }

    /**
      * @brief Increments hop count for forwarding
      */
    void IncrementHopCount() { hop_count_++; }

    /**
      * @brief Sets the join request specific information
      *
      * @param requested_slots Number of data slots requested
      * @return Result Success if setting succeeded, error code otherwise
      */
    Result SetJoinRequestInfo(uint8_t requested_slots);

    /**
     * @brief Sets the requested data slots
     *
     * @param requested_slots Number of data slots requested
     * @return Result Success if setting succeeded, error code otherwise
     */
    Result SetRequestedSlots(uint8_t requested_slots);

    /**
     * @brief Sets the sponsor address
     *
     * @param sponsor_address Sponsor node address (0 for no sponsor)
     * @return Result Success if setting succeeded, error code otherwise
     */
    Result SetSponsorAddress(AddressType sponsor_address);

    /**
      * @brief Serializes the header to a byte serializer
      * 
      * Extends the base header serialization with join request specific fields.
      * 
      * @param serializer Serializer to write the header to
      * @return Result Success if serialization succeeded, error code otherwise
      */
    Result Serialize(utils::ByteSerializer& serializer) const override;

    /**
      * @brief Deserializes a join request header from a byte deserializer
      * 
      * @param deserializer Deserializer containing the header data
      * @return std::optional<JoinRequestHeader> Deserialized header if successful,
      *         std::nullopt otherwise
      */
    static std::optional<JoinRequestHeader> Deserialize(
        utils::ByteDeserializer& deserializer);

    /**
      * @brief Calculates the size of the join request specific header extension
      *
      * @return size_t Size of the join request header fields in bytes
      */
    static constexpr size_t JoinRequestFieldsSize() {
        return sizeof(uint8_t) +      // Requested slots
               sizeof(AddressType) +  // Next hop
               sizeof(AddressType) +  // Sponsor address
               sizeof(uint8_t);       // Hop count
    }

    /**
      * @brief Gets the total size of this header type
      * 
      * @return size_t Size of the header in bytes (base + join request specific fields)
      */
    size_t GetSize() const override {
        return BaseHeader::Size() + JoinRequestFieldsSize();
    }

   private:
    uint8_t requested_slots_ = 1;  ///< Requested number of data slots
    AddressType next_hop_ = 0;     ///< Next hop for message forwarding
    AddressType sponsor_address_ =
        0;                   ///< Sponsor node address (0 = no sponsor)
    uint8_t hop_count_ = 0;  ///< Hops from joining node
};

}  // namespace loramesher