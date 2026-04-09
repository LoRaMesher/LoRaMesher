/**
 * @file nm_claim_message.hpp
 * @brief NM election claim message definition
 */

#pragma once

#include "nm_claim_header.hpp"
#include "types/messages/base_message.hpp"
#include "utils/byte_operations.h"
#include "utils/logger.hpp"

namespace loramesher {

/**
 * @brief Message broadcast during NM election to claim the NM role.
 *
 * Sent via DISCOVERY_TX slot so it reaches all neighbors even when
 * the TDMA schedule is broken.  Nodes with lower election_priority win.
 */
class NMClaimMessage : public IConvertibleToBaseMessage {
   public:
    /**
     * @brief Creates an NMClaimMessage
     *
     * @param src Source (claiming) node address
     * @param priority Election priority (lower = higher priority)
     * @param battery_level Battery level (0-100%)
     * @param network_node_count Known network size
     * @param network_id Stable network identifier
     * @return std::optional<NMClaimMessage>
     */
    static std::optional<NMClaimMessage> Create(AddressType src,
                                                uint8_t priority,
                                                uint8_t battery_level,
                                                uint8_t network_node_count,
                                                uint16_t network_id) {
        NMClaimHeader header(kBroadcastAddress,  // broadcast
                             src, priority, battery_level, network_node_count,
                             network_id);
        return NMClaimMessage(header);
    }

    /**
     * @brief Deserializes an NMClaimMessage from raw bytes
     */
    /**
     * @brief Creates an NMClaimMessage directly from a BaseMessage
     */
    static std::optional<NMClaimMessage> CreateFromBaseMessage(
        const BaseMessage& message) {
        if (message.GetType() != MessageType::NM_CLAIM) {
            LOG_ERROR("Invalid message type for NMClaimMessage: %d",
                      static_cast<int>(message.GetType()));
            return std::nullopt;
        }
        auto payload = message.GetPayload();
        if (payload.size() < NMClaimHeader::NMClaimFieldsSize()) {
            LOG_ERROR("Payload too small for NM claim fields: %zu < %zu",
                      payload.size(), NMClaimHeader::NMClaimFieldsSize());
            return std::nullopt;
        }
        utils::ByteDeserializer deserializer(payload);
        auto priority = deserializer.ReadUint8();
        auto battery_level = deserializer.ReadUint8();
        auto network_node_count = deserializer.ReadUint8();
        auto network_id = deserializer.ReadUint16();
        if (!priority || !battery_level || !network_node_count || !network_id) {
            LOG_ERROR("Failed to read NM claim payload fields");
            return std::nullopt;
        }
        NMClaimHeader header(message.GetDestination(), message.GetSource(),
                             *priority, *battery_level, *network_node_count,
                             *network_id);
        return NMClaimMessage(header);
    }

    /**
     * @brief Deserializes an NMClaimMessage from raw bytes
     */
    static std::optional<NMClaimMessage> CreateFromSerialized(
        const std::vector<uint8_t>& data) {
        static const size_t kMinSize =
            BaseHeader::Size() + NMClaimHeader::NMClaimFieldsSize();
        if (data.size() < kMinSize) {
            LOG_ERROR("Data too small for NMClaimMessage: %zu < %zu",
                      data.size(), kMinSize);
            return std::nullopt;
        }
        utils::ByteDeserializer deserializer(data);
        auto header = NMClaimHeader::Deserialize(deserializer);
        if (!header)
            return std::nullopt;
        return NMClaimMessage(*header);
    }

    uint8_t GetPriority() const { return header_.GetPriority(); }

    uint8_t GetBatteryLevel() const { return header_.GetBatteryLevel(); }

    uint8_t GetNetworkNodeCount() const {
        return header_.GetNetworkNodeCount();
    }

    uint16_t GetNetworkId() const { return header_.GetNetworkId(); }

    AddressType GetSource() const { return header_.GetSource(); }

    const NMClaimHeader& GetHeader() const { return header_; }

    size_t GetTotalSize() const { return header_.GetSize(); }

    BaseMessage ToBaseMessage() const override {
        size_t payload_size = NMClaimHeader::NMClaimFieldsSize();
        std::array<uint8_t, BaseMessage::kMaxPayloadSize> buf{};
        utils::ByteSerializer serializer(buf.data(), payload_size);
        serializer.WriteUint8(header_.GetPriority());
        serializer.WriteUint8(header_.GetBatteryLevel());
        serializer.WriteUint8(header_.GetNetworkNodeCount());
        serializer.WriteUint16(header_.GetNetworkId());
        return BaseMessage(
            header_.GetDestination(), header_.GetSource(),
            MessageType::NM_CLAIM,
            std::span<const uint8_t>(buf.data(), serializer.getOffset()));
    }

    std::optional<std::vector<uint8_t>> Serialize() const override {
        std::vector<uint8_t> out(GetTotalSize());
        utils::ByteSerializer serializer(out);
        auto result = header_.Serialize(serializer);
        if (!result.IsSuccess())
            return std::nullopt;
        return out;
    }

   private:
    explicit NMClaimMessage(const NMClaimHeader& header) : header_(header) {}

    NMClaimHeader header_;
};

}  // namespace loramesher
