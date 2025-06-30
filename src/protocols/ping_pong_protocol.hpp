/**
 * @file ping_pong_protocol.hpp
 * @brief Definition of PingPong protocol for connectivity testing
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include "hardware/hardware_manager.hpp"
#include "os/rtos.hpp"
#include "types/messages/ping_pong/ping_pong_message.hpp"
#include "types/protocols/protocol.hpp"

namespace loramesher {
namespace protocols {

/**
  * @brief Callback type for PingPong completion notifications
  * 
  * @param address The address that responded
  * @param rtt The round-trip time in milliseconds
  * @param success Whether the ping was successful
  */
using PingCompletionCallback =
    std::function<void(AddressType address, uint32_t rtt, bool success)>;

/**
  * @brief Implementation of the PingPong protocol for connectivity testing
  * 
  * This protocol allows basic connectivity testing through PING/PONG message
  * exchanges, with support for measuring round-trip times and handling timeouts.
  */
class PingPongProtocol : public Protocol {
   public:
    /**
      * @brief Constructor for PingPongProtocol
      */
    PingPongProtocol();

    /**
      * @brief Destructor for PingPongProtocol
      * 
      * Ensures the timeout checking task is properly cleaned up.
      */
    ~PingPongProtocol() override;

    /**
      * @brief Initialize the protocol
      * 
      * Creates and starts the timeout checking task.
      * 
      * @param hardware Shared pointer to the hardware manager
      * @param node_address The address of this node
      * @return Result Success if initialization was successful, error details otherwise
      */
    Result Init(std::shared_ptr<hardware::IHardwareManager> hardware,
                AddressType node_address) override;

    /**
      * @brief Start the protocol operation
      *
      * @return Result Success if started successfully, error details otherwise
      */
    Result Start() override;

    /**
      * @brief Stop the protocol operation
      *
      * @return Result Success if stopped successfully, error details otherwise
      */
    Result Stop() override;

    /**
      * @brief Process a received radio event according to PingPong protocol
      * 
      * @param event The radio event to be processed
      * @return Result Success if message was processed successfully, error details otherwise
      */
    Result ProcessReceivedRadioEvent(std::unique_ptr<radio::RadioEvent> event);

    /**
      * @brief Send a message using the PingPong protocol
      * 
      * This is a generic message sending method inherited from Protocol.
      * 
      * @param message The message to send
      * @return Result Success if message was sent successfully, error details otherwise
      */
    Result SendMessage(const BaseMessage& message) override;

    /**
      * @brief Send a ping to the specified address
      * 
      * @param destination The address to ping
      * @param source The source address (our address)
      * @param timeout_ms Timeout in milliseconds
      * @param callback Optional callback for ping completion notification
      * @return Result Success if ping was sent successfully, error details otherwise
      */
    Result SendPing(AddressType destination, AddressType source,
                    uint32_t timeout_ms = 1000,
                    PingCompletionCallback callback = nullptr);

    /**
      * @brief Check for ping timeouts and handle them
      * 
      * This method should be called periodically to check for timeouts.
      */
    void CheckTimeouts();

   private:
    /**
      * @brief Structure to track pending pings
      */
    struct PendingPing {
        uint16_t sequence_number; /**< Sequence number of the ping */
        uint32_t timestamp;       /**< Timestamp when the ping was sent */
        std::chrono::steady_clock::time_point
            sent_time;                   /**< System time when ping was sent */
        uint32_t timeout_ms;             /**< Timeout period in milliseconds */
        PingCompletionCallback callback; /**< Callback for ping completion */
    };

    /**
      * @brief Static task function for timeout checking
      * 
      * @param parameters Pointer to the PingPongProtocol instance
      */
    static void TimeoutCheckTaskFunction(void* parameters);

    /**
      * @brief Send a pong response to a ping
      * 
      * @param ping_message The ping message to respond to
      * @return Result Success if pong was sent successfully, error details otherwise
      */
    Result SendPong(const PingPongMessage& ping_message);

    /**
      * @brief Process a ping message
      * 
      * @param message The ping message to process
      * @return Result Success if ping was processed successfully, error details otherwise
      */
    Result ProcessPing(const PingPongMessage& message);

    /**
      * @brief Process a pong message
      * 
      * @param message The pong message to process
      * @return Result Success if pong was processed successfully, error details otherwise
      */
    Result ProcessPong(const PingPongMessage& message);

    /**
      * @brief Get the next sequence number for ping requests
      * 
      * @return uint16_t The next sequence number
      */
    uint16_t GetNextSequenceNumber();

    /**
      * @brief Get the current timestamp
      * 
      * @return uint32_t Current timestamp value
      */
    uint32_t GetCurrentTimestamp() const;

    std::unordered_map<AddressType, std::unordered_map<uint16_t, PendingPing>>
        pending_pings_;  ///< Map of pending pings by destination address and sequence number

    uint16_t current_sequence_number_;  ///< Current sequence number counter
    os::TaskHandle_t
        timeout_task_handle_;          ///< Handle to the timeout checking task
    os::QueueHandle_t message_queue_;  ///< Queue for received radio events
    os::TaskHandle_t
        process_task_handle_;  ///< Handle for message processing task

    bool stop_tasks_;  ///< Flag to control the timeout checking task

    // Constants
    static constexpr uint32_t TIMEOUT_CHECK_INTERVAL_MS =
        1000;  ///< Interval for timeout checks in milliseconds
    static constexpr uint32_t TIMEOUT_TASK_STACK_SIZE =
        2048;  ///< TODO: Change it to use the configStack.
    ///Stack size for the timeout checking task
    static constexpr uint32_t TIMEOUT_TASK_PRIORITY =
        3;  ///< Priority for the timeout checking task
    static constexpr size_t MESSAGE_QUEUE_SIZE =
        10;  ///< Maximum number of messages in queue
    static constexpr uint32_t QUEUE_WAIT_TIMEOUT_MS =
        100;  ///< Wait timeout for queue operations
    static constexpr uint32_t PROCESS_TASK_STACK_SIZE =
        2048;  ///< Stack size for processing task
    static constexpr uint32_t PROCESS_TASK_PRIORITY =
        3;  ///< Priority for processing task

    static void MessageProcessTaskFunction(
        void* parameters);  ///< Message processing task function
};

}  // namespace protocols
}  // namespace loramesher