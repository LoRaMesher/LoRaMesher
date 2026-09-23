/**
 * @file reliable_delivery.hpp
 * @brief Standalone reliable-delivery state machine (ACK + retransmit).
 *
 * This component owns the pending table, retransmit countdown, ACK matching,
 * RTT computation, retained payloads, and the outcome callback. It has no
 * dependency on the radio, routing, the message queue, the superframe, or any
 * RTOS facility — every mesh-specific action is delegated to host-supplied
 * closures. This keeps the state machine unit-testable in isolation.
 */

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

#include "types/error_codes/result.hpp"
#include "types/messages/base_message.hpp"
#include "types/messages/loramesher/data_header.hpp"
#include "utils/byte_operations.h"

namespace loramesher {
namespace protocols {
namespace reliability {

/**
 * @brief Framing prefix of reliable DATA and reliable GROUP payloads.
 *
 * Wire layout: [msg_seq:1][send_ts:4][application payload]. Every
 * transmission attempt is a distinct link-layer packet with its own sequence
 * number; msg_seq is the sequence allocated when the message was first sent
 * and stays the same across attempts, so the destination can de-duplicate
 * delivery and the acknowledgement can be matched to the tracked message.
 * send_ts is the attempt's own send time, echoed by the acknowledgement for a
 * round-trip sample.
 */
struct ReliablePrefix {
    static constexpr size_t kSize = sizeof(uint8_t) + sizeof(uint32_t);

    uint8_t msg_seq = 0;   ///< Stable end-to-end message sequence
    uint32_t send_ts = 0;  ///< Send time of this attempt (ms)

    /// Write the prefix into @p serializer.
    void Write(utils::ByteSerializer& serializer) const {
        serializer.WriteUint8(msg_seq);
        serializer.WriteUint32(send_ts);
    }

    /// Parse the prefix from the start of @p payload.
    static std::optional<ReliablePrefix> Read(
        std::span<const uint8_t> payload) {
        if (payload.size() < kSize) {
            return std::nullopt;
        }
        utils::ByteDeserializer deserializer(payload);
        auto seq = deserializer.ReadUint8();
        auto ts = deserializer.ReadUint32();
        if (!seq || !ts) {
            return std::nullopt;
        }
        return ReliablePrefix{*seq, *ts};
    }
};

/**
 * @brief Stable identifier for a tracked message.
 */
struct MessageId {
    AddressType source = 0;  ///< Originator of the message
    uint8_t seq = 0;         ///< Per-source sequence number

    /// Pack source and sequence into a single comparable value.
    uint32_t value() const {
        return (static_cast<uint32_t>(source) << 8) | seq;
    }

    bool operator==(const MessageId& other) const = default;
};

/// Terminal result of a tracked message.
enum class Outcome { Delivered, Failed, GroupWindowClosed };

/**
 * @brief Outcome reported through the delivery callback.
 */
struct DeliveryResult {
    MessageId id;           ///< Identifier of the tracked message
    Outcome outcome;        ///< What happened
    AddressType by = 0;     ///< Acknowledging node (valid for Delivered)
    uint32_t rtt_ms = 0;    ///< Round-trip time (valid for Delivered)
    uint8_t ack_count = 0;  ///< Distinct responders (group outcomes)
};

using DeliveryCallback = std::function<void(const DeliveryResult&)>;

/**
 * @brief Mesh-specific operations the host protocol must provide.
 */
struct Host {
    /// Transmit one attempt of a tracked message. Must be non-blocking and must
    /// not transmit synchronously from a receive context.
    std::function<Result(const MessageId&, std::span<const uint8_t>)>
        send_attempt;
    /// Monotonic millisecond clock.
    std::function<uint32_t()> now_ms;
};

/**
 * @brief Per-message retransmission and acknowledgement policy.
 */
struct Policy {
    uint32_t timeout_ms = 0;        ///< Time between attempts
    uint8_t max_retries = 3;        ///< Retransmissions after the first attempt
    bool collect_multiple = false;  ///< true: group window (one Delivered per
                                    ///< distinct responder, no erase on ACK)
    uint32_t requeue_delay_ms = 0;  ///< Delay before re-trying an attempt the
                                    ///< host could not queue (0 = timeout_ms)
    uint32_t max_timeout_ms = 0;    ///< Upper bound for backed-off timeouts
                                    ///< (0 = unbounded)
    bool exponential_backoff = false;  ///< Double the timeout on each retry
};

/**
 * @brief Fixed-capacity reliable-delivery state machine.
 */
class ReliableDelivery {
   public:
    /// Maximum number of concurrently tracked messages.
    static constexpr size_t kMaxPending = 8;
    /// Maximum distinct responders tracked per group window: every other node
    /// of the largest network the protocol can address (control-slot indices
    /// are 8-bit with 0xFF reserved as "unassigned").
    static constexpr size_t kMaxGroupResponders = 0xFF - 1;
    /// Largest application payload a reliable message can carry.
    static constexpr size_t kMaxReliablePayload = BaseMessage::kMaxPayloadSize -
                                                  DataHeader::DataFieldsSize() -
                                                  ReliablePrefix::kSize;

