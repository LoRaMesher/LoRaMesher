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

#include "types/error_codes/result.hpp"
#include "types/messages/base_message.hpp"
#include "types/messages/loramesher/data_header.hpp"
#include "utils/compat/span.hpp"

namespace loramesher {
namespace protocols {
namespace reliability {

/// Size of the send-timestamp prefix carried by reliable messages for RTT.
static constexpr size_t kReliableTimestampSize = 4;

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
};

/**
 * @brief Fixed-capacity reliable-delivery state machine.
 */
class ReliableDelivery {
   public:
    /// Maximum number of concurrently tracked messages.
    static constexpr size_t kMaxPending = 8;
    /// Maximum distinct responders tracked per group window.
    static constexpr size_t kMaxGroupResponders = 16;
    /// Largest application payload a reliable message can carry.
    static constexpr size_t kMaxReliablePayload = BaseMessage::kMaxPayloadSize -
                                                  DataHeader::DataFieldsSize() -
                                                  kReliableTimestampSize;

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
        uint8_t retries_left = 0;
        uint32_t sent_at_ms = 0;
        std::array<AddressType, kMaxGroupResponders> responders{};
        uint8_t responder_count = 0;
    };

    PendingEntry* FindEntry(MessageId id);
    PendingEntry* FindFreeSlot();
    bool RecordResponder(PendingEntry& entry, AddressType by);
    uint32_t Now() const;

    std::array<PendingEntry, kMaxPending> entries_{};
    Host host_;
    DeliveryCallback callback_;
};

}  // namespace reliability
}  // namespace protocols
}  // namespace loramesher
