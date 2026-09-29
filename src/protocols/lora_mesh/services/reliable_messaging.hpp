/**
 * @file reliable_messaging.hpp
 * @brief Group multicast + reliable-delivery subsystem extracted from NetworkService
 *
 * Owns the end-to-end reliable unicast/group delivery state machine
 * (reliability::ReliableDelivery), group (multicast) membership, and the
 * acknowledgement-collection windows.
 * Constructed and owned by NetworkService, which delegates the corresponding
 * public API to it and supplies cross-cutting dependencies as Host closures.
 *
 * Threading: sends run on the application thread while timers and received
 * acknowledgements run on the protocol task. All state is guarded by an
 * internal mutex; delivery outcomes are reported after it is released, so a
 * delivery callback may call back into this component.
 */

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "protocols/lora_mesh/services/message_cache.hpp"
#include "protocols/reliability/delivery_windows.hpp"
#include "protocols/reliability/reliable_delivery.hpp"
#include "types/error_codes/result.hpp"
#include "types/messages/base_header.hpp"
#include "types/messages/base_message.hpp"
#include "types/messages/loramesher/data_message.hpp"
#include "types/messages/loramesher/group_message.hpp"
#include "types/protocols/lora_mesh/path_rtt.hpp"
#include "types/protocols/lora_mesh/slot_allocation.hpp"
#include "utils/compat/span.hpp"

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
        /// Deliver a received payload addressed to @p dest (this node or a
        /// group) to the app. The component passes the message's remaining
        /// TTL; the coordinator converts it to a hop count.
        std::function<void(AddressType src, uint8_t seq, AddressType dest,
                           uint8_t ttl, std::span<const uint8_t> payload)>
            deliver_to_app;
        /// True when the protocol is in NORMAL_OPERATION or NETWORK_MANAGER.
        std::function<bool()> in_operational_state;
        std::function<uint8_t()> max_hops;  ///< Configured max hop count
        std::function<uint16_t()>
            max_packet_size;        ///< Configured max packet size
        HopsToDestFn hops_to_dest;  ///< Routing hop-count lookup
        SuperframeDurationFn superframe_duration;  ///< Superframe duration (ms)
        /// Stored round-trip estimate toward a destination (nullopt if the
        /// destination is unknown).
        std::function<std::optional<types::protocols::lora_mesh::PathRtt>(
            AddressType)>
            get_path_rtt;
        /// Store the round-trip estimate toward a destination.
        std::function<bool(AddressType,
                           const types::protocols::lora_mesh::PathRtt&)>
            set_path_rtt;
        /// Random value used to start new sequence streams.
        std::function<uint32_t()> random;
    };

    /**
     * @param message_cache Node-wide sequence counter and de-duplication
     *        cache (not owned; must outlive this component)
     * @param host Cross-cutting dependencies
     */
    ReliableMessaging(MessageCache& message_cache, Host host);

    // --- Group (multicast) membership ---

    Result JoinGroup(AddressType group);
    Result LeaveGroup(AddressType group);
    bool IsMemberOfGroup(AddressType group) const;
    std::vector<AddressType> GetGroups() const;

    // --- Group send / receive ---

    /// Send a best-effort (non-reliable) group multicast message.
    Result SendGroup(AddressType group, std::span<const uint8_t> data);

    /// Process an inbound group message: dedup, deliver to members, relay flood.
    Result ProcessGroupMessage(const BaseMessage& message,
                               uint32_t reception_timestamp);

    // --- Reliable unicast/group delivery ---

    /// Estimate a retransmit timeout (ms) from hop count and superframe duration.
    uint32_t ComputeReliableTimeout(AddressType dest) const;

    /// Retransmit timeout (ms) for @p dest: the measured round-trip estimate
    /// when one exists, otherwise ComputeReliableTimeout(), clamped to
    /// [kTimeoutFloorMs, MaxReliableTimeout()].
    uint32_t ComputeAdaptiveTimeout(AddressType dest) const;

    /// Upper bound (ms) for a retransmit timeout, including backoff.
    uint32_t MaxReliableTimeout() const;

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

    /**
     * @brief De-duplicate delivery of a received reliable message
     *
     * @param source Sender of the message
     * @param kind Sender stream the message belongs to
     * @param msg_seq Message sequence from the reliable framing prefix
     * @param send_ts Send timestamp from the reliable framing prefix
     * @return true if the message has not been delivered before
     */
    bool AcceptReliable(AddressType source, reliability::StreamKind kind,
                        uint8_t msg_seq, uint32_t send_ts);

   private:
    /// Outcomes produced while the mutex is held, reported after release.
    struct OutcomeBatch {
        std::array<reliability::DeliveryResult,
                   2 * reliability::ReliableDelivery::kMaxPending>
            results{};
        size_t count = 0;
    };

    /// Run @p fn under mutex_, collecting its delivery outcomes, then report
    /// them after the mutex is released.
    template <typename Fn>
    auto RunLocked(Fn&& fn);
    /// Report @p batch to the delivery callback; the mutex must not be held.
    void DispatchOutcomes(const OutcomeBatch& batch);
    bool IsMemberOfGroupLocked(AddressType group) const;

    Result ForwardGroupMessage(const GroupMessage& original);
    Result SendReliableAttempt(const reliability::AttemptRequest& request);
    /// Fold an acknowledgement's round-trip sample into @p peer's estimate.
    void RecordRttSample(AddressType peer, uint32_t echo_ts);
    uint32_t SuperframeOrDefault() const;
    void OnReliableOutcome(const reliability::DeliveryResult& result);
    void CloseExpiredGroupWindows();
    reliability::Host BuildReliableHost();

    static constexpr uint8_t kDefaultTTL = 10;
    static constexpr uint32_t kTimeoutFloorMs = 500;
    /// Retransmit timeouts never exceed this many superframes per hop limit.
    static constexpr uint32_t kMaxTimeoutSuperframesPerHop = 4;

    // Group membership
    static constexpr size_t kMaxGroups = 8;
    std::array<AddressType, kMaxGroups> groups_{};
    uint8_t group_count_ = 0;

    // Acknowledgement-collection windows for reliable group sends
    struct GroupWindow {
        bool valid = false;
        uint8_t seq = 0;
        AddressType group = 0;
        uint32_t deadline_ms = 0;
    };

    /// Open window of a reliable group send with sequence @p seq, or null.
    GroupWindow* FindGroupWindow(uint8_t seq);

    /// Message sequence counter of one unicast destination.
    struct SeqStream {
        bool valid = false;
        AddressType dest = 0;
        uint8_t next = 0;
    };

    /// Every unicast peer the protocol can address; streams are never evicted,
    /// since restarting a counter could reuse a sequence the peer still holds.
    static constexpr size_t kMaxSeqStreams = 0xFF - 1;

    /**
     * @brief Allocate the next message sequence of a stream
     *
     * @param dest Unicast destination (ignored for the group stream)
     * @param group_stream Allocate from the stream shared by all group sends
     * @return The sequence, or nullopt if the stream table is full or the
     *         sequence would leave an unacknowledged message outside the
     *         receiver's delivery window
     */
    std::optional<uint8_t> AllocateMessageSeq(AddressType dest,
                                              bool group_stream);
    SeqStream* FindOrCreateSeqStream(AddressType dest);
    uint8_t RandomSeq() const;

    std::array<SeqStream, kMaxSeqStreams> seq_streams_{};
    uint8_t group_next_seq_ = 0;
    bool group_stream_started_ = false;

    /// Received reliable messages, de-duplicated per sender stream.
    reliability::DeliveryWindows delivery_windows_;

    std::array<GroupWindow, reliability::ReliableDelivery::kMaxPending>
        group_windows_{};

    reliability::DeliveryCallback delivery_callback_;

    /// Collects outcomes while mutex_ is held; null otherwise.
    OutcomeBatch* outcome_batch_ = nullptr;

    mutable std::mutex mutex_;
    MessageCache& message_cache_;
    Host host_;

    // Constructed last: BuildReliableHost() reads host_, so host_ must precede.
    reliability::ReliableDelivery reliable_;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
