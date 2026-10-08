/**
 * @file message_queue_service.cpp
 * @brief Implementation of message queue service
 */

#include "message_queue_service.hpp"

#include <algorithm>
#include <numeric>

#include "utils/logger.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

MessageQueueService::MessageQueueService(size_t max_queue_size)
    : max_queue_size_(max_queue_size) {}

Result MessageQueueService::AddMessageToQueue(
    types::protocols::lora_mesh::SlotAllocation::SlotType type,
    std::unique_ptr<BaseMessage> message) {

    size_t idx = static_cast<size_t>(type);
    if (idx == 0 || idx >= kNumSlotTypes) {
        LOG_ERROR(
            "Invalid slot type: %s",
            types::protocols::lora_mesh::slot_utils::SlotTypeToString(type)
                .c_str());
        return Result(LoraMesherErrorCode::kInvalidArgument,
                      "Invalid slot type");
    }

    os::LockGuard lock(queue_mutex_);
    auto& queue = message_queues_[idx];

    if (max_queue_size_ > 0 && queue.size() >= max_queue_size_) {
        LOG_WARNING(
            "Queue for slot type %s is full (size=%zu), rejecting new message",
            types::protocols::lora_mesh::slot_utils::SlotTypeToString(type)
                .c_str(),
            queue.size());
        return Result(
            LoraMesherErrorCode::kQueueFull,
            "Queue full for slot type " +
                types::protocols::lora_mesh::slot_utils::SlotTypeToString(
                    type));
    }

    queue.push_back(std::move(message));

    LOG_DEBUG(
        "Added message to queue type %s, new size: %zu",
        types::protocols::lora_mesh::slot_utils::SlotTypeToString(type).c_str(),
        queue.size());

    return Result::Success();
}

std::unique_ptr<BaseMessage> MessageQueueService::ExtractMessageOfType(
    types::protocols::lora_mesh::SlotAllocation::SlotType type) {

    size_t idx = static_cast<size_t>(type);
    if (idx == 0 || idx >= kNumSlotTypes) {
        return nullptr;
    }

    os::LockGuard lock(queue_mutex_);
    auto& queue = message_queues_[idx];

    if (!queue.empty()) {
        std::unique_ptr<BaseMessage> message = std::move(queue.front());
        queue.erase(queue.begin());

        LOG_DEBUG("Extracted message from queue type %d, new size: %zu",
                  static_cast<int>(type), queue.size());

        return message;
    }

    return nullptr;
}

bool MessageQueueService::IsQueueEmpty(
    types::protocols::lora_mesh::SlotAllocation::SlotType type) const {

    size_t idx = static_cast<size_t>(type);
    if (idx == 0 || idx >= kNumSlotTypes) {
        return true;
    }

    os::LockGuard lock(queue_mutex_);
    return message_queues_[idx].empty();
}

size_t MessageQueueService::GetQueueSize(
    types::protocols::lora_mesh::SlotAllocation::SlotType type) const {

    size_t idx = static_cast<size_t>(type);
    if (idx == 0 || idx >= kNumSlotTypes) {
        return 0;
    }

    os::LockGuard lock(queue_mutex_);
    return message_queues_[idx].size();
}

void MessageQueueService::ClearAllQueues() {
    os::LockGuard lock(queue_mutex_);

    for (auto& queue : message_queues_) {
        queue.clear();
    }

    LOG_INFO("All message queues cleared");
}

void MessageQueueService::SetMaxQueueSize(size_t max_size) {
    os::LockGuard lock(queue_mutex_);

    max_queue_size_ = max_size;

    // If the new size is smaller than current queues, truncate them
    if (max_size > 0) {
        for (auto& queue : message_queues_) {
            if (queue.size() > max_size) {
                // Keep only the newest messages (at the end of the vector)
                queue.erase(queue.begin(),
                            queue.begin() + (queue.size() - max_size));

                LOG_INFO("Queue truncated to %zu messages", max_size);
            }
        }
    }
}

size_t MessageQueueService::GetMaxQueueSize() const {
    return max_queue_size_;
}

void MessageQueueService::ClearQueue(
    types::protocols::lora_mesh::SlotAllocation::SlotType type) {

    size_t idx = static_cast<size_t>(type);
    if (idx == 0 || idx >= kNumSlotTypes) {
        return;
    }

    os::LockGuard lock(queue_mutex_);
    message_queues_[idx].clear();
    LOG_INFO("Queue for type %d cleared", static_cast<int>(type));
}

bool MessageQueueService::HasAnyMessages() const {
    os::LockGuard lock(queue_mutex_);

    for (const auto& queue : message_queues_) {
        if (!queue.empty()) {
            return true;
        }
    }

    return false;
}

size_t MessageQueueService::GetTotalMessageCount() const {
    os::LockGuard lock(queue_mutex_);

    size_t total = 0;
    for (const auto& queue : message_queues_) {
        total += queue.size();
    }

    return total;
}

bool MessageQueueService::HasMessage(MessageType type) const {
    os::LockGuard lock(queue_mutex_);

    for (const auto& queue : message_queues_) {
        for (const auto& message : queue) {
            if (message->GetType() == type) {
                return true;
            }
        }
    }
    return false;
}

bool MessageQueueService::RemoveMessage(MessageType type) {
    os::LockGuard lock(queue_mutex_);
    for (auto& queue : message_queues_) {
        auto it = std::find_if(queue.begin(), queue.end(),
                               [type](const std::unique_ptr<BaseMessage>& msg) {
                                   return msg->GetType() == type;
                               });
        if (it != queue.end()) {
            queue.erase(it);
            LOG_INFO("Removed message of type %d from queue",
                     static_cast<int>(type));
            return true;
        }
    }
    return false;
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
