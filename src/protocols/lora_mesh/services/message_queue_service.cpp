/**
 * @file message_queue_service.cpp
 * @brief Implementation of message queue service
 */

#include <numeric>
#include "message_queue_service.hpp"
#include "utils/logger.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

MessageQueueService::MessageQueueService(size_t max_queue_size)
    : max_queue_size_(max_queue_size) {}

void MessageQueueService::AddMessageToQueue(
    types::protocols::lora_mesh::SlotAllocation::SlotType type,
    std::unique_ptr<BaseMessage> message) {

    std::lock_guard<std::mutex> lock(queue_mutex_);

    // Check if queue has reached maximum size
    if (max_queue_size_ > 0 &&
        message_queues_[type].size() >= max_queue_size_) {
        LOG_WARNING("Queue for type %d is full, dropping oldest message",
                    static_cast<int>(type));

        // Remove oldest message (front of queue)
        if (!message_queues_[type].empty()) {
            message_queues_[type].erase(message_queues_[type].begin());
        }
    }

    // Add the new message to the queue
    message_queues_[type].push_back(std::move(message));

    LOG_DEBUG("Added message to queue type %d, new size: %zu",
              static_cast<int>(type), message_queues_[type].size());
}

std::unique_ptr<BaseMessage> MessageQueueService::ExtractMessageOfType(
    types::protocols::lora_mesh::SlotAllocation::SlotType type) {

    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = message_queues_.find(type);
    if (it != message_queues_.end() && !it->second.empty()) {
        // Get the first message
        std::unique_ptr<BaseMessage> message = std::move(it->second.front());

        // Remove it from the queue
        it->second.erase(it->second.begin());

        LOG_DEBUG("Extracted message from queue type %d, new size: %zu",
                  static_cast<int>(type), it->second.size());

        // Return the message (will be moved)
        return message;
    }

    // No message found
    return nullptr;
}

bool MessageQueueService::IsQueueEmpty(
    types::protocols::lora_mesh::SlotAllocation::SlotType type) const {

    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = message_queues_.find(type);
    return (it == message_queues_.end() || it->second.empty());
}

size_t MessageQueueService::GetQueueSize(
    types::protocols::lora_mesh::SlotAllocation::SlotType type) const {

    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = message_queues_.find(type);
    return (it != message_queues_.end()) ? it->second.size() : 0;
}

void MessageQueueService::ClearAllQueues() {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    for (auto& queue_pair : message_queues_) {
        queue_pair.second.clear();
    }

    LOG_INFO("All message queues cleared");
}

void MessageQueueService::SetMaxQueueSize(size_t max_size) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    max_queue_size_ = max_size;

    // If the new size is smaller than current queues, truncate them
    if (max_size > 0) {
        for (auto& queue_pair : message_queues_) {
            if (queue_pair.second.size() > max_size) {
                // Keep only the newest messages (at the end of the vector)
                queue_pair.second.erase(
                    queue_pair.second.begin(),
                    queue_pair.second.begin() +
                        (queue_pair.second.size() - max_size));

                LOG_INFO("Queue for type %d truncated to %zu messages",
                         static_cast<int>(queue_pair.first), max_size);
            }
        }
    }
}

size_t MessageQueueService::GetMaxQueueSize() const {
    return max_queue_size_;
}

void MessageQueueService::ClearQueue(
    types::protocols::lora_mesh::SlotAllocation::SlotType type) {

    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = message_queues_.find(type);
    if (it != message_queues_.end()) {
        it->second.clear();
        LOG_INFO("Queue for type %d cleared", static_cast<int>(type));
    }
}

bool MessageQueueService::HasAnyMessages() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    for (const auto& queue_pair : message_queues_) {
        if (!queue_pair.second.empty()) {
            return true;
        }
    }

    return false;
}

size_t MessageQueueService::GetTotalMessageCount() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    size_t total = 0;
    for (const auto& queue_pair : message_queues_) {
        total += queue_pair.second.size();
    }

    return total;
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher