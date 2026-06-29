/**
 * @file reliable_messaging.cpp
 * @brief Implementation of the group multicast + reliable-delivery subsystem
 */

#include "reliable_messaging.hpp"

#include <algorithm>

#include "types/messages/loramesher/ack_payload.hpp"
#include "types/messages/loramesher/data_header.hpp"
#include "types/messages/loramesher/group_message.hpp"
#include "types/messages/message_type.hpp"
#include "utils/byte_operations.h"
#include "utils/logger.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

namespace {
using SlotType = types::protocols::lora_mesh::SlotAllocation::SlotType;
}  // namespace

ReliableMessaging::ReliableMessaging(std::mutex& mutex, Host host)
    : mutex_(mutex),
      host_(std::move(host)),
      reliable_(BuildReliableHost(),
                [this](const reliability::DeliveryResult& result) {
                    OnReliableOutcome(result);
                }) {}

reliability::Host ReliableMessaging::BuildReliableHost() {
    reliability::Host host;
    host.send_attempt = [this](const reliability::MessageId& id,
                               std::span<const uint8_t> payload) {
        return SendReliableAttempt(id, payload);
    };
    host.now_ms = [this]() {
        return host_.now_ms ? host_.now_ms() : 0u;
    };
    return host;
}

// --- Group (multicast) membership ---

Result ReliableMessaging::JoinGroup(AddressType group) {
    if (!IsGroupAddress(group)) {
        return Result(LoraMesherErrorCode::kInvalidArgument,
                      "Address is not a group address");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint8_t i = 0; i < group_count_; ++i) {
        if (groups_[i] == group) {
            return Result::Success();
        }
    }
    if (group_count_ >= kMaxGroups) {
        return Result(LoraMesherErrorCode::kBufferOverflow,
                      "Group membership table is full");
    }
    groups_[group_count_++] = group;
    LOG_INFO("Joined group 0x%04X", group);
    return Result::Success();
}

Result ReliableMessaging::LeaveGroup(AddressType group) {
    if (!IsGroupAddress(group)) {
        return Result(LoraMesherErrorCode::kInvalidArgument,
                      "Address is not a group address");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint8_t i = 0; i < group_count_; ++i) {
        if (groups_[i] == group) {
            groups_[i] = groups_[group_count_ - 1];
            group_count_--;
            LOG_INFO("Left group 0x%04X", group);
            return Result::Success();
        }
    }
    return Result::Success();
}

bool ReliableMessaging::IsMemberOfGroup(AddressType group) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint8_t i = 0; i < group_count_; ++i) {
        if (groups_[i] == group) {
            return true;
        }
    }
    return false;
}

std::vector<AddressType> ReliableMessaging::GetGroups() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<AddressType>(groups_.begin(),
                                    groups_.begin() + group_count_);
}

// --- Reliable destination shadow table ---

AddressType ReliableMessaging::LookupReliableDest(uint8_t seq) const {
    for (const auto& entry : reliable_dest_) {
        if (entry.valid && entry.seq == seq) {
            return entry.dest;
        }
    }
    return 0;
}

void ReliableMessaging::RecordReliableDest(uint8_t seq, AddressType dest) {
    for (auto& entry : reliable_dest_) {
        if (!entry.valid) {
            entry = {true, seq, dest};
            return;
        }
    }
    LOG_WARNING("Reliable destination table full; seq=%u not recorded", seq);
}

void ReliableMessaging::ClearReliableDest(uint8_t seq) {
    for (auto& entry : reliable_dest_) {
        if (entry.valid && entry.seq == seq) {
            entry.valid = false;
            return;
        }
    }
}

// --- Reliable unicast/group delivery ---

uint32_t ReliableMessaging::ComputeReliableTimeout(AddressType dest) const {
    uint8_t hops = host_.hops_to_dest ? host_.hops_to_dest(dest) : 1;
    if (hops == 0) {
        hops = 1;
    }

    uint32_t superframe_ms =
        host_.superframe_duration ? host_.superframe_duration() : 0;
    if (superframe_ms == 0) {
        superframe_ms = 1000;
    }

    // Round trip ≈ 2 hops, plus one superframe of slot-phase guard.
    uint32_t timeout = (2u * hops + 1u) * superframe_ms;
    constexpr uint32_t kTimeoutFloorMs = 500;
    return timeout < kTimeoutFloorMs ? kTimeoutFloorMs : timeout;
}

