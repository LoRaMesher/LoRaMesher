/**
 * @file sync_beacon_service.cpp
 * @brief Implementation of the sync-beacon transmit/forward path.
 */

#include "sync_beacon_service.hpp"

#include <algorithm>
#include <cstring>

#include "utils/logger.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

SyncBeaconService::SyncBeaconService(
    std::shared_ptr<ISuperframeService> superframe_service,
    std::shared_ptr<IMessageQueueService> message_queue_service, Host host)
    : superframe_service_(std::move(superframe_service)),
      message_queue_service_(std::move(message_queue_service)),
      host_(std::move(host)) {}

void SyncBeaconService::SetSyncBeaconPreSendCallback(BaseMessage& base_msg) {
    base_msg.SetPreSendCallback([this](BaseMessage& msg) {
        constexpr uint32_t kSerializationOverheadMs = 1;
        uint32_t actual_time =
            superframe_service_->GetTimeSinceSuperframeStart() +
            kSerializationOverheadMs;

        auto payload = msg.MutablePayload();
        constexpr size_t kOffset =
            SyncBeaconHeader::kPropagationDelayPayloadOffset;
        if (payload.size() < kOffset + sizeof(uint32_t)) {
            LOG_ERROR(
                "Pre-send callback: payload too small for propagation delay");
            return;
        }
        std::memcpy(payload.data() + kOffset, &actual_time, sizeof(uint32_t));

        LOG_DEBUG("Pre-send callback: updated propagation_delay to %u ms",
                  actual_time);
    });
}

Result SyncBeaconService::SendSyncBeacon(const Context& ctx) {
    // Only network manager can send original sync beacons
    if (!ctx.in_network_manager_state ||
        ctx.network_manager != ctx.node_address) {
        LOG_ERROR("Only network manager can send sync beacons");
        return Result::Error(LoraMesherErrorCode::kInvalidState);
    }

    if (!superframe_service_) {
        LOG_ERROR("Superframe service required for sync beacon");
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    // Get actual total slots from the slot table
    uint16_t total_slots = ctx.slot_count;
    if (total_slots == 0) {
        total_slots = 20;  // Fallback default
        LOG_WARNING("Slot table empty, using default total slots: %d",
                    total_slots);
    }

    // Create original sync beacon with placeholder propagation_delay (0)
    // The actual timing will be captured by the pre-send callback right before transmission
    auto sync_beacon_opt = SyncBeaconMessage::CreateOriginal(
        kBroadcastAddress,  // Broadcast destination
        ctx.node_address,   // Network manager as source
        ctx.network_id,     // Stable network identifier (survives NM elections)
        total_slots,        // Actual total slots from slot table
        static_cast<uint16_t>(superframe_service_->GetSlotDuration()),
        ctx.node_address,  // Network manager address
        0,                 // Placeholder - will be updated by callback
        std::min(
            static_cast<uint8_t>(ctx.current_network_depth),
            ctx.max_hops),  // Dynamic growth (depth+1) capped by configured limit
        ctx.allocated_control_slots);  // Authoritative node count for slot alignment

    if (!sync_beacon_opt.has_value()) {
        LOG_ERROR("Failed to create sync beacon message");
        return Result::Error(LoraMesherErrorCode::kConfigurationError);
    }

    // Convert to base message and queue for transmission
    BaseMessage base_msg = sync_beacon_opt.value().ToBaseMessage();

    // Set callback to update propagation_delay right before transmission
    SetSyncBeaconPreSendCallback(base_msg);

    auto base_msg_ptr = std::make_unique<BaseMessage>(std::move(base_msg));
    Result queue_result = message_queue_service_->AddMessageToQueue(
        types::protocols::lora_mesh::SlotAllocation::SlotType::SYNC_BEACON_TX,
        std::move(base_msg_ptr));
    if (!queue_result) {
        LOG_ERROR("Failed to queue sync beacon: %s",
                  queue_result.GetErrorMessage().c_str());
        return queue_result;
    }

    LOG_INFO("Queued sync beacon for transmission: %d total slots, %d max hops",
             total_slots, ctx.current_network_depth);
    return Result::Success();
}

Result SyncBeaconService::ForwardSyncBeacon(
    const SyncBeaconMessage& original_beacon, uint32_t processing_delay,
    const Context& ctx) {
    // Create forwarded beacon from the original
    auto forwarded_beacon_opt = original_beacon.CreateForwardedBeacon(
        ctx.node_address, processing_delay, ctx.guard_time_ms);

    if (!forwarded_beacon_opt.has_value()) {
        LOG_ERROR("Failed to create forwarded sync beacon");
        return Result::Error(LoraMesherErrorCode::kConfigurationError);
    }

    // Convert to base message and queue for transmission
    BaseMessage base_msg = forwarded_beacon_opt.value().ToBaseMessage();

    // Set pre-send callback to capture actual TX time including subslot wait.
    // CreateForwardedBeacon sets an initial propagation_delay estimate, but the
    // callback overwrites it with GetTimeSinceSuperframeStart() right before TX,
    // which naturally includes any subslot delay that has elapsed.
    SetSyncBeaconPreSendCallback(base_msg);

    // If listening was expanded after missed beacons, the designated TX slot
    // was demoted to RX. Now that we have a fresh beacon to forward and are
    // re-synced, restore the TX slot so the queued beacon can be transmitted.
    host_.restore_tx_slot();

    // Clear any stale beacon before queuing the fresh one
    message_queue_service_->ClearQueue(
        types::protocols::lora_mesh::SlotAllocation::SlotType::SYNC_BEACON_TX);

    auto base_msg_ptr = std::make_unique<BaseMessage>(std::move(base_msg));
    Result queue_result = message_queue_service_->AddMessageToQueue(
        types::protocols::lora_mesh::SlotAllocation::SlotType::SYNC_BEACON_TX,
        std::move(base_msg_ptr));
    if (!queue_result) {
        LOG_ERROR("Failed to queue forwarded sync beacon: %s",
                  queue_result.GetErrorMessage().c_str());
        return queue_result;
    }

    LOG_INFO("Queued forwarded sync beacon for transmission");

    LOG_INFO("Forwarded sync beacon, new hop count %d",
             forwarded_beacon_opt.value().GetHopCount());

    return Result::Success();
}

bool SyncBeaconService::ShouldForwardSyncBeacon(const SyncBeaconMessage& beacon,
                                                const Context& ctx) const {
    if (!ctx.in_normal_operation) {
        return false;
    }

    // Forward any beacon that hasn't exceeded max propagation distance.
    // The rate limiter in ProcessSyncBeacon ensures only the first beacon per
    // superframe is processed, so hop-layer filtering is unnecessary and
    // harmful for mobile nodes whose routing-table distance may be stale.
    bool should_forward = beacon.GetHopCount() < beacon.GetMaxHops();

    if (should_forward) {
        LOG_DEBUG("Will forward sync beacon: beacon hop %d, max_hops %d",
                  beacon.GetHopCount(), beacon.GetMaxHops());
    } else {
        LOG_DEBUG("Not forwarding: hop count %d reached max_hops %d",
                  beacon.GetHopCount(), beacon.GetMaxHops());
    }

    return should_forward;
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
