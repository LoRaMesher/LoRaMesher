/**
 * @file i_message_queue_service.hpp
 * @brief Interface for message queue management
 */

#pragma once

#include <memory>
#include "types/messages/message_type.hpp"
#include "types/protocols/lora_mesh/slot_allocation.hpp"

namespace loramesher {
namespace protocols {

/**
 * @brief Interface for message queue service
 * 
 * Handles message queues for different message types
 */
class IMessageQueueService {
   public:
    virtual ~IMessageQueueService() = default;

    /**
     * @brief Add a message to the queue
     * 
     * @param type Message slot type
     * @param message Unique pointer to the message
     */
    virtual void AddMessageToQueue(
        loramesher::types::protocols::lora_mesh::SlotAllocation::SlotType type,
        std::unique_ptr<BaseMessage> message) = 0;

    /**
     * @brief Extract the first message of a specific type from the queue
     * 
     * @param type Message type to retrieve
     * @return std::unique_ptr<BaseMessage> The extracted message, or nullptr if none found
     */
    virtual std::unique_ptr<BaseMessage> ExtractMessageOfType(
        loramesher::types::protocols::lora_mesh::SlotAllocation::SlotType
            type) = 0;

    /**
     * @brief Check if a queue for a specific message type is empty
     * 
     * @param type Message type to check
     * @return bool True if queue is empty, false otherwise
     */
    virtual bool IsQueueEmpty(
        loramesher::types::protocols::lora_mesh::SlotAllocation::SlotType type)
        const = 0;

    /**
     * @brief Get the count of messages in a specific queue
     * 
     * @param type Message type queue to check
     * @return size_t Number of messages in the queue
     */
    virtual size_t GetQueueSize(
        loramesher::types::protocols::lora_mesh::SlotAllocation::SlotType type)
        const = 0;

    /**
     * @brief Clear all message queues
     */
    virtual void ClearAllQueues() = 0;

    /**
     * @brief Returns true if any queue has messages of type
     * @param type Type of message to check
     * @return bool True if any queue has messages of the specified type
     */
    virtual bool HasMessage(MessageType type) const = 0;

    /**
     * @brief Remove the first occurrence of a specific message type from any queue
     * @param type Type of message to remove
     * @return true if a message was removed, false if none found
     */
    virtual bool RemoveMessage(MessageType type) = 0;
};

}  // namespace protocols
}  // namespace loramesher