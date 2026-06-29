/**
 * @file reliable_messaging.cpp
 * @brief Implementation of group multicast + reliable-delivery glue
 */

#include "reliable_messaging.hpp"

#include "utils/logger.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

ReliableMessaging::ReliableMessaging(std::mutex& mutex,
                                     HopsToDestFn hops_to_dest,
                                     SuperframeDurationFn superframe_duration)
    : mutex_(mutex),
      hops_to_dest_(std::move(hops_to_dest)),
      superframe_duration_(std::move(superframe_duration)) {}

uint32_t ReliableMessaging::ComputeReliableTimeout(AddressType dest) const {
    uint8_t hops = hops_to_dest_ ? hops_to_dest_(dest) : 1;
    if (hops == 0) {
        hops = 1;
    }

    uint32_t superframe_ms = superframe_duration_ ? superframe_duration_() : 0;
    if (superframe_ms == 0) {
        superframe_ms = 1000;
    }

    // Round trip ≈ 2 hops, plus one superframe of slot-phase guard.
    uint32_t timeout = (2u * hops + 1u) * superframe_ms;
    constexpr uint32_t kTimeoutFloorMs = 500;
    return timeout < kTimeoutFloorMs ? kTimeoutFloorMs : timeout;
}

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

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
