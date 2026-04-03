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

std::optional<RoutingTableMessage> RoutingTableMessage::CreateFromBaseMessage(
    const BaseMessage& message) {
    if (message.GetType() != MessageType::ROUTE_TABLE) {
        LOG_ERROR("Invalid message type for RoutingTableMessage: %d",
                  static_cast<int>(message.GetType()));
        return std::nullopt;
    }

    auto payload = message.GetPayload();
    if (payload.size() < RoutingTableHeader::RoutingTableFieldsSize()) {
        LOG_ERROR("Payload too small for routing table fields: %zu < %zu",
                  payload.size(), RoutingTableHeader::RoutingTableFieldsSize());
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(payload);

    auto network_manager = deserializer.ReadUint16();
    auto table_version = deserializer.ReadUint8();
    auto entry_count_opt = deserializer.ReadUint8();
    auto source_capabilities = deserializer.ReadUint8();
    auto source_allocated_data_slots = deserializer.ReadUint8();

    if (!network_manager || !table_version || !entry_count_opt ||
        !source_capabilities || !source_allocated_data_slots) {
        LOG_ERROR("Failed to read routing table header fields");
        return std::nullopt;
    }

    uint8_t entry_count = *entry_count_opt;
    if (entry_count > kMaxRoutingEntries) {
        LOG_ERROR("Entry count %d exceeds maximum %d", entry_count,
                  kMaxRoutingEntries);
        return std::nullopt;
    }

    size_t required_payload = RoutingTableHeader::RoutingTableFieldsSize() +
                              entry_count * RoutingTableEntry::Size();
    if (payload.size() < required_payload) {
        LOG_ERROR(
            "Truncated routing table: %zu payload bytes but need %zu for %d "
            "entries",
            payload.size(), required_payload, entry_count);
        return std::nullopt;
    }

    std::array<RoutingTableEntry, kMaxRoutingEntries> entries{};
    for (uint8_t i = 0; i < entry_count; i++) {
        auto entry = RoutingTableEntry::Deserialize(deserializer);
        if (!entry) {
            LOG_ERROR("Failed to deserialize network node route %d", i);
            return std::nullopt;
        }
        entries[i] = *entry;
    }

    RoutingTableHeader header(message.GetDestination(), message.GetSource(),
                              *network_manager, *table_version, entry_count,
                              *source_capabilities,
                              *source_allocated_data_slots);

    return RoutingTableMessage(header, std::span<const RoutingTableEntry>(
                                           entries.data(), entry_count));
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

    size_t required_size =
        kMinHeaderSize + entry_count * RoutingTableEntry::Size();
    if (data.size() < required_size) {
        LOG_ERROR(
            "Truncated routing table: %zu bytes but need %zu for %d entries",
            data.size(), required_size, entry_count);
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

uint8_t RoutingTableMessage::GetReceptionQualityFor(
    AddressType node_address) const {
    // Return reception_quality if the sender has direct reception data.
    // The reception_quality field is only non-zero when the sender has
    // actually received messages from that node (link_stats.messages_received > 0),
    // so it inherently proves direct radio contact regardless of hop_count.
    for (uint8_t i = 0; i < entry_count_; ++i) {
        if (entries_[i].destination == node_address &&
            entries_[i].reception_quality > 0) {
            return entries_[i].reception_quality;
        }
    }

    // Node not found or sender has no direct reception data
    return 0;
}

size_t RoutingTableMessage::GetTotalPayloadSize() const {
    // Base size: routing table fields + entries
    size_t size = header_.RoutingTableFieldsSize();
    size += RoutingTableEntry::Size() * entry_count_;

    return size;
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