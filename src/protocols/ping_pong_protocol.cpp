/**
 * @file ping_pong_protocol.cpp
 * @brief Implementation of the PingPong protocol
 */

#include "protocols/ping_pong_protocol.hpp"

#include <algorithm>
#include <chrono>

#include "os/os_port.hpp"
#include "ping_pong_protocol.hpp"
#include "types/radio/radio_event.hpp"

namespace loramesher {
namespace protocols {

PingPongProtocol::PingPongProtocol()
    : Protocol(ProtocolType::kPingPong),
      current_sequence_number_(0),
      timeout_task_handle_(nullptr),
      stop_tasks_(false) {}

PingPongProtocol::~PingPongProtocol() {
    Stop();
}

Result PingPongProtocol::Start() {
    // Ensure hardware is available
    if (!hardware_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Hardware not initialized in PingPong protocol");
    }

    // Start hardware
    Result hw_result = hardware_->Start();
    if (!hw_result) {
        LOG_ERROR("Failed to start hardware: %s",
                  hw_result.GetErrorMessage().c_str());
        return hw_result;
    }

    LOG_INFO("Starting PingPong protocol");

    return Result::Success();
}

Result PingPongProtocol::Stop() {
    LOG_INFO("Stopping PingPong protocol");

    if (stop_tasks_ == true) {
        return Result::Success();
    }

    // Stop timeout task
    stop_tasks_ = true;

    // If task handle is valid, delete the task
    if (timeout_task_handle_ != nullptr) {
        GetRTOS().DeleteTask(timeout_task_handle_);
        timeout_task_handle_ = nullptr;
    }

    if (process_task_handle_ != nullptr) {
        GetRTOS().DeleteTask(process_task_handle_);
        process_task_handle_ = nullptr;
    }

    if (message_queue_ != nullptr) {
        GetRTOS().DeleteQueue(message_queue_);
        message_queue_ = nullptr;
    }

    return Result::Success();
}

Result PingPongProtocol::Init(
    std::shared_ptr<hardware::IHardwareManager> hardware,
    AddressType node_address) {
    // Store hardware reference and node address
    hardware_ = hardware;
    node_address_ = node_address;

    // Reset sequence number
    current_sequence_number_ = 0;

    // Clear any pending pings
    pending_pings_.clear();

    // Reset the stop flag
    stop_tasks_ = false;

    // Initialize message queue
    message_queue_ = GetRTOS().CreateQueue(
        MESSAGE_QUEUE_SIZE, sizeof(std::unique_ptr<radio::RadioEvent>*));
    if (!message_queue_) {
        return Result(LoraMesherErrorCode::kConfigurationError,
                      "Failed to create message queue for PingPong protocol");
    }

    // Create the timeout checking task
    bool timeout_task_created = GetRTOS().CreateTask(
        TimeoutCheckTaskFunction, "PingPongTimeout", TIMEOUT_TASK_STACK_SIZE,
        this, TIMEOUT_TASK_PRIORITY, &timeout_task_handle_);

    if (!timeout_task_created) {
        return Result(
            LoraMesherErrorCode::kConfigurationError,
            "Failed to create timeout checking task for PingPong protocol");
    }

    // Create message processing task
    bool process_task_created = GetRTOS().CreateTask(
        MessageProcessTaskFunction, "PingPongProcess", PROCESS_TASK_STACK_SIZE,
        this, PROCESS_TASK_PRIORITY, &process_task_handle_);

    if (!process_task_created) {
        // Clean up timeout task
        GetRTOS().DeleteTask(timeout_task_handle_);
        timeout_task_handle_ = nullptr;

        return Result(
            LoraMesherErrorCode::kConfigurationError,
            "Failed to create message processing task for PingPong protocol");
    }

    // Set up hardware event callback - just enqueues the message
    Result hw_result = hardware_->setActionReceive(
        [this](std::unique_ptr<radio::RadioEvent> event) {
            // Create a copy of the pointer that can be passed to the queue
            auto event_ptr =
                new std::unique_ptr<radio::RadioEvent>(std::move(event));

            // Send to queue with short timeout (shouldn't block in interrupt)
            auto queue_result =
                GetRTOS().SendToQueue(message_queue_, &event_ptr, 0);

            // If queue is full or error, clean up to avoid memory leak
            if (queue_result != os::QueueResult::kOk) {
                LOG_ERROR("Error sending to queue %d", queue_result);
                delete event_ptr;
            }
        });

    if (!hw_result) {
        LOG_ERROR("Failed to set hardware callback: %s",
                  hw_result.GetErrorMessage().c_str());
        return hw_result;
    }

    return Result::Success();
}

/**
 * @brief Message processing task function
 * 
 * @param parameters Pointer to PingPongProtocol instance
 */
void PingPongProtocol::MessageProcessTaskFunction(void* parameters) {
    // Get the protocol instance from the parameters
    PingPongProtocol* protocol = static_cast<PingPongProtocol*>(parameters);
    if (!protocol) {
        return;
    }

    // RTOS instance
    auto& rtos = GetRTOS();

    // Pointer to receive from queue
    std::unique_ptr<radio::RadioEvent>* event_ptr = nullptr;

    // Task loop
    while (!protocol->stop_tasks_ && !rtos.ShouldStopOrPause()) {
        // Wait for a message from the queue
        if (rtos.ReceiveFromQueue(protocol->message_queue_, &event_ptr,
                                  QUEUE_WAIT_TIMEOUT_MS) ==
            os::QueueResult::kOk) {
            // Process the event
            Result result =
                protocol->ProcessReceivedRadioEvent(std::move(*event_ptr));

            if (!result) {
                LOG_ERROR(result.GetErrorMessage().c_str());
            }

            // Clean up the pointer wrapper
            delete event_ptr;
        }
    }

    // Task cleanup
    rtos.DeleteTask(nullptr);  // Delete self
}

void PingPongProtocol::TimeoutCheckTaskFunction(void* parameters) {
    // Get the protocol instance from the parameters
    PingPongProtocol* protocol = static_cast<PingPongProtocol*>(parameters);
    if (!protocol) {
        return;
    }

    // RTOS instance for delay and other operations
    auto& rtos = GetRTOS();

    // Task loop
    while (!protocol->stop_tasks_ && !rtos.ShouldStopOrPause()) {
        // Check for timeouts
        protocol->CheckTimeouts();

        // Delay before next check
        rtos.delay(TIMEOUT_CHECK_INTERVAL_MS);
    }

    // Task cleanup
    rtos.DeleteTask(nullptr);  // Delete self
}

Result PingPongProtocol::ProcessReceivedRadioEvent(
    std::unique_ptr<radio::RadioEvent> event) {
    LOG_DEBUG("Processing received radio event in Ping Pong protocol");

    // Verify this is a received event
    if (event->getType() != radio::RadioEventType::kReceived) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Event is not a received event type");
    }

