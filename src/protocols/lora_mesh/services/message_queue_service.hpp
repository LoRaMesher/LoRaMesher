/**
 * @file message_queue_service.hpp
 * @brief Implementation of message queue service
 */

#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include "protocols/lora_mesh/interfaces/i_message_queue_service.hpp"
#include "types/messages/base_message.hpp"
#include "types/protocols/lora_mesh/slot_allocation.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Implementation of message queue service
 * 
 * Manages queues for different message types
 */
class MessageQueueService : public IMessageQueueService {
   public:
    /**
     * @brief Constructor
     * 
     * @param max_queue_size Maximum size for each queue (0 = unlimited)
     */
    explicit MessageQueueService(size_t max_queue_size = 0);

    /**
     * @brief Virtual destructor
     */
    virtual ~MessageQueueService() = default;

    // IMessageQueueService interface implementation
    void AddMessageToQueue(
        types::protocols::lora_mesh::SlotAllocation::SlotType type,
        std::unique_ptr<BaseMessage> message) override;

    std::unique_ptr<BaseMessage> ExtractMessageOfType(
        types::protocols::lora_mesh::SlotAllocation::SlotType type) override;

    bool IsQueueEmpty(types::protocols::lora_mesh::SlotAllocation::SlotType
                          type) const override;

    size_t GetQueueSize(types::protocols::lora_mesh::SlotAllocation::SlotType
                            type) const override;

    void ClearAllQueues() override;

    /**
     * @brief Set maximum queue size
     * 
     * @param max_size Maximum size (0 = unlimited)
     */
    void SetMaxQueueSize(size_t max_size);

    /**
     * @brief Get maximum queue size
     * 
     * @return size_t Maximum queue size
     */
    size_t GetMaxQueueSize() const;

    /**
     * @brief Clear a specific message queue
     * 
     * @param type Type of queue to clear
     */
    void ClearQueue(types::protocols::lora_mesh::SlotAllocation::SlotType type);

    /**
     * @brief Check if any queues have messages
     * 
     * @return bool True if any queue has messages
     */
    bool HasAnyMessages() const;

    /**
     * @brief Get total messages across all queues
     * 
     * @return size_t Total message count
     */
    size_t GetTotalMessageCount() const;

    /**
     * @brief Check if a specific message type queue has messages
     * @param type Type of message to check
     * @return bool True if the queue has messages, false otherwise
     */
    bool HasMessage(MessageType type) const override;

   private:
    std::unordered_map<types::protocols::lora_mesh::SlotAllocation::SlotType,
                       std::vector<std::unique_ptr<BaseMessage>>>
        message_queues_;
    size_t max_queue_size_;
    mutable std::mutex queue_mutex_;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher