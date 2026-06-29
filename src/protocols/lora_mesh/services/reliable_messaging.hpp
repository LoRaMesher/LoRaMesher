/**
 * @file reliable_messaging.hpp
 * @brief Group multicast + reliable-delivery subsystem extracted from NetworkService
 *
 * Owns the end-to-end reliable unicast/group delivery state machine
 * (reliability::ReliableDelivery), the seq->destination shadow table, group
 * (multicast) membership, and the acknowledgement-collection windows.
 * Constructed and owned by NetworkService, which delegates the corresponding
 * public API to it and supplies cross-cutting dependencies as Host closures.
 *
 * Threading: this component does not own a mutex. The membership operations
 * lock the coordinator's mutex (passed by reference) exactly as before; the
 * reliable/group paths run on the single protocol task like the rest of the
 * coordinator.
 */

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include "protocols/reliability/reliable_delivery.hpp"
#include "types/error_codes/result.hpp"
#include "types/messages/base_header.hpp"
#include "types/messages/base_message.hpp"
#include "types/messages/loramesher/data_message.hpp"
#include "types/protocols/lora_mesh/slot_allocation.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Group multicast and reliable-delivery subsystem for NetworkService
 */
class ReliableMessaging {
   public:
    /// Routing hop-count to a destination (>= 1; 1 when unknown).
    using HopsToDestFn = std::function<uint8_t(AddressType dest)>;
    /// Current superframe duration in milliseconds (0 when unavailable).
    using SuperframeDurationFn = std::function<uint32_t()>;

    /**
     * @brief Cross-cutting dependencies bound to the owning NetworkService
     */
    struct Host {
        AddressType node_address = 0;      ///< Local node address
        std::function<uint32_t()> now_ms;  ///< Monotonic millisecond clock
        /// Serialize+enqueue a BaseMessage onto a TX slot queue.
        std::function<Result(
            types::protocols::lora_mesh::SlotAllocation::SlotType,
            std::unique_ptr<BaseMessage>)>
            enqueue;
        /// Routing next-hop toward a destination (0 if none).
        std::function<AddressType(AddressType)> find_next_hop;
        /// Forward an ACK DataMessage toward its destination.
        std::function<Result(const DataMessage&)> forward_data_message;
        /// Allocate the next message sequence number.
        std::function<uint8_t()> next_seq;
        /// Record (source, seq) in the de-duplication cache.
        std::function<void(AddressType, uint8_t)> record_in_cache;
        /// True when the protocol is in NORMAL_OPERATION or NETWORK_MANAGER.
        std::function<bool()> in_operational_state;
        std::function<uint8_t()> max_hops;  ///< Configured max hop count
        std::function<uint16_t()>
            max_packet_size;        ///< Configured max packet size
        HopsToDestFn hops_to_dest;  ///< Routing hop-count lookup
        SuperframeDurationFn superframe_duration;  ///< Superframe duration (ms)
    };

    ReliableMessaging(std::mutex& mutex, Host host);

    // --- Group (multicast) membership ---

    Result JoinGroup(AddressType group);
    Result LeaveGroup(AddressType group);
    bool IsMemberOfGroup(AddressType group) const;
    std::vector<AddressType> GetGroups() const;

    // --- Reliable destination shadow table ---

    AddressType LookupReliableDest(uint8_t seq) const;
    void RecordReliableDest(uint8_t seq, AddressType dest);
    void ClearReliableDest(uint8_t seq);

    // --- Reliable unicast/group delivery ---

    /// Estimate a retransmit timeout (ms) from hop count and superframe duration.
    uint32_t ComputeReliableTimeout(AddressType dest) const;

    /// Track a reliable unicast send; returns the message id (or {0,0} on error).
    reliability::MessageId SendReliable(AddressType destination,
                                        const std::vector<uint8_t>& data,
                                        uint8_t max_retries,
                                        uint32_t timeout_override_ms);

    /// Track a reliable group send with an acknowledgement-collection window.
    reliability::MessageId SendGroupReliable(AddressType group,
                                             std::span<const uint8_t> data,
                                             uint8_t max_retries,
                                             uint32_t window_ms);

    /// Process an inbound ACK message (match, forward, or ignore).
    Result ProcessAckMessage(const BaseMessage& message);

    /// Enqueue an acknowledgement back toward the original sender.
    void EnqueueAck(AddressType dest, uint8_t acked_seq, bool was_group,
                    uint32_t echo_ts);

    /// Advance retransmission timers and close expired group windows.
    void ProcessReliableTimers();

    /// Register the delivery-outcome callback.
    void SetDeliveryCallback(reliability::DeliveryCallback callback);

    /// @return number of reliable messages currently awaiting acknowledgement.
    size_t GetReliablePendingCount() const;

   private:
    Result SendReliableAttempt(const reliability::MessageId& id,
                               std::span<const uint8_t> payload);
    void OnReliableOutcome(const reliability::DeliveryResult& result);
    void CloseExpiredGroupWindows();
    reliability::Host BuildReliableHost();

    static constexpr uint8_t kDefaultTTL = 10;

    // Group membership
    static constexpr size_t kMaxGroups = 8;
    std::array<AddressType, kMaxGroups> groups_{};
    uint8_t group_count_ = 0;

    // seq->destination shadow table
    struct ReliableDest {
        bool valid = false;
        uint8_t seq = 0;
        AddressType dest = 0;
    };

    std::array<ReliableDest, reliability::ReliableDelivery::kMaxPending>
        reliable_dest_{};

    // Acknowledgement-collection windows for reliable group sends
    struct GroupWindow {
        bool valid = false;
        uint8_t seq = 0;
        uint32_t deadline_ms = 0;
    };

    std::array<GroupWindow, reliability::ReliableDelivery::kMaxPending>
        group_windows_{};

    reliability::DeliveryCallback delivery_callback_;

    std::mutex& mutex_;  ///< Coordinator mutex (not owned)
    Host host_;

    // Constructed last: BuildReliableHost() reads host_, so host_ must precede.
    reliability::ReliableDelivery reliable_;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
