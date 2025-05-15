/**
 * @file routing_table_entry.hpp
 * @brief Definition of routing table entry for serialization
 */

#pragma once

#include <cstdint>
#include "types/error_codes/result.hpp"
#include "types/messages/base_header.hpp"

namespace loramesher {

/**
  * @brief Structure representing a routing table entry for serialization
  * 
  * Contains essential information about a route that can be shared with 
  * other nodes in the network.
  */
struct RoutingTableEntry {
    AddressType destination;  ///< Destination address
    uint8_t hop_count;        ///< Number of hops to destination
    uint8_t link_quality;     ///< Link quality metric (0-100%)
    uint8_t allocated_slots;  ///< Number of data slots allocated to this node

    /**
   * @brief Constructor with all fields
   * 
   * @param dest Destination address
   * @param hops Hop count to destination
   * @param quality Link quality metric
   * @param slots Allocated data slots
   */
    RoutingTableEntry(AddressType dest, uint8_t hops, uint8_t quality,
                      uint8_t slots)
        : destination(dest),
          hop_count(hops),
          link_quality(quality),
          allocated_slots(slots) {}

    /**
   * @brief Default constructor
   */
    RoutingTableEntry() = default;

    /**
   * @brief Size of an entry in bytes
   * 
   * @return size_t Size of the entry
   */
    static constexpr size_t Size() {
        return sizeof(AddressType) +  // Destination address
               sizeof(uint8_t) +      // Hop count
               sizeof(uint8_t) +      // Link quality
               sizeof(uint8_t);       // Allocated slots
    }

    /**
   * @brief Serialize the entry to a byte serializer
   * 
   * @param serializer The serializer to write to
   * @return Result Success if serialization succeeded
   */
    Result Serialize(utils::ByteSerializer& serializer) const {
        serializer.WriteUint16(destination);
        serializer.WriteUint8(hop_count);
        serializer.WriteUint8(link_quality);
        serializer.WriteUint8(allocated_slots);
        return Result::Success();
    }

    /**
   * @brief Deserialize an entry from a byte deserializer
   * 
   * @param deserializer The deserializer to read from
   * @return std::optional<RoutingTableEntry> The entry if successful, nullopt otherwise
   */
    static std::optional<RoutingTableEntry> Deserialize(
        utils::ByteDeserializer& deserializer) {

        auto dest = deserializer.ReadUint16();
        auto hops = deserializer.ReadUint8();
        auto quality = deserializer.ReadUint8();
        auto slots = deserializer.ReadUint8();

        if (!dest || !hops || !quality || !slots) {
            return std::nullopt;
        }

        return RoutingTableEntry(*dest, *hops, *quality, *slots);
    }
};

}  // namespace loramesher