    /// Largest application payload a reliable message can carry.
    static constexpr size_t MaxReliablePayload() { return kMaxReliablePayload; }

    /**
     * @brief Construct the state machine.
     *
     * @param host Mesh-specific operations
     * @param callback Outcome callback fired on terminal events
     */
    ReliableDelivery(Host host, DeliveryCallback callback);

    /**
     * @brief Begin tracking a message and perform attempt #1.
     *
     * @param id Identifier the host placed on the wire
     * @param payload Application payload (retained for retransmission)
     * @param policy Retransmission / acknowledgement policy
     * @return Result Success, or an error if the table is full or the payload
     *         exceeds MaxReliablePayload()
     */
    Result Track(MessageId id, std::span<const uint8_t> payload, Policy policy);

    /**
     * @brief Process an acknowledgement.
     *
     * @param acked Identifier being acknowledged
     * @param by Acknowledging node
     * @param echo_ts Send timestamp echoed by the acknowledgement, for RTT
     * @return bool true if it matched a tracked entry; false if unsolicited
     */
    bool OnAck(MessageId acked, AddressType by, uint32_t echo_ts);

    /**
     * @brief Advance retransmission timers; retransmit or fail expired entries.
     */
    void Tick();

    /**
     * @brief Close a group window, firing GroupWindowClosed with the responder
     *        count and erasing the entry.
     *
     * @param id Identifier of the group entry to close
     */
    void CloseGroup(MessageId id);

    /// Number of currently tracked messages.
    size_t PendingCount() const;

   private:
    /**
     * @brief One tracked message.
     */
    struct PendingEntry {
        bool valid = false;
        MessageId id{};
        std::array<uint8_t, kMaxReliablePayload> payload{};
        uint8_t len = 0;
        Policy policy{};
        uint32_t next_deadline_ms = 0;
        uint32_t current_timeout_ms = 0;  ///< Timeout of the latest attempt
        bool requeue = false;             ///< Latest attempt was not queued
        bool requeue_is_retry = false;    ///< That attempt was a retransmission
        uint8_t retries_left = 0;
        uint32_t sent_at_ms = 0;
        std::array<AddressType, kMaxGroupResponders> responders{};
        uint8_t responder_count = 0;
    };

    PendingEntry* FindEntry(MessageId id);
    PendingEntry* FindFreeSlot();
    /// Hand one attempt of @p entry to the host and schedule its deadline.
    /// A rejected attempt is re-tried after the requeue delay and does not
    /// consume a retry.
    void Attempt(PendingEntry& entry, uint32_t now, bool is_retry);
    bool RecordResponder(PendingEntry& entry, AddressType by);
    uint32_t Now() const;

    std::array<PendingEntry, kMaxPending> entries_{};
    Host host_;
    DeliveryCallback callback_;
};

}  // namespace reliability
}  // namespace protocols
}  // namespace loramesher
