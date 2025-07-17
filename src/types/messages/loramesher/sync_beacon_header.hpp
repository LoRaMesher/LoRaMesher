/**
 * @file sync_beacon_header.hpp
 * @brief Header definition for multi-hop synchronization beacon messages
 */

#pragma once

#include "types/messages/base_header.hpp"

namespace loramesher {

/**
 * @brief Header for SYNC_BEACON messages
 * 
 * Extends BaseHeader with multi-hop synchronization fields including timing,
 * forwarding information, and network topology data for collision-free
 * sync beacon propagation across mesh networks.
 */
class SyncBeaconHeader : public BaseHeader {
   public:
    /**
     * @brief Default constructor
     */
    SyncBeaconHeader() = default;

    /**
     * @brief Constructor with core sync fields (optimized)
     * 
     * @param dest Destination address (typically broadcast 0xFFFF)
     * @param src Source address (current transmitter)
     * @param network_id Network identifier
     * @param total_slots Number of slots in complete superframe
     * @param slot_duration_ms Individual slot duration in milliseconds
     * @param network_manager Address of the network manager
     */
    SyncBeaconHeader(AddressType dest, AddressType src, uint16_t network_id,
                     uint8_t total_slots, uint16_t slot_duration_ms,
                     AddressType network_manager);

    /**
     * @brief Full constructor with all multi-hop fields (optimized)
     * 
     * @param dest Destination address (typically broadcast 0xFFFF)
     * @param src Source address (current transmitter)
     * @param network_id Network identifier
     * @param total_slots Number of slots in complete superframe
     * @param slot_duration_ms Individual slot duration in milliseconds
     * @param network_manager Address of the network manager
     * @param hop_count Hops from Network Manager
     * @param propagation_delay_ms Accumulated forwarding delay
     * @param max_hops Network diameter limit
     */
    SyncBeaconHeader(AddressType dest, AddressType src, uint16_t network_id,
                     uint8_t total_slots, uint16_t slot_duration_ms,
                     AddressType network_manager, uint8_t hop_count,
                     uint32_t propagation_delay_ms, uint8_t max_hops);

    // Core synchronization field getters (optimized)
    uint16_t GetNetworkId() const { return network_id_; }

    uint8_t GetTotalSlots() const { return total_slots_; }

    uint16_t GetSlotDuration() const { return slot_duration_ms_; }

    AddressType GetNetworkManager() const { return network_manager_; }

    // Multi-hop forwarding field getters (optimized)
    uint8_t GetHopCount() const { return hop_count_; }

    uint32_t GetPropagationDelay() const { return propagation_delay_ms_; }

    uint8_t GetMaxHops() const { return max_hops_; }

    // Calculated/derived field getters
    uint16_t GetSuperframeDuration() const {
        return total_slots_ * slot_duration_ms_;
    }

    /**
     * @brief Sets the core synchronization information (optimized)
     * 
     * @param network_id Network identifier
     * @param total_slots Number of slots in superframe
     * @param slot_duration_ms Individual slot duration
     * @return Result Success if setting succeeded, error otherwise
     */
    Result SetSyncInfo(uint16_t network_id, uint8_t total_slots,
                       uint16_t slot_duration_ms);

    /**
     * @brief Sets the multi-hop forwarding information (optimized)
     * 
     * @param hop_count Hops from Network Manager
     * @param propagation_delay_ms Accumulated forwarding delay
     * @param max_hops Network diameter limit
     * @return Result Success if setting succeeded, error otherwise
     */
    Result SetForwardingInfo(uint8_t hop_count, uint32_t propagation_delay_ms,
                             uint8_t max_hops);

    /**
     * @brief Adds propagation delay for forwarding
     * 
     * @param additional_delay_ms Delay to add (processing + transmission time)
     */
    void AddPropagationDelay(uint32_t additional_delay_ms) {
        propagation_delay_ms_ += additional_delay_ms;
    }

    /**
     * @brief Increments hop count for forwarding
     */
    void IncrementHopCount() { hop_count_++; }

    /**
     * @brief Checks if this beacon should be forwarded by the given node
     * 
     * @param node_hop_count The receiving node's hop distance from Network Manager
     * @return bool True if the node should forward this beacon
     */
    bool ShouldBeForwardedBy(uint8_t node_hop_count) const {
        return (node_hop_count == hop_count_ + 1) && (hop_count_ < max_hops_);
    }

    /**
     * @brief Creates a forwarded version of this beacon for the next hop
     * 
     * @param forwarding_node Address of the node doing the forwarding
     * @param processing_delay Processing and transmission delay to add
     * @param guard_time_ms Guard time to add to propagation delay
     * @return SyncBeaconHeader Beacon prepared for forwarding
     */
    SyncBeaconHeader CreateForwardedBeacon(AddressType forwarding_node,
                                           uint32_t processing_delay,
                                           uint32_t guard_time_ms) const;

    /**
     * @brief Serializes the header to a byte serializer
     * 
     * @param serializer Serializer to write the header to
     * @return Result Success if serialization succeeded, error otherwise
     */
    Result Serialize(utils::ByteSerializer& serializer) const override;

    /**
     * @brief Deserializes a sync beacon header from a byte deserializer
     * 
     * @param deserializer Deserializer containing the header data
     * @return std::optional<SyncBeaconHeader> Deserialized header if successful,
     *         std::nullopt otherwise
     */
    static std::optional<SyncBeaconHeader> Deserialize(
        utils::ByteDeserializer& deserializer);

    /**
     * @brief Calculates the size of the sync beacon specific header extension
     * 
     * @return size_t Size of the sync beacon header fields in bytes
     */
    static constexpr size_t SyncBeaconFieldsSize() {
        return sizeof(uint16_t) +     // network_id
               sizeof(uint8_t) +      // total_slots
               sizeof(uint16_t) +     // slot_duration_ms
               sizeof(AddressType) +  // network_manager
               sizeof(uint8_t) +      // hop_count
               sizeof(uint32_t) +     // propagation_delay_ms
               sizeof(uint8_t);       // max_hops
    }

    /**
     * @brief Gets the total size of this header type
     * 
     * @return size_t Size of the header in bytes (base + sync beacon fields)
     */
    size_t GetSize() const override {
        return BaseHeader::Size() + SyncBeaconFieldsSize();
    }

   private:
    // Core synchronization fields
    uint16_t network_id_ = 0;   ///< Network identifier
    uint8_t total_slots_ = 10;  ///< Number of slots in superframe
    uint16_t slot_duration_ms_ =
        1000;  ///< Individual slot duration (optimized from uint32_t)
    AddressType network_manager_ = 0;  ///< Address of the network manager

    // Multi-hop forwarding fields
    uint8_t hop_count_ = 0;              ///< Hops from Network Manager
    uint32_t propagation_delay_ms_ = 0;  ///< Accumulated forwarding delay
    uint8_t max_hops_ = 5;               ///< Network diameter limit
};

}  // namespace loramesher