Result ReliableMessaging::SendReliableAttempt(
    const reliability::MessageId& id, std::span<const uint8_t> payload) {
    AddressType dest = LookupReliableDest(id.seq);
    if (dest == 0) {
        LOG_ERROR("No destination recorded for reliable seq=%u", id.seq);
        return Result(LoraMesherErrorCode::kInvalidState,
                      "No destination for reliable attempt");
    }

    uint8_t ttl =
        (host_.max_hops() > 0)
            ? static_cast<uint8_t>(std::min(2u * host_.max_hops(), 255u))
            : kDefaultTTL;

    // Build wire payload: [send_timestamp:4][application payload]
    uint32_t timestamp = host_.now_ms();
    std::vector<uint8_t> wire(reliability::kReliableTimestampSize +
                              payload.size());
    utils::ByteSerializer serializer(wire.data(), wire.size());
    serializer.WriteUint32(timestamp);
    if (!payload.empty()) {
        serializer.WriteBytes(payload.data(), payload.size());
    }

    std::unique_ptr<BaseMessage> base_msg;
    if (IsGroupAddress(dest)) {
        auto group_msg =
            GroupMessage::Create(dest, host_.node_address, ttl,
                                 GroupMessage::kFlagRequestAcks, id.seq, wire);
        if (!group_msg) {
            return Result(LoraMesherErrorCode::kMemoryError,
                          "Failed to create reliable group message");
        }
        base_msg = std::make_unique<BaseMessage>(group_msg->ToBaseMessage());
    } else {
        AddressType next_hop = host_.find_next_hop(dest);
        if (next_hop == 0) {
            next_hop = dest;
        }
        auto data_msg =
            DataMessage::Create(dest, host_.node_address, next_hop, wire, ttl,
                                id.seq, MessageType::DATA_RELIABLE);
        if (!data_msg) {
            return Result(LoraMesherErrorCode::kMemoryError,
                          "Failed to create reliable data message");
        }
        base_msg = std::make_unique<BaseMessage>(data_msg->ToBaseMessage());
    }

    return host_.enqueue(SlotType::TX, std::move(base_msg));
}

reliability::MessageId ReliableMessaging::SendReliable(
    AddressType destination, const std::vector<uint8_t>& data,
    uint8_t max_retries, uint32_t timeout_override_ms) {
    constexpr reliability::MessageId kInvalidId{0, 0};

    if (destination == host_.node_address) {
        LOG_WARNING("Cannot send reliable data to self");
        return kInvalidId;
    }

    if (!host_.in_operational_state()) {
        LOG_WARNING("Cannot send reliable data in current state");
        return kInvalidId;
    }

    const size_t overhead = BaseHeader::Size() + DataHeader::DataFieldsSize() +
                            reliability::kReliableTimestampSize;
    if (data.size() + overhead > host_.max_packet_size() ||
        data.size() > reliability::ReliableDelivery::MaxReliablePayload()) {
        LOG_WARNING("Reliable payload %zu B exceeds capacity", data.size());
        return kInvalidId;
    }

    uint8_t seq = host_.next_seq();

    // Prevent self-receive if we hear our own message.
    host_.record_in_cache(host_.node_address, seq);
    RecordReliableDest(seq, destination);

    reliability::MessageId id{host_.node_address, seq};
    reliability::Policy policy;
    policy.timeout_ms = timeout_override_ms != 0
                            ? timeout_override_ms
                            : ComputeReliableTimeout(destination);
    policy.max_retries = max_retries;
    policy.collect_multiple = false;

    Result result = reliable_.Track(
        id, std::span<const uint8_t>(data.data(), data.size()), policy);
    if (!result.IsSuccess()) {
        LOG_ERROR("Failed to track reliable message seq=%u: %s", seq,
                  result.GetErrorMessage().c_str());
        ClearReliableDest(seq);
        return kInvalidId;
    }

    LOG_INFO("Sending reliable DATA to 0x%04X (seq=%u, timeout=%u, retries=%u)",
             destination, seq, policy.timeout_ms, max_retries);
    return id;
}

reliability::MessageId ReliableMessaging::SendGroupReliable(
    AddressType group, std::span<const uint8_t> data, uint8_t max_retries,
    uint32_t window_ms) {
    constexpr reliability::MessageId kInvalidId{0, 0};

    if (!IsGroupAddress(group)) {
        LOG_WARNING("SendGroupReliable destination 0x%04X is not a group",
                    group);
        return kInvalidId;
    }

    if (!host_.in_operational_state()) {
        LOG_WARNING("Cannot send reliable group data in current state");
        return kInvalidId;
    }

    const size_t overhead = BaseHeader::Size() +
                            GroupMessage::kGroupFieldsSize +
                            reliability::kReliableTimestampSize;
    if (data.size() + overhead > host_.max_packet_size() ||
        data.size() > reliability::ReliableDelivery::MaxReliablePayload()) {
        LOG_WARNING("Reliable group payload %zu B exceeds capacity",
                    data.size());
        return kInvalidId;
    }

    uint8_t seq = host_.next_seq();

    host_.record_in_cache(host_.node_address, seq);
    // A group destination tells the send_attempt closure to build a flooded
    // group message (with the request-acks flag) rather than a unicast.
    RecordReliableDest(seq, group);

    reliability::MessageId id{host_.node_address, seq};
    reliability::Policy policy;
    policy.timeout_ms = window_ms;
    policy.max_retries = max_retries;
    policy.collect_multiple = true;

    Result result = reliable_.Track(id, data, policy);
    if (!result.IsSuccess()) {
        LOG_ERROR("Failed to track reliable group seq=%u: %s", seq,
                  result.GetErrorMessage().c_str());
        ClearReliableDest(seq);
        return kInvalidId;
    }

    // Register the acknowledgement-collection window.
    uint32_t deadline = host_.now_ms() + window_ms;
    for (auto& window : group_windows_) {
        if (!window.valid) {
            window = {true, seq, deadline};
            break;
        }
    }

    LOG_INFO("Sending reliable GROUP to 0x%04X (seq=%u, window=%u)", group, seq,
             window_ms);
    return id;
}

