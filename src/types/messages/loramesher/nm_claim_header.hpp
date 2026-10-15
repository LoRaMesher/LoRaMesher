/**
 * @file nm_claim_header.hpp
 * @brief Header definition for NM election claim messages
 */

#pragma once

#include "types/messages/base_header.hpp"

namespace loramesher {

/**
 * @brief Header for NM_CLAIM messages
 *
 * Broadcast during NM election to claim the Network Manager role.
 * Nodes with lower election_priority_ win ties.
 */
class NMClaimHeader : public BaseHeader {
   public:
    NMClaimHeader() = default;

    /**
     * @brief Full constructor
     *
     * @param dest Destination address (typically broadcast 0xFFFF)
     * @param src Source address (claiming node)
     * @param priority Election priority (lower = higher priority)
     * @param network_node_count Number of nodes claimant knows
     * @param network_id Stable network identifier
     */
    NMClaimHeader(AddressType dest, AddressType src, uint8_t priority,
                  uint8_t network_node_count, uint16_t network_id)
        : BaseHeader(dest, src, MessageType::NM_CLAIM,
                     static_cast<uint8_t>(NMClaimFieldsSize())),
          election_priority_(priority),
          network_node_count_(network_node_count),
          network_id_(network_id) {}

    uint8_t GetPriority() const { return election_priority_; }

    uint8_t GetNetworkNodeCount() const { return network_node_count_; }

    uint16_t GetNetworkId() const { return network_id_; }

    /**
     * @brief Serializes the header
     */
    Result Serialize(utils::ByteSerializer& serializer) const override {
        auto result = BaseHeader::Serialize(serializer);
        if (!result)
            return result;
        serializer.WriteUint8(election_priority_);
        serializer.WriteUint8(network_node_count_);
        serializer.WriteUint16(network_id_);
        return Result::Success();
    }

    /**
     * @brief Deserializes a NMClaimHeader from bytes
     */
    static std::optional<NMClaimHeader> Deserialize(
        utils::ByteDeserializer& deserializer) {
        auto base = BaseHeader::Deserialize(deserializer);
        if (!base)
            return std::nullopt;

        auto priority = deserializer.ReadUint8();
        auto node_count = deserializer.ReadUint8();
        auto network_id = deserializer.ReadUint16();

        if (!priority || !node_count || !network_id) {
            return std::nullopt;
        }

        NMClaimHeader header;
        static_cast<BaseHeader&>(header) = *base;
        header.election_priority_ = *priority;
        header.network_node_count_ = *node_count;
        header.network_id_ = *network_id;
        return header;
    }

    /**
     * @brief Size of NM claim specific fields
     */
    static constexpr size_t NMClaimFieldsSize() {
        return sizeof(uint8_t) +  // election_priority
               sizeof(uint8_t) +  // network_node_count
               sizeof(uint16_t);  // network_id
    }

    size_t GetSize() const override {
        return BaseHeader::Size() + NMClaimFieldsSize();
    }

   private:
    uint8_t election_priority_ = 0xFF;  ///< Lower = higher priority
    uint8_t network_node_count_ = 1;    ///< Known network size
    uint16_t network_id_ = 0;           ///< Stable network identifier
};

}  // namespace loramesher
