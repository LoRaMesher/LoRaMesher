/**
 * @file reliable_delivery.cpp
 * @brief Implementation of the reliable-delivery state machine.
 */

#include "reliable_delivery.hpp"

#include <algorithm>
#include <utility>

#include "utils/logger.hpp"
#include "utils/time_utils.hpp"

namespace loramesher {
namespace protocols {
namespace reliability {

ReliableDelivery::ReliableDelivery(Host host, DeliveryCallback callback)
    : host_(std::move(host)), callback_(std::move(callback)) {}

uint32_t ReliableDelivery::Now() const {
    return host_.now_ms ? host_.now_ms() : 0;
}

ReliableDelivery::PendingEntry* ReliableDelivery::FindEntry(MessageId id) {
    for (auto& entry : entries_) {
        if (entry.valid && entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

ReliableDelivery::PendingEntry* ReliableDelivery::FindFreeSlot() {
    for (auto& entry : entries_) {
        if (!entry.valid) {
            return &entry;
        }
    }
    return nullptr;
}

bool ReliableDelivery::RecordResponder(PendingEntry& entry, AddressType by) {
    for (uint8_t i = 0; i < entry.responder_count; ++i) {
        if (entry.responders[i] == by) {
            return false;
        }
    }
    if (entry.responder_count >= kMaxGroupResponders) {
        return false;
    }
    entry.responders[entry.responder_count++] = by;
    return true;
}

Result ReliableDelivery::Track(MessageId id, std::span<const uint8_t> payload,
                               Policy policy) {
    if (payload.size() > kMaxReliablePayload) {
        return Result(LoraMesherErrorCode::kBufferOverflow,
                      "Payload exceeds reliable delivery capacity");
    }

    if (FindEntry(id) != nullptr) {
        return Result(LoraMesherErrorCode::kInvalidArgument,
                      "Message id is already pending");
    }

    PendingEntry* entry = FindFreeSlot();
    if (!entry) {
        return Result(LoraMesherErrorCode::kQueueFull,
                      "Reliable delivery table is full");
    }

    const uint32_t now = Now();

    entry->valid = true;
    entry->id = id;
    entry->len = static_cast<uint8_t>(payload.size());
    std::copy(payload.begin(), payload.end(), entry->payload.begin());
    entry->policy = policy;
    entry->retries_left = policy.max_retries;
    entry->current_timeout_ms = policy.timeout_ms;
    entry->requeue = false;
    entry->requeue_is_retry = false;
    entry->consecutive_requeues = 0;
    entry->responder_count = 0;

    Attempt(*entry, now, /*is_retry=*/false);

    return Result::Success();
}

void ReliableDelivery::Attempt(PendingEntry& entry, uint32_t now,
                               bool is_retry) {
    AttemptRequest request{
        entry.id, std::span<const uint8_t>(entry.payload.data(), entry.len)};
    Result sent =
        host_.send_attempt ? host_.send_attempt(request) : Result::Success();

    if (!sent.IsSuccess()) {
        if (entry.consecutive_requeues >=
            entry.policy.max_consecutive_requeues) {
            LOG_WARNING(
                "Reliable seq=%u could not be queued after %u tries (%s)",
                entry.id.seq, entry.consecutive_requeues + 1,
                sent.GetErrorMessage().c_str());
            Finish(entry, Outcome::Failed);
            return;
        }
        entry.consecutive_requeues++;
        const uint32_t delay = entry.policy.requeue_delay_ms != 0
                                   ? entry.policy.requeue_delay_ms
                                   : entry.current_timeout_ms;
        LOG_WARNING(
            "Reliable seq=%u attempt not queued (%s); retrying in %u ms",
            entry.id.seq, sent.GetErrorMessage().c_str(), delay);
        entry.next_deadline_ms = now + delay;
        entry.requeue = true;
        entry.requeue_is_retry = is_retry;
        return;
    }

    if (is_retry && entry.policy.exponential_backoff) {
        uint64_t doubled = static_cast<uint64_t>(entry.current_timeout_ms) * 2;
        if (entry.policy.max_timeout_ms != 0) {
            doubled = std::min<uint64_t>(doubled, entry.policy.max_timeout_ms);
        }
        entry.current_timeout_ms = static_cast<uint32_t>(doubled);
    }
    entry.requeue = false;
    entry.consecutive_requeues = 0;
    entry.sent_at_ms = now;
    entry.next_deadline_ms = now + entry.current_timeout_ms;
}

bool ReliableDelivery::OnAck(MessageId acked, AddressType by,
                             uint32_t echo_ts) {
    PendingEntry* entry = FindEntry(acked);
    if (!entry) {
        return false;
    }

    if (!entry->policy.collect_multiple && by != entry->id.dest) {
        return false;
    }

    const uint32_t rtt = Now() - echo_ts;

    if (entry->policy.collect_multiple) {
        if (!RecordResponder(*entry, by)) {
            return false;
        }
        const uint8_t count = entry->responder_count;
        if (callback_) {
            callback_({entry->id, Outcome::Delivered, by, rtt, count});
        }
        return true;
    }

    const MessageId id = entry->id;
    entry->valid = false;
    if (callback_) {
        callback_({id, Outcome::Delivered, by, rtt, 0});
    }
    return true;
}

void ReliableDelivery::Tick() {
    const uint32_t now = Now();

    for (auto& entry : entries_) {
        if (!entry.valid || !utils::TimeReached(now, entry.next_deadline_ms)) {
            continue;
        }
        if (entry.requeue) {
            // The previous attempt never left the node; repeating it does not
            // spend the retry budget.
            Attempt(entry, now, entry.requeue_is_retry);
        } else if (entry.retries_left > 0) {
            entry.retries_left--;
            Attempt(entry, now, /*is_retry=*/true);
        } else if (!entry.policy.collect_multiple) {
            Finish(entry, Outcome::Failed);
        }
        // A group window with no retries left stays open until its owner
        // closes it.
    }
}

void ReliableDelivery::Finish(PendingEntry& entry, Outcome outcome) {
    const MessageId id = entry.id;
    const uint8_t count = entry.responder_count;
    entry.valid = false;
    if (callback_) {
        callback_({id, outcome, 0, 0, count});
    }
}

void ReliableDelivery::CloseGroup(MessageId id) {
    PendingEntry* entry = FindEntry(id);
    if (!entry) {
        return;
    }
    Finish(*entry, Outcome::GroupWindowClosed);
}

void ReliableDelivery::AbortAll() {
    for (auto& entry : entries_) {
        if (entry.valid) {
            Finish(entry, entry.policy.collect_multiple
                              ? Outcome::GroupWindowClosed
                              : Outcome::Failed);
        }
    }
}

uint8_t ReliableDelivery::PendingSeqSpan(bool group_stream, AddressType dest,
                                         uint8_t seq) const {
    uint8_t span = 0;
    for (const auto& entry : entries_) {
        if (!entry.valid || entry.policy.collect_multiple != group_stream ||
            (!group_stream && entry.id.dest != dest)) {
            continue;
        }
        span = std::max(span, static_cast<uint8_t>(seq - entry.id.seq));
    }
    return span;
}

size_t ReliableDelivery::PendingCount() const {
    size_t count = 0;
    for (const auto& entry : entries_) {
        if (entry.valid) {
            ++count;
        }
    }
    return count;
}

}  // namespace reliability
}  // namespace protocols
}  // namespace loramesher
