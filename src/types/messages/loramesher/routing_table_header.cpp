/**
 * @file routing_table_header.cpp
 * @brief Implementation of routing table header functionality
 */

#include "routing_table_header.hpp"
#include "routing_table_entry.hpp"

namespace loramesher {

RoutingTableHeader::RoutingTableHeader(
    AddressType dest, AddressType src, AddressType network_manager_addr,
    uint8_t table_version, uint8_t entry_count, uint8_t source_capabilities,
    uint8_t source_allocated_data_slots, uint8_t source_control_slot_index)
    : BaseHeader(
          dest, src, MessageType::ROUTE_TABLE,
          RoutingTableFieldsSize() + RoutingTableEntry::Size() * entry_count),
      network_manager_addr_(network_manager_addr),
      table_version_(table_version),
      entry_count_(entry_count),
      source_capabilities_(source_capabilities),
      source_allocated_data_slots_(source_allocated_data_slots),
      source_control_slot_index_(source_control_slot_index) {}

Result RoutingTableHeader::SetRoutingTableInfo(AddressType network_manager_addr,
                                               uint8_t table_version,
                                               uint8_t entry_count) {
    network_manager_addr_ = network_manager_addr;
    table_version_ = table_version;
    entry_count_ = entry_count;

    return Result::Success();
}

Result RoutingTableHeader::Serialize(utils::ByteSerializer& serializer) const {
    // First serialize the base header fields
    Result result = BaseHeader::Serialize(serializer);
    if (!result.IsSuccess()) {
        return result;
    }

    // Then serialize routing table specific fields
    serializer.WriteUint16(network_manager_addr_);
    serializer.WriteUint8(table_version_);
    serializer.WriteUint8(entry_count_);
    serializer.WriteUint8(source_capabilities_);
    serializer.WriteUint8(source_allocated_data_slots_);
    serializer.WriteUint8(source_control_slot_index_);

    return Result::Success();
}

std::optional<RoutingTableHeader> RoutingTableHeader::Deserialize(
    utils::ByteDeserializer& deserializer) {

    // First deserialize the base header
    auto base_header = BaseHeader::Deserialize(deserializer);
    if (!base_header) {
        LOG_ERROR("Failed to deserialize base header");
        return std::nullopt;
    }

    // Verify this is a routing table message
    if (base_header->GetType() != MessageType::ROUTE_TABLE) {
        LOG_ERROR("Wrong message type for routing table header: %d",
                  static_cast<int>(base_header->GetType()));
        return std::nullopt;
    }

    // Deserialize routing table specific fields
    auto network_id = deserializer.ReadUint16();
    auto table_version = deserializer.ReadUint8();
    auto entry_count = deserializer.ReadUint8();
    auto source_capabilities = deserializer.ReadUint8();
    auto source_allocated_data_slots = deserializer.ReadUint8();
    auto source_control_slot_index = deserializer.ReadUint8();

    if (!network_id.has_value() || !table_version.has_value() ||
        !entry_count.has_value() || !source_capabilities.has_value() ||
        !source_allocated_data_slots.has_value() ||
        !source_control_slot_index.has_value()) {
        LOG_ERROR("Failed to deserialize routing table header fields");
        return std::nullopt;
    }

    // Create and return the routing table header
    RoutingTableHeader header(
        base_header->GetDestination(), base_header->GetSource(), *network_id,
        *table_version, *entry_count, *source_capabilities,
        *source_allocated_data_slots, *source_control_slot_index);

    return header;
}

}  // namespace loramesher