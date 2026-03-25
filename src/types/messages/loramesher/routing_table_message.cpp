/**
 * @file routing_table_message.cpp
 * @brief Implementation of routing table message functionality
 */

#include "routing_table_message.hpp"

#include <algorithm>
#include "routing_table_entry.hpp"

namespace loramesher {

RoutingTableMessage::RoutingTableMessage(
    const RoutingTableHeader& header,
    std::span<const RoutingTableEntry> entries)
    : header_(header),
      entry_count_(static_cast<uint8_t>(
          std::min(entries.size(), static_cast<size_t>(kMaxRoutingEntries)))) {
    std::copy(entries.begin(), entries.begin() + entry_count_,
              entries_.begin());
}

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
    auto entries_span = routing_msg->GetEntries();
    entry_count_ = static_cast<uint8_t>(entries_span.size());
    std::copy(entries_span.begin(), entries_span.end(), entries_.begin());
}

std::optional<RoutingTableMessage> RoutingTableMessage::Create(
    AddressType dest, AddressType src, AddressType network_manager_addr,
    uint8_t table_version, const std::vector<RoutingTableEntry>& entries,
    uint8_t source_capabilities, uint8_t source_allocated_data_slots) {

    // Check if the number of entries fits
    if (entries.size() > kMaxRoutingEntries) {
        LOG_ERROR("Too many routing table entries: %zu (max %d)",
                  entries.size(), kMaxRoutingEntries);
        return std::nullopt;
    }

    // Create the header with the correct number of entries
    RoutingTableHeader header(dest, src, network_manager_addr, table_version,
                              static_cast<uint8_t>(entries.size()),
                              source_capabilities, source_allocated_data_slots);

    LOG_DEBUG(
        "Created routing table message "
        "src: 0x%04X, dest: 0x%04X, NM: 0x%04X, "
        "table v.: %d, entry count: %d, caps: 0x%02X, data_slots: %d",
        src, dest, network_manager_addr, table_version,
        static_cast<int>(entries.size()), source_capabilities,
        source_allocated_data_slots);

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
    uint8_t entry_count = header->GetEntryCount();
    if (entry_count > kMaxRoutingEntries) {
        LOG_ERROR("Entry count %d exceeds maximum %d", entry_count,
                  kMaxRoutingEntries);
        return std::nullopt;
    }

    std::array<RoutingTableEntry, kMaxRoutingEntries> entries{};

    // Deserialize each network node route entry
    for (uint8_t i = 0; i < entry_count; i++) {
        auto entry = RoutingTableEntry::Deserialize(deserializer);
        if (!entry) {
            LOG_ERROR("Failed to deserialize network node route %d", i);
            return std::nullopt;
        }
        entries[i] = *entry;
    }

    return RoutingTableMessage(*header, std::span<const RoutingTableEntry>(
                                            entries.data(), entry_count));
}

AddressType RoutingTableMessage::GetNetworkManager() const {
    return header_.GetNetworkManager();
}

uint8_t RoutingTableMessage::GetTableVersion() const {
    return header_.GetTableVersion();
}

std::span<const RoutingTableEntry> RoutingTableMessage::GetEntries() const {
    return {entries_.data(), entry_count_};
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

uint8_t RoutingTableMessage::GetSourceCapabilities() const {
    return header_.GetSourceCapabilities();
}

uint8_t RoutingTableMessage::GetSourceAllocatedDataSlots() const {
    return header_.GetSourceAllocatedDataSlots();
}

uint8_t RoutingTableMessage::GetLinkQualityFor(AddressType node_address) const {
    // Only return quality for direct-neighbor entries (hop_count == 1).
    // Multi-hop entries mean the sender knows us indirectly and cannot
    // hear our transmissions — returning their quality would mask
    // unidirectional links.
    for (uint8_t i = 0; i < entry_count_; ++i) {
        if (entries_[i].destination == node_address &&
            entries_[i].hop_count == 1) {
            return entries_[i].link_quality;
        }
    }

    // Node not found or only reachable via multi-hop
    return 0;
}

size_t RoutingTableMessage::GetTotalPayloadSize() const {
    // Base size: routing table fields + entries
    size_t size = header_.RoutingTableFieldsSize();
    size += RoutingTableEntry::Size() * entry_count_;

    return size;
}

Result RoutingTableMessage::SetLinkQualityFor(AddressType node_address,
                                              uint8_t link_quality) {
    // Find the entry and set the link quality
    for (uint8_t i = 0; i < entry_count_; ++i) {
        if (entries_[i].destination == node_address) {
            entries_[i].link_quality = link_quality;
            return Result::Success();
        }
    }

    return Result(LoraMesherErrorCode::kInvalidState,
                  "Node address not found in routing table");
}

BaseMessage RoutingTableMessage::ToBaseMessage() const {
    // Calculate payload size (only routing table fields + entries, no BaseHeader)
    size_t payload_size = RoutingTableHeader::RoutingTableFieldsSize();
    payload_size += entry_count_ * RoutingTableEntry::Size();

    if (payload_size > BaseMessage::kMaxPayloadSize) {
        LOG_ERROR("Routing table message payload too large: %zu > %zu",
                  payload_size, BaseMessage::kMaxPayloadSize);
        return BaseMessage(header_.GetDestination(), header_.GetSource(),
                           MessageType::ROUTE_TABLE, {});
    }

    std::array<uint8_t, BaseMessage::kMaxPayloadSize> payload_buf{};
    utils::ByteSerializer serializer(payload_buf.data(), payload_size);

    // Serialize only the routing table specific fields (not the BaseHeader part)
    serializer.WriteUint16(header_.GetNetworkManager());
    serializer.WriteUint8(header_.GetTableVersion());
    serializer.WriteUint8(header_.GetEntryCount());
    serializer.WriteUint8(header_.GetSourceCapabilities());
    serializer.WriteUint8(header_.GetSourceAllocatedDataSlots());

    // Serialize all network node routes
    for (uint8_t i = 0; i < entry_count_; ++i) {
        entries_[i].Serialize(serializer);
    }

    // Create the base message with the correct type and our payload
    return BaseMessage(
        header_.GetDestination(), header_.GetSource(), MessageType::ROUTE_TABLE,
        std::span<const uint8_t>(payload_buf.data(), serializer.getOffset()));
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
    for (uint8_t i = 0; i < entry_count_; ++i) {
        result = entries_[i].Serialize(serializer);
        if (!result.IsSuccess()) {
            LOG_ERROR("Failed to serialize network node route");
            return std::nullopt;
        }
    }

    return serialized;
}

}  // namespace loramesher