void ReliableMessaging::EnqueueAck(AddressType dest, uint8_t acked_seq,
                                   bool was_group, uint32_t echo_ts) {
    AddressType next_hop = host_.find_next_hop(dest);
    if (next_hop == 0) {
        next_hop = dest;
    }

    uint8_t ttl =
        (host_.max_hops() > 0)
            ? static_cast<uint8_t>(std::min(2u * host_.max_hops(), 255u))
            : kDefaultTTL;

    AckPayload ack;
    ack.acked_seq = acked_seq;
    ack.flags = was_group ? AckPayload::kFlagWasGroup : 0;
    ack.echo_timestamp = echo_ts;
    auto ack_bytes = ack.Serialize();
    std::vector<uint8_t> payload(ack_bytes.begin(), ack_bytes.end());

    // ACKs are not de-duplicated and are matched by acked_seq in the payload,
    // so the message seq_num is unused; keep it at 0.
    auto ack_msg =
        DataMessage::Create(dest, host_.node_address, next_hop, payload, ttl,
                            /*seq_num=*/0, MessageType::ACK);
    if (!ack_msg) {
        LOG_ERROR("Failed to create ACK for 0x%04X seq=%u", dest, acked_seq);
        return;
    }

    auto base_msg = std::make_unique<BaseMessage>(ack_msg->ToBaseMessage());
    Result queue_result = host_.enqueue(SlotType::TX, std::move(base_msg));
    if (!queue_result) {
        LOG_ERROR("Failed to queue ACK for 0x%04X: %s", dest,
                  queue_result.GetErrorMessage().c_str());
    }
}

Result ReliableMessaging::ProcessAckMessage(const BaseMessage& message) {
    auto ack_msg_opt = DataMessage::CreateFromBaseMessage(message);
    if (!ack_msg_opt) {
        LOG_ERROR("Failed to deserialize ACK message");
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to deserialize ACK message");
    }

    const DataMessage& ack_msg = *ack_msg_opt;
    AddressType next_hop = ack_msg.GetNextHop();
    AddressType final_dest = ack_msg.GetDestination();
    AddressType acker = ack_msg.GetSource();
    uint8_t ttl = ack_msg.GetTTL();

    if (acker == host_.node_address) {
        return Result::Success();
    }

    // Link-layer filter: only act on ACKs for which we are the next hop.
    if (next_hop != host_.node_address) {
        return Result::Success();
    }

    if (final_dest == host_.node_address) {
        auto ack = AckPayload::Deserialize(ack_msg.GetPayload());
        if (!ack) {
            LOG_ERROR("Malformed ACK payload from 0x%04X", acker);
            return Result(LoraMesherErrorCode::kSerializationError,
                          "Malformed ACK payload");
        }
        reliability::MessageId id{host_.node_address, ack->acked_seq};
        bool matched = reliable_.OnAck(id, acker, ack->echo_timestamp);
        LOG_DEBUG("ACK from 0x%04X for seq=%u matched=%d", acker,
                  ack->acked_seq, matched);
        return Result::Success();
    }

    // Forward the ACK toward the original sender.
    if (ttl <= 1) {
        LOG_WARNING("ACK TTL expired toward 0x%04X, dropping", final_dest);
        return Result::Success();
    }
    return host_.forward_data_message(ack_msg);
}

void ReliableMessaging::ProcessReliableTimers() {
    reliable_.Tick();
    CloseExpiredGroupWindows();
}

void ReliableMessaging::CloseExpiredGroupWindows() {
    uint32_t now = host_.now_ms();
    for (auto& window : group_windows_) {
        if (window.valid && now >= window.deadline_ms) {
            window.valid = false;
            reliable_.CloseGroup({host_.node_address, window.seq});
        }
    }
}

void ReliableMessaging::OnReliableOutcome(
    const reliability::DeliveryResult& result) {
    const bool is_group = IsGroupAddress(LookupReliableDest(result.id.seq));

    switch (result.outcome) {
        case reliability::Outcome::Delivered:
            // A unicast entry is erased on first ACK; a group window stays open
            // until it is explicitly closed, so keep its destination mapping.
            if (!is_group) {
                ClearReliableDest(result.id.seq);
            }
            break;
        case reliability::Outcome::Failed:
        case reliability::Outcome::GroupWindowClosed:
            ClearReliableDest(result.id.seq);
            break;
    }

    if (delivery_callback_) {
        delivery_callback_(result);
    }
}

void ReliableMessaging::SetDeliveryCallback(
    reliability::DeliveryCallback callback) {
    delivery_callback_ = std::move(callback);
}

size_t ReliableMessaging::GetReliablePendingCount() const {
    return reliable_.PendingCount();
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
