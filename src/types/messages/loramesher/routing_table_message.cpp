/**
 * @file routing_table_message.cpp
 * @brief Implementation of routing table message functionality
 */

#include "routing_table_message.hpp"

namespace loramesher {

RoutingTableMessage::RoutingTableMessage(
    const RoutingTableHeader& header,
    const std::vector<RoutingTableEntry>& entries)
    : header_(header), entries_(entries) {}

std::optional<RoutingTableMessage> RoutingTableMessage::Create(
    AddressType dest, AddressType src, uint16_t network_id,
    uint8_t table_version, const std::vector<RoutingTableEntry>& entries) {

    // Check if the number of entries fits in a uint8_t
    if (entries.size() > UINT8_MAX) {
        LOG_ERROR("Too many routing table entries: %zu", entries.size());
        return std::nullopt;
    }

    // Create the header with the correct number of entries
    RoutingTableHeader header(dest, src, network_id, table_version,
                              static_cast<uint8_t>(entries.size()));

    return RoutingTableMessage(header, entries);
}

std::optional<RoutingTableMessage> RoutingTableMessage::CreateFromSerialized(
    const std::vector<uint8_t>& data) {

    static constexpr size_t kMinHeaderSize =
        RoutingTableHeader::RoutingTableFieldsSize() + BaseHeader::Size();

    // Check minimum size requirements for header
    if (data.size() < kMinHeaderSize) {
        LOG_ERROR("Data too small for routing table message: %zu < %zu",
                  data.size(), kMinHeaderSize);
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(data);

    // Deserialize the header
    auto header = RoutingTableHeader::Deserialize(deserializer);
    if (!header) {
        LOG_ERROR("Failed to deserialize routing table header");
        return std::nullopt;
    }

    // Read the entries based on the count in the header
    std::vector<RoutingTableEntry> entries;
    uint8_t entry_count = header->GetEntryCount();

    // Validate that we have enough data for all entries
    size_t required_size =
        kMinHeaderSize + entry_count * RoutingTableEntry::Size();
    if (data.size() < required_size) {
        LOG_ERROR("Not enough data for all routing table entries: %zu < %zu",
                  data.size(), required_size);
        return std::nullopt;
    }

    // Deserialize each entry
    for (uint8_t i = 0; i < entry_count; i++) {
        auto entry = RoutingTableEntry::Deserialize(deserializer);
        if (!entry) {
            LOG_ERROR("Failed to deserialize routing table entry %d", i);
            return std::nullopt;
        }
        entries.push_back(*entry);
    }

    return RoutingTableMessage(*header, entries);
}

uint16_t RoutingTableMessage::GetNetworkId() const {
    return header_.GetNetworkId();
}

uint8_t RoutingTableMessage::GetTableVersion() const {
    return header_.GetTableVersion();
}

const std::vector<RoutingTableEntry>& RoutingTableMessage::GetEntries() const {
    return entries_;
}

AddressType RoutingTableMessage::GetSource() const {
    return header_.GetSource();
}

AddressType RoutingTableMessage::GetDestination() const {
    return header_.GetDestination();
}

const RoutingTableHeader& RoutingTableMessage::GetHeader() const {
    return header_;
}

size_t RoutingTableMessage::GetTotalSize() const {
    return header_.GetSize() + (entries_.size() * RoutingTableEntry::Size());
}

BaseMessage RoutingTableMessage::ToBaseMessage() const {
    // Calculate total payload size needed for:
    // - Network ID (2 bytes)
    // - Table version (1 byte)
    // - Entry count (1 byte)
    // - All routing entries (each entry size * number of entries)
    size_t payload_size = RoutingTableHeader::RoutingTableFieldsSize() +
                          (entries_.size() * RoutingTableEntry::Size());
    std::vector<uint8_t> payload(payload_size);

    utils::ByteSerializer serializer(payload);

    // Serialize the header-specific fields into the payload
    serializer.WriteUint16(header_.GetNetworkId());
    serializer.WriteUint8(header_.GetTableVersion());
    serializer.WriteUint8(static_cast<uint8_t>(entries_.size()));

    // Serialize all routing entries
    for (const auto& entry : entries_) {
        entry.Serialize(serializer);
    }

    // Create the base message with our serialized content as payload
    auto base_message =
        BaseMessage::Create(header_.GetDestination(), header_.GetSource(),
                            MessageType::ROUTE_TABLE, payload);

    if (!base_message.has_value()) {
        LOG_ERROR("Failed to create base message from routing table message");
        // Return an empty message as fallback
        return BaseMessage(header_.GetDestination(), header_.GetSource(),
                           MessageType::ROUTE_TABLE, {});
    }

    return base_message.value();
}

std::optional<std::vector<uint8_t>> RoutingTableMessage::Serialize() const {
    // Create a buffer for the serialized message
    std::vector<uint8_t> serialized(GetTotalSize());
    utils::ByteSerializer serializer(serialized);

    // Serialize the header
    Result result = header_.Serialize(serializer);
    if (!result.IsSuccess()) {
        LOG_ERROR("Failed to serialize routing table header");
        return std::nullopt;
    }

    // Serialize all entries
    for (const auto& entry : entries_) {
        result = entry.Serialize(serializer);
        if (!result.IsSuccess()) {
            LOG_ERROR("Failed to serialize routing table entry");
            return std::nullopt;
        }
    }

    return serialized;
}

}  // namespace loramesher