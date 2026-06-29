/**
 * @file reliable_messaging.cpp
 * @brief Implementation of group multicast + reliable-delivery glue
 */

#include "reliable_messaging.hpp"

#include "utils/logger.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

ReliableMessaging::ReliableMessaging(std::mutex& mutex) : mutex_(mutex) {}

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