    // Get the message from the event
    auto message = event->TakeMessage();
    if (message == nullptr) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Received event contains no message");
    }

    // Verify this is a DATA type message (required for PingPong)
    if (message_type::GetMainType(message->GetHeader().GetType()) !=
        MessageType::CONTROL_MSG) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Message is not a CONTROL_MSG type message");
    }

    // Serialize the message to pass to the PingPong deserializer
    auto serialized_message = message->Serialize();
    if (!serialized_message) {
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to serialize message for PingPong processing");
    }

    // Try to interpret as a PingPong message
    auto ping_pong_message_opt =
        PingPongMessage::CreateFromSerialized(*serialized_message);

    if (!ping_pong_message_opt) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Failed to parse as a valid PingPong message");
    }

    // Process based on subtype
    const PingPongMessage& ping_pong_message = *ping_pong_message_opt;
    switch (ping_pong_message.GetSubtype()) {
        case PingPongSubtype::PING:
            return ProcessPing(ping_pong_message);
        case PingPongSubtype::PONG:
            return ProcessPong(ping_pong_message);
        default:
            // This should never happen as CreateFromSerialized validates subtypes,
            // but included for robustness
            return Result(LoraMesherErrorCode::kInvalidParameter,
                          "Unknown PingPong subtype: " +
                              std::to_string(static_cast<int>(
                                  ping_pong_message.GetSubtype())));
    }
}

Result PingPongProtocol::SendMessage(const BaseMessage& message) {
    if (!hardware_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Hardware not initialized in PingPong protocol");
    }

    LOG_DEBUG("PingPong protocol sending message");

    // Forward to hardware for transmission
    return hardware_->SendMessage(message);
}

