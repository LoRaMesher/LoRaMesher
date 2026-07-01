/**
 * @file sync_beacon_service.hpp
 * @brief Sync-beacon transmit/forward path extracted from NetworkService.
 *
 * Owns the cohesive, side-effect-free sync-beacon TX behaviour: building an
 * original beacon (Network Manager), forwarding a received beacon, deciding
 * whether to forward, and stamping the propagation-delay field right before
 * transmission. It mutates no coordinator state; the coordinator supplies a
 * read-only @ref Context snapshot and a single @ref Host callback
 * (restore_tx_slot). The already-dependency-injected superframe and
 * message-queue services are held directly.
 *
 * The RX orchestration (`ProcessSyncBeacon`) intentionally stays in
 * NetworkService: it mutates network parameters that routing/election/join also
 * own, so it is coordination rather than sync-beacon logic.
 */

#pragma once

#include <functional>
#include <memory>

#include "protocols/lora_mesh/interfaces/i_message_queue_service.hpp"
#include "protocols/lora_mesh/interfaces/i_superframe_service.hpp"
#include "types/error_codes/result.hpp"
#include "types/messages/base_header.hpp"
#include "types/messages/loramesher/sync_beacon_message.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Builds, forwards, and time-stamps sync beacons.
 *
 * Stateless with respect to coordinator data: every call takes a @ref Context
 * snapshot. Runs on the protocol task, so no internal synchronization.
 */
class SyncBeaconService {
   public:
    /// Read-only snapshot of coordinator state needed to (build|forward) a beacon.
    struct Context {
        AddressType node_address = 0;
        AddressType network_manager = 0;
        uint16_t network_id = 0;
        bool in_network_manager_state = false;  ///< state == NETWORK_MANAGER
        bool in_normal_operation = false;       ///< state == NORMAL_OPERATION
        uint8_t current_network_depth = 0;
        uint8_t max_hops = 0;                 ///< config_.max_hops
        uint16_t guard_time_ms = 0;           ///< config_.guard_time_ms
        uint16_t slot_count = 0;              ///< total slots in the superframe
        uint8_t allocated_control_slots = 0;  ///< advertised node count
    };

    /// Coordinator callbacks. Kept to genuine collaborator operations.
    struct Host {
        /// Restore the SYNC_BEACON_TX slot demoted by expanded listening.
        std::function<void()> restore_tx_slot;
    };

    SyncBeaconService(
        std::shared_ptr<ISuperframeService> superframe_service,
        std::shared_ptr<IMessageQueueService> message_queue_service, Host host);

    /// Build and queue an original sync beacon (Network Manager only).
    Result SendSyncBeacon(const Context& ctx);

    /// Build and queue a forwarded copy of a received beacon.
    Result ForwardSyncBeacon(const SyncBeaconMessage& original_beacon,
                             uint32_t processing_delay, const Context& ctx);

    /// True if the received beacon should be forwarded in this node's state.
    bool ShouldForwardSyncBeacon(const SyncBeaconMessage& beacon,
                                 const Context& ctx) const;

   private:
    /// Attach a pre-send callback that stamps propagation_delay just before TX.
    void SetSyncBeaconPreSendCallback(BaseMessage& base_msg);

    std::shared_ptr<ISuperframeService> superframe_service_;
    std::shared_ptr<IMessageQueueService> message_queue_service_;
    Host host_;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
