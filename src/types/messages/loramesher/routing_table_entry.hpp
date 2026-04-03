/**
 * @file routing_table_entry.hpp
 * @brief Definition of routing table entry for serialization
 */

#pragma once

#include <cstdint>
#include "types/error_codes/result.hpp"
#include "types/messages/base_header.hpp"
#include "types/node_capabilities.hpp"

namespace loramesher {

/**
  * @brief Structure representing a routing table entry for serialization
  * 
  * Contains essential information about a route that can be shared with 
  * other nodes in the network.
  */
struct RoutingTableEntry {
    AddressType destination = 0;  ///< Destination address
    uint8_t hop_count = 0;        ///< Number of hops to destination
    uint8_t link_quality = 0;     ///< Link quality metric (0-255)
    uint8_t allocated_data_slots =
        0;                     ///< Number of data slots allocated to this node
    uint8_t capabilities = 0;  ///< Node capabilities bitmap
    uint8_t control_slot_index =
        0xFF;  ///< Assigned control slot index (0xFF = unassigned)
    uint8_t reception_quality =
        0;  ///< Sender's raw EWMA reception quality for direct neighbors
    AddressType next_hop =
        0;  ///< Sender's next_hop for this destination (loop prevention)

    /**
     * @brief Constructor with all fields
     *
     * @param dest Destination address
     * @param hops Hop count to destination
     * @param quality Link quality metric
     * @param data_slots Allocated data slots
     * @param caps Node capabilities bitmap
     * @param ctrl_slot_idx Control slot index (0xFF = unassigned)
     */
    RoutingTableEntry(AddressType dest, uint8_t hops, uint8_t quality,
                      uint8_t data_slots, uint8_t caps = 0,
                      uint8_t ctrl_slot_idx = 0xFF)
        : destination(dest),
          hop_count(hops),
          link_quality(quality),
          allocated_data_slots(data_slots),
          capabilities(caps),
          control_slot_index(ctrl_slot_idx) {}

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
               sizeof(uint8_t) +      // Allocated slots
               sizeof(uint8_t) +      // Capabilities
               sizeof(uint8_t) +      // Control slot index
               sizeof(uint8_t) +      // Reception quality
               sizeof(AddressType);   // Next hop (loop prevention)
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
        serializer.WriteUint8(allocated_data_slots);
        serializer.WriteUint8(capabilities);
        serializer.WriteUint8(control_slot_index);
        serializer.WriteUint8(reception_quality);
        serializer.WriteUint16(next_hop);
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
        auto data_slots = deserializer.ReadUint8();
        auto caps = deserializer.ReadUint8();
        auto ctrl_slot = deserializer.ReadUint8();
        auto rx_quality = deserializer.ReadUint8();
        auto nh = deserializer.ReadUint16();

        if (!dest || !hops || !quality || !data_slots || !caps || !ctrl_slot ||
            !rx_quality || !nh) {
            return std::nullopt;
        }

        RoutingTableEntry entry(*dest, *hops, *quality, *data_slots, *caps,
                                *ctrl_slot);
        entry.reception_quality = *rx_quality;
        entry.next_hop = *nh;
        return entry;
    }
};

}  // namespace loramesher