Result PingPongProtocol::SendPing(AddressType destination, AddressType source,
                                  uint32_t timeout_ms,
                                  PingCompletionCallback callback) {
    // If source is not specified (0), use the node address
    if (source == 0) {
        source = node_address_;
    }

    // Get the next sequence number
    uint16_t seq_num = GetNextSequenceNumber();

    // Get current timestamp
    uint32_t timestamp = GetCurrentTimestamp();

    // Create a ping message
    auto ping_message_opt = PingPongMessage::Create(
        destination, source, PingPongSubtype::PING, seq_num, timestamp);

    if (!ping_message_opt) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Failed to create PingPong message");
    }

    // Add to pending pings
    PendingPing pending;
    pending.sequence_number = seq_num;
    pending.timestamp = timestamp;
    pending.sent_time = std::chrono::steady_clock::now();
    pending.timeout_ms = timeout_ms;
    pending.callback = callback;

    pending_pings_[destination][seq_num] = pending;

    // Send the message
    return SendMessage(ping_message_opt->ToBaseMessage());
}

void PingPongProtocol::CheckTimeouts() {
    auto now = std::chrono::steady_clock::now();

    // Check each destination
    for (auto dest_it = pending_pings_.begin();
         dest_it != pending_pings_.end();) {
        // Check each pending ping for this destination
        for (auto ping_it = dest_it->second.begin();
             ping_it != dest_it->second.end();) {
            const PendingPing& pending = ping_it->second;

            // Calculate elapsed time
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - pending.sent_time)
                    .count();

            // Check if timed out
            if (elapsed > pending.timeout_ms) {
                // Ping timed out, call the callback if provided
                if (pending.callback) {
                    pending.callback(dest_it->first, 0, false);
                }

                // Remove this pending ping
                ping_it = dest_it->second.erase(ping_it);
            } else {
                ++ping_it;
            }
        }

        // If no more pending pings for this destination, remove the destination
        if (dest_it->second.empty()) {
            dest_it = pending_pings_.erase(dest_it);
        } else {
            ++dest_it;
        }
    }

    // Yield to other tasks of equal priority
    GetRTOS().YieldTask();
}

Result PingPongProtocol::SendPong(const PingPongMessage& ping_message) {
    // Create a pong response
    auto pong_message_opt = PingPongMessage::Create(
        ping_message.GetHeader().GetSource(),  // Destination is the ping source
        ping_message.GetHeader().GetDestination(),  // Source is our address
        PingPongSubtype::PONG,
        ping_message.GetSequenceNumber(),  // Same sequence number
        ping_message.GetTimestamp());      // Echo the timestamp

    if (!pong_message_opt) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Failed to create PONG message");
    }

    // Send the pong
    return SendMessage(pong_message_opt->ToBaseMessage());
}

Result PingPongProtocol::ProcessPing(const PingPongMessage& message) {
    // Respond with a pong
    return SendPong(message);
}

Result PingPongProtocol::ProcessPong(const PingPongMessage& message) {
    AddressType source = message.GetHeader().GetSource();
    uint16_t seq_num = message.GetSequenceNumber();

    // Find the corresponding pending ping
    auto dest_it = pending_pings_.find(source);
    if (dest_it == pending_pings_.end()) {
        // No pending pings for this source
        return Result(
            LoraMesherErrorCode::kInvalidState,
            "No pending pings for source address: " + std::to_string(source));
    }

    auto ping_it = dest_it->second.find(seq_num);
    if (ping_it == dest_it->second.end()) {
        // No pending ping with this sequence number
        return Result(
            LoraMesherErrorCode::kInvalidState,
            "No pending ping with sequence number: " + std::to_string(seq_num));
    }

    // Calculate RTT
    uint32_t rtt = message.CalculateRTT(ping_it->second.timestamp);

    // Call the callback if provided
    if (ping_it->second.callback) {
        ping_it->second.callback(source, rtt, true);
    }

    // Remove the pending ping
    dest_it->second.erase(ping_it);

    // If no more pending pings for this destination, remove the destination
    if (dest_it->second.empty()) {
        pending_pings_.erase(dest_it);
    }

    return Result::Success();
}

uint16_t PingPongProtocol::GetNextSequenceNumber() {
    return current_sequence_number_++;
}

uint32_t PingPongProtocol::GetCurrentTimestamp() const {
    // Use system time as timestamp
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

}  // namespace protocols
}  // namespace loramesher