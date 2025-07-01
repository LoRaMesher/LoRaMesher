/**
 * @file sync_beacon_message.hpp
 * @brief Definition of multi-hop synchronization beacon message for mesh networking
 */

#pragma once

#include "sync_beacon_header.hpp"
#include "types/messages/base_message.hpp"
#include "utils/byte_operations.h"
#include "utils/logger.hpp"

namespace loramesher {

/**
 * @brief Message for multi-hop network synchronization
 * 
 * Implements the IConvertibleToBaseMessage interface to provide conversion
 * to the BaseMessage format for transmission. Uses SyncBeaconHeader for
 * comprehensive synchronization and forwarding information.
 */
class SyncBeaconMessage : public IConvertibleToBaseMessage {
   public:
    /**
     * @brief Creates a new sync beacon message (Network Manager originating)
     * 
     * @param dest Destination address (typically broadcast 0xFFFF)
     * @param src Source address (Network Manager)
     * @param network_id Network identifier
     * @param superframe_number Incremental superframe counter
     * @param superframe_duration_ms Total superframe time
     * @param total_slots Number of slots in superframe
     * @param slot_duration_ms Individual slot duration
     * @param original_timestamp_ms NM's transmission timestamp
     * @param max_hops Network diameter limit
     * @param sequence_number Loop prevention sequence number
     * @return std::optional<SyncBeaconMessage> Valid message if creation succeeded,
     *         std::nullopt otherwise
     */
    static std::optional<SyncBeaconMessage> CreateOriginal(
        AddressType dest, AddressType src, uint16_t network_id,
        uint32_t superframe_number, uint16_t superframe_duration_ms,
        uint8_t total_slots, uint32_t slot_duration_ms,
        uint32_t original_timestamp_ms, uint8_t max_hops,
        uint16_t sequence_number);

    /**
     * @brief Creates a forwarded sync beacon message
     * 
     * @param dest Destination address (typically broadcast 0xFFFF)
     * @param src Source address (forwarding node)
     * @param network_id Network identifier
     * @param superframe_number Incremental superframe counter
     * @param superframe_duration_ms Total superframe time
     * @param total_slots Number of slots in superframe
     * @param slot_duration_ms Individual slot duration
     * @param original_source Network Manager address
     * @param hop_count Hops from Network Manager
     * @param original_timestamp_ms NM's original transmission time
     * @param propagation_delay_ms Accumulated forwarding delay
     * @param max_hops Network diameter limit
     * @param sequence_number Loop prevention sequence number
     * @return std::optional<SyncBeaconMessage> Valid message if creation succeeded,
     *         std::nullopt otherwise
     */
    static std::optional<SyncBeaconMessage> CreateForwarded(
        AddressType dest, AddressType src, uint16_t network_id,
        uint32_t superframe_number, uint16_t superframe_duration_ms,
        uint8_t total_slots, uint32_t slot_duration_ms,
        AddressType original_source, uint8_t hop_count,
        uint32_t original_timestamp_ms, uint32_t propagation_delay_ms,
        uint8_t max_hops, uint16_t sequence_number);

    /**
     * @brief Creates a sync beacon message from serialized data
     * 
     * @param data Serialized message data
     * @return std::optional<SyncBeaconMessage> Deserialized message if successful,
     *         std::nullopt otherwise
     */
    static std::optional<SyncBeaconMessage> CreateFromSerialized(
        const std::vector<uint8_t>& data);

    // Core synchronization field getters
    uint16_t GetNetworkId() const;
    uint32_t GetSuperframeNumber() const;
    uint16_t GetSuperframeDuration() const;
    uint8_t GetTotalSlots() const;
    uint32_t GetSlotDuration() const;

    // Multi-hop forwarding field getters
    AddressType GetOriginalSource() const;
    uint8_t GetHopCount() const;
    uint32_t GetOriginalTimestamp() const;
    uint32_t GetPropagationDelay() const;
    uint8_t GetMaxHops() const;
    uint16_t GetSequenceNumber() const;

    /**
     * @brief Gets the source address (current transmitter)
     * 
     * @return AddressType The source address
     */
    AddressType GetSource() const;

    /**
     * @brief Gets the destination address
     * 
     * @return AddressType The destination address
     */
    AddressType GetDestination() const;

    /**
     * @brief Gets the sync beacon header
     * 
     * @return const SyncBeaconHeader& The sync beacon header
     */
    const SyncBeaconHeader& GetHeader() const;

    /**
     * @brief Gets the total size of the serialized message
     * 
     * @return size_t Total size in bytes
     */
    size_t GetTotalSize() const;

    /**
     * @brief Checks if this beacon should be forwarded by the given node
     * 
     * @param node_hop_count The receiving node's hop distance from Network Manager
     * @return bool True if the node should forward this beacon
     */
    bool ShouldBeForwardedBy(uint8_t node_hop_count) const;

    /**
     * @brief Creates a forwarded version of this beacon for the next hop
     * 
     * @param forwarding_node Address of the node doing the forwarding
     * @param processing_delay Processing and transmission delay to add
     * @return std::optional<SyncBeaconMessage> Forwarded beacon if successful,
     *         std::nullopt otherwise
     */
    std::optional<SyncBeaconMessage> CreateForwardedBeacon(
        AddressType forwarding_node, uint32_t processing_delay) const;

    /**
     * @brief Calculates the original Network Manager timing compensating for delays
     * 
     * @param reception_time When this beacon was received
     * @return uint32_t Estimated original Network Manager transmission time
     */
    uint32_t CalculateOriginalTiming(uint32_t reception_time) const;

    /**
     * @brief Checks if this beacon is from the original Network Manager
     * 
     * @return bool True if this is the original beacon (hop count 0)
     */
    bool IsOriginalBeacon() const;

    /**
     * @brief Converts to a BaseMessage for transmission
     * 
     * @return BaseMessage The converted base message
     */
    BaseMessage ToBaseMessage() const override;

    /**
     * @brief Serializes the complete message
     * 
     * @return std::optional<std::vector<uint8_t>> Serialized message if successful,
     *         std::nullopt otherwise
     */
    std::optional<std::vector<uint8_t>> Serialize() const override;

   private:
    /**
     * @brief Private constructor
     * 
     * @param header The sync beacon header
     */
    explicit SyncBeaconMessage(const SyncBeaconHeader& header);

    SyncBeaconHeader header_;  ///< Sync beacon message header
};

}  // namespace loramesher