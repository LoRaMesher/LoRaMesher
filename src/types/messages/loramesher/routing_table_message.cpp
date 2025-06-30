/**
 * @file routing_table_message.cpp
 * @brief Implementation of routing table message functionality
 */

#include "routing_table_message.hpp"
#include "routing_table_entry.hpp"

namespace loramesher {

RoutingTableMessage::RoutingTableMessage(
    const RoutingTableHeader& header,
    const std::vector<RoutingTableEntry>& entries)
    : header_(header), entries_(entries) {}

RoutingTableMessage::RoutingTableMessage(const BaseMessage& message) {
    // Ensure the message type is correct
    if (message.GetType() != MessageType::ROUTE_TABLE) {
        LOG_ERROR("Invalid message type for RoutingTableMessage: %d",
                  static_cast<int>(message.GetType()));
        throw std::invalid_argument(
            "Invalid message type for RoutingTableMessage");
    }

    auto opt_serialized = message.Serialize();
    if (!opt_serialized) {
        LOG_ERROR("Failed to serialize routing message");
        throw std::runtime_error("Failed to serialize routing message");
    }

    auto routing_msg =
        RoutingTableMessage::CreateFromSerialized(*opt_serialized);
    if (!routing_msg) {
        LOG_ERROR("Failed to deserialize routing message");
        throw std::runtime_error("Failed to deserialize routing message");
    }

    // Set the header and entries from the deserialized message
    header_ = routing_msg->GetHeader();
    entries_ = routing_msg->GetEntries();
}

std::optional<RoutingTableMessage> RoutingTableMessage::Create(
    AddressType dest, AddressType src, AddressType network_manager_addr,
    uint8_t table_version, const std::vector<RoutingTableEntry>& entries) {

    // Check if the number of entries fits in a uint8_t
    if (entries.size() > UINT8_MAX) {
        LOG_ERROR("Too many routing table entries: %zu", entries.size());
        return std::nullopt;
    }

    // Create the header with the correct number of entries
    RoutingTableHeader header(dest, src, network_manager_addr, table_version,
                              static_cast<uint8_t>(entries.size()));

    LOG_DEBUG("Created routing table message with source: 0x%04X, "
              "destination: 0x%04X, network manager: 0x%04X, "
              "table version: %d, entry count: %d",
              src, dest, network_manager_addr, table_version,
              static_cast<int>(entries.size()));

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
    std::vector<RoutingTableEntry> entries;
    uint8_t entry_count = header->GetEntryCount();

    // Deserialize each network node route entry
    for (uint8_t i = 0; i < entry_count; i++) {
        auto entry = RoutingTableEntry::Deserialize(deserializer);
        if (!entry) {
            LOG_ERROR("Failed to deserialize network node route %d", i);
            return std::nullopt;
        }
        entries.push_back(*entry);
    }

    return RoutingTableMessage(*header, entries);
}

AddressType RoutingTableMessage::GetNetworkManager() const {
    return header_.GetNetworkManager();
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

uint8_t RoutingTableMessage::GetLinkQualityFor(AddressType node_address) const {
    // Find the node entry with the specified address
    for (const auto& entry : entries_) {
        if (entry.destination == node_address) {
            return entry.link_quality;
        }
    }

    // Node not found
    return 0;
}

size_t RoutingTableMessage::GetTotalPayloadSize() const {
    // Base size: header + network manager + entry count
    size_t size = header_.RoutingTableFieldsSize();

    // Add size of all network node route entries
    for (const auto& entry : entries_) {
        // For simplicity, we'll use a fixed size per entry
        // In a real implementation, you may need a more sophisticated calculation
        size += RoutingTableEntry::Size();
    }

    return size;
}

Result RoutingTableMessage::SetLinkQualityFor(AddressType node_address,
                                              uint8_t link_quality) {
    // Find the entry and set the link quality
    for (auto& entry : entries_) {
        if (entry.destination == node_address) {
            entry.link_quality = link_quality;
            return Result::Success();
        }
    }

    return Result(LoraMesherErrorCode::kInvalidState,
                  "Node address not found in routing table");
}

BaseMessage RoutingTableMessage::ToBaseMessage() const {
    // Calculate total message size
    size_t total_message_size = GetTotalPayloadSize() + BaseHeader::Size();
    if (total_message_size > BaseMessage::kMaxPayloadSize) {
        LOG_ERROR("Routing table message payload too large: %zu > %zu",
                  total_message_size, BaseMessage::kMaxPayloadSize);
        return BaseMessage(header_.GetDestination(), header_.GetSource(),
                           MessageType::ROUTE_TABLE, {});
    }

    std::vector<uint8_t> payload(total_message_size);
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
    auto base_message = BaseMessage::CreateFromSerialized(payload);
    if (!base_message.has_value()) {
        LOG_ERROR("Failed to create base message from routing table message");
        // Return an empty message as fallback
        return BaseMessage(header_.GetDestination(), header_.GetSource(),
                           MessageType::ROUTE_TABLE, {});
    }

    return base_message.value();
}

std::optional<std::vector<uint8_t>> RoutingTableMessage::Serialize() const {
    // Calculate total payload size
    size_t total_size = GetTotalPayloadSize() + BaseHeader::Size();
    if (total_size > BaseMessage::kMaxPayloadSize) {
        LOG_ERROR("Routing table message payload too large: %zu > %zu",
                  total_size, BaseMessage::kMaxPayloadSize);
        return std::nullopt;
    }

    // Create a buffer for the serialized message
    std::vector<uint8_t> serialized(total_size);
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