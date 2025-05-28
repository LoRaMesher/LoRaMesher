/**
 * @file routing_table_message.cpp
 * @brief Implementation of routing table message functionality
 */

#include "routing_table_message.hpp"
#include "routing_table_entry.hpp"

namespace loramesher {

using namespace types::protocols::lora_mesh;

RoutingTableMessage::RoutingTableMessage(
    const RoutingTableHeader& header,
    const std::vector<NetworkNodeRoute>& entries)
    : header_(header), entries_(entries) {}

std::optional<RoutingTableMessage> RoutingTableMessage::Create(
    AddressType dest, AddressType src, AddressType network_manager_addr,
    uint8_t table_version, const std::vector<NetworkNodeRoute>& entries) {

    // Check if the number of entries fits in a uint8_t
    if (entries.size() > UINT8_MAX) {
        LOG_ERROR("Too many routing table entries: %zu", entries.size());
        return std::nullopt;
    }

    // Create the header with the correct number of entries
    RoutingTableHeader header(dest, src, network_manager_addr, table_version,
                              static_cast<uint8_t>(entries.size()));

    return RoutingTableMessage(header, entries);
}

std::optional<RoutingTableMessage> RoutingTableMessage::CreateFromSerialized(
    const std::vector<uint8_t>& data) {

    // Calculate minimum size for header plus network manager address
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
    std::vector<NetworkNodeRoute> entries;
    uint8_t entry_count = header->GetEntryCount();

    // Deserialize each network node route entry
    for (uint8_t i = 0; i < entry_count; i++) {
        auto entry = NetworkNodeRoute::Deserialize(deserializer);
        if (!entry) {
            LOG_ERROR("Failed to deserialize network node route %d", i);
            return std::nullopt;
        }
        entries.push_back(*entry);
    }

    return RoutingTableMessage(*header, entries);
}

AddressType RoutingTableMessage::GetNetworkManager() const {
    return header_.GetNetworkManagerAddress();
}

uint8_t RoutingTableMessage::GetTableVersion() const {
    return header_.GetTableVersion();
}

const std::vector<NetworkNodeRoute>& RoutingTableMessage::GetEntries() const {
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

uint8_t RoutingTableMessage::GetLinkQualityFor(AddressType node_address) const {
    // Find the node entry with the specified address
    for (const auto& entry : entries_) {
        if (entry.address == node_address) {
            return entry.link_quality;
        }
    }

    // Node not found
    return 0;
}

size_t RoutingTableMessage::GetTotalSize() const {
    // Base size: header + network manager + entry count
    size_t size = header_.GetSize();

    // Add size of all network node route entries
    for (const auto& entry : entries_) {
        // For simplicity, we'll use a fixed size per entry
        // In a real implementation, you may need a more sophisticated calculation
        size += RoutingTableEntry::Size();
    }

    return size;
}

BaseMessage RoutingTableMessage::ToBaseMessage() const {
    // Calculate total payload size
    size_t payload_size = GetTotalSize();

    std::vector<uint8_t> payload(payload_size);
    utils::ByteSerializer serializer(payload);

    // Serialize the header-specific fields
    Result result = header_.Serialize(serializer);
    if (!result.IsSuccess()) {
        LOG_ERROR("Failed to serialize routing table header");
        return BaseMessage(header_.GetDestination(), header_.GetSource(),
                           MessageType::ROUTE_TABLE, {});
    }

    // Serialize all network node routes
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
            LOG_ERROR("Failed to serialize network node route");
            return std::nullopt;
        }
    }

    return serialized;
}

}  // namespace loramesher