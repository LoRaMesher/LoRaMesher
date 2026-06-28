/**
 * @file reliable_delivery.cpp
 * @brief Implementation of the reliable-delivery state machine.
 */

#include "reliable_delivery.hpp"

#include <algorithm>
#include <utility>

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
    entry->next_deadline_ms = now + policy.timeout_ms;
    entry->sent_at_ms = now;
    entry->responder_count = 0;

    if (host_.send_attempt) {
        host_.send_attempt(
            id, std::span<const uint8_t>(entry->payload.data(), entry->len));
    }

    return Result::Success();
}

bool ReliableDelivery::OnAck(MessageId acked, AddressType by,
                             uint32_t echo_ts) {
    PendingEntry* entry = FindEntry(acked);
    if (!entry) {
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
        if (!entry.valid || entry.policy.collect_multiple) {
            continue;
        }
        if (now < entry.next_deadline_ms) {
            continue;
        }

        if (entry.retries_left > 0) {
            entry.retries_left--;
            entry.sent_at_ms = now;
            entry.next_deadline_ms = now + entry.policy.timeout_ms;
            if (host_.send_attempt) {
                host_.send_attempt(
                    entry.id,
                    std::span<const uint8_t>(entry.payload.data(), entry.len));
            }
        } else {
            const MessageId id = entry.id;
            entry.valid = false;
            if (callback_) {
                callback_({id, Outcome::Failed, 0, 0, 0});
            }
        }
    }
}

void ReliableDelivery::CloseGroup(MessageId id) {
    PendingEntry* entry = FindEntry(id);
    if (!entry) {
        return;
    }
    const uint8_t count = entry->responder_count;
    entry->valid = false;
    if (callback_) {
        callback_({id, Outcome::GroupWindowClosed, 0, 0, count});
    }
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
