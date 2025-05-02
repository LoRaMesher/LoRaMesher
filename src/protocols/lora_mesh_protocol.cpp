/**
 * @file lora_mesh_protocol.cpp
 * @brief Implementation of the LoRaMesh protocol
 */

#include "protocols/lora_mesh_protocol.hpp"

#include <algorithm>
#include <chrono>

#include "lora_mesh_protocol.hpp"
#include "os/os_port.hpp"
#include "types/radio/radio_event.hpp"

namespace loramesher {
namespace protocols {

LoRaMeshProtocol::LoRaMeshProtocol()
    : Protocol(ProtocolType::kLoraMesh),
      radio_task_handle_(nullptr),
      slot_manager_task_handle_(nullptr),
      protocol_event_queue_(nullptr),
      state_(ProtocolState::INITIALIZING),
      stop_tasks_(false),
      current_slot_(0),
      is_synchronized_(false),
      network_manager_(0),
      last_sync_time_(0) {
    // Initialize superframe with default values
    current_superframe_.total_slots = DEFAULT_SLOTS_PER_SUPERFRAME;
    current_superframe_.data_slots = DEFAULT_SLOTS_PER_SUPERFRAME * 6 / 10;
    current_superframe_.discovery_slots = DEFAULT_SLOTS_PER_SUPERFRAME * 2 / 10;
    current_superframe_.control_slots = DEFAULT_SLOTS_PER_SUPERFRAME * 2 / 10;
    current_superframe_.slot_duration_ms = DEFAULT_SLOT_DURATION_MS;
    current_superframe_.superframe_start_time = 0;
}

LoRaMeshProtocol::~LoRaMeshProtocol() {
    Stop();
}

Result LoRaMeshProtocol::Init(
    std::shared_ptr<hardware::IHardwareManager> hardware,
    AddressType node_address) {
    // Store hardware reference and node address
    hardware_ = hardware;
    node_address_ = node_address;

    // Reset protocol state
    state_ = ProtocolState::INITIALIZING;
    stop_tasks_ = false;
    current_slot_ = 0;
    is_synchronized_ = false;
    network_manager_ = 0;
    last_sync_time_ = 0;

    // Clear all data structures
    routing_table_.clear();
    network_nodes_.clear();
    slot_table_.clear();
    message_queue_.clear();

    // Initialize the queues
    protocol_event_queue_ = GetRTOS().CreateQueue(
        RADIO_QUEUE_SIZE, sizeof(std::unique_ptr<radio::RadioEvent>*));
    if (!protocol_event_queue_) {
        return Result(
            LoraMesherErrorCode::kConfigurationError,
            "Failed to create protocol event queue for LoRaMesh protocol");
    }

    // Create the tasks
    bool protocol_logic_task_created = GetRTOS().CreateTask(
        ProtocolLogicTaskFunction, "LoRaMeshProtocolLogic",
        RADIO_TASK_STACK_SIZE, this, TASK_PRIORITY, &radio_task_handle_);

    if (!protocol_logic_task_created) {
        // Clean up queues
        GetRTOS().DeleteQueue(protocol_event_queue_);
        protocol_event_queue_ = nullptr;

        return Result(
            LoraMesherErrorCode::kConfigurationError,
            "Failed to create protocol logic task for LoRaMesh protocol");
    }

    bool slot_task_created = GetRTOS().CreateTask(
        SlotManagerTaskFunction, "LoRaMeshSlot", SLOT_MANAGER_TASK_STACK_SIZE,
        this, TASK_PRIORITY, &slot_manager_task_handle_);

    if (!slot_task_created) {
        // Clean up existing task and queues
        GetRTOS().DeleteTask(radio_task_handle_);
        GetRTOS().DeleteQueue(protocol_event_queue_);
        radio_task_handle_ = nullptr;
        protocol_event_queue_ = nullptr;

        return Result(
            LoraMesherErrorCode::kConfigurationError,
            "Failed to create slot manager task for LoRaMesh protocol");
    }

    // Set up hardware event callback
    Result hw_result = hardware_->setActionReceive(
        [this](std::unique_ptr<radio::RadioEvent> event) {
            // Create a copy of the pointer that can be passed to the queue
            auto event_ptr =
                new std::unique_ptr<radio::RadioEvent>(std::move(event));

            // Send to queue with short timeout (shouldn't block in interrupt)
            auto queue_result =
                GetRTOS().SendToQueue(protocol_event_queue_, &event_ptr, 0);

            // If queue is full or error, clean up to avoid memory leak
            if (queue_result != os::QueueResult::kOk) {
                delete event_ptr;
            }
        });

    if (!hw_result) {
        return hw_result;
    }

    // Apply default configuration
    config_ = LoRaMeshProtocolConfig(node_address);

    return Result::Success();
}

Result LoRaMeshProtocol::Configure(const LoRaMeshProtocolConfig& config) {
    // Validate configuration
    std::string validation_error = config.Validate();
    if (!validation_error.empty()) {
        return Result(LoraMesherErrorCode::kInvalidParameter, validation_error);
    }

    // Apply configuration
    config_ = config;

    // Update node address if different
    if (node_address_ != config.getNodeAddress() &&
        config.getNodeAddress() != 0) {
        node_address_ = config.getNodeAddress();
    }

    // TODO: Test if the default slot extra duration works. Addapt to a test duration
    current_superframe_.slot_duration_ms =
        hardware_->getTimeOnAir(config.getMaxPacketSize()) +
        DEFAULT_SLOT_EXTRA_DURATION_MS;

    return Result::Success();
}

Result LoRaMeshProtocol::Start() {
    // Ensure hardware is available
    if (!hardware_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Hardware not initialized in LoRaMesh protocol");
    }

    // Start hardware
    Result hw_result = hardware_->Start();
    if (!hw_result) {
        return hw_result;
    }

    // TODO: Send initial discovery messages to find network

    return Result::Success();
}

Result LoRaMeshProtocol::Stop() {
    // Signal all tasks to stop
    stop_tasks_ = true;

    // Clean up tasks
    if (radio_task_handle_ != nullptr) {
        GetRTOS().DeleteTask(radio_task_handle_);
        radio_task_handle_ = nullptr;
    }

    if (slot_manager_task_handle_ != nullptr) {
        GetRTOS().DeleteTask(slot_manager_task_handle_);
        slot_manager_task_handle_ = nullptr;
    }

    // Clean up queues
    if (protocol_event_queue_ != nullptr) {
        GetRTOS().DeleteQueue(protocol_event_queue_);
        protocol_event_queue_ = nullptr;
    }

    return Result::Success();
}

void LoRaMeshProtocol::ProtocolLogicTaskFunction(void* parameters) {
    // Get protocol instance
    LoRaMeshProtocol* protocol = static_cast<LoRaMeshProtocol*>(parameters);
    if (!protocol) {
        return;
    }

    // RTOS instance
    auto& rtos = GetRTOS();

    // Pointer to receive from queue
    std::unique_ptr<radio::RadioEvent>* event_ptr = nullptr;

    Result result = Result::Success();

    // Update state to discovery
    protocol->state_ = ProtocolState::DISCOVERY;
    protocol->current_superframe_.superframe_start_time =
        protocol->GetCurrentTime();

    // Task loop
    while (!protocol->stop_tasks_ && !rtos.ShouldStopOrPause()) {
        // Handle protocol state machine
        switch (protocol->state_) {
            case ProtocolState::DISCOVERY:
                result = protocol->LogicDiscovery();
                if (!result) {
                    // Handle error
                    LOG_ERROR("Discovery logic failed: %s",
                              result.GetErrorMessage().c_str());
                }
                break;
            case ProtocolState::JOINING:
                result = protocol->LogicJoining();
                if (!result) {
                    // Handle error
                    LOG_ERROR("Joining logic failed: %s",
                              result.GetErrorMessage().c_str());
                }
                break;
            case ProtocolState::NORMAL_OPERATION:
                // Handle normal operation state
                break;
            case ProtocolState::NETWORK_MANAGER:
                // Handle network manager state
                break;
            default:
                LOG_ERROR("Invalid protocol state: %d",
                          static_cast<int>(protocol->state_));
                // Invalid state, log error and exit
                protocol->Stop();
                return;
        }
        // Give other tasks a chance to run
        rtos.YieldTask();
    }

    // Task cleanup
    rtos.DeleteTask(nullptr);  // Delete self
}

Result LoRaMeshProtocol::ProcessReceivedRadioEvent(
    std::unique_ptr<radio::RadioEvent> event) {
    // TODO: Implement
    return Result(LoraMesherErrorCode::kNotImplemented,
                  "ProcessReceivedRadioEvent not implemented yet");
}

void LoRaMeshProtocol::SlotManagerTaskFunction(void* parameters) {
    // Get protocol instance
    LoRaMeshProtocol* protocol = static_cast<LoRaMeshProtocol*>(parameters);
    if (!protocol) {
        return;
    }

    // RTOS instance
    auto& rtos = GetRTOS();

    // Wait until a process notifies this task to start
    rtos.WaitForNotify(MAX_DELAY);

    // Initialize slot tracking
    uint16_t current_slot = protocol->current_slot_;
    uint32_t last_slot_time = protocol->GetCurrentTime();
    uint32_t slot_duration = protocol->current_superframe_.slot_duration_ms;

    // Task loop
    while (!protocol->stop_tasks_ && !rtos.ShouldStopOrPause()) {

        // Get current time
        uint32_t current_time = protocol->GetCurrentTime();

        // Check if we should move to next slot
        // TODO: What happen if the time is not synchronized? Just wait for the next slot?
        // TODO: If the actual slot is less than maxtimeonair, wait for the next slot?
        if (current_time - last_slot_time >= slot_duration) {
            // Move to next slot
            current_slot =
                (current_slot + 1) % protocol->current_superframe_.total_slots;
            last_slot_time = current_time;

            // Notify protocol of slot transition
            protocol->HandleSlotTransition(current_slot);
        }

        // Check if we should start a new superframe
        if (current_slot == 0 &&
            current_time -
                    protocol->current_superframe_.superframe_start_time >=
                protocol->current_superframe_.total_slots * slot_duration) {

            // Update superframe start time
            protocol->current_superframe_.superframe_start_time = current_time;

            // TODO: Perform superframe transition activities
        }

        // TODO: Need Short delay to prevent busy-waiting?
        // rtos.delay(slot_duration / 10);
        rtos.YieldTask();
    }

    // Task cleanup
    rtos.DeleteTask(nullptr);  // Delete self
}

Result LoRaMeshProtocol::SendMessage(const BaseMessage& message) {
    if (!hardware_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Hardware not initialized in LoRaMesh protocol");
    }

    // TODO: Add to a queue and wait for a slot to send the message
    // Forward to hardware for transmission
    return hardware_->SendMessage(message);
}

void LoRaMeshProtocol::SetRouteUpdateCallback(RouteUpdateCallback callback) {
    route_update_callback_ = callback;
}

Result LoRaMeshProtocol::LogicDiscovery() {
    // Set radio to receive mode
    Result result = hardware_->setState(radio::RadioState::kReceive);
    if (!result) {
        return result;
    }

    uint32_t discovery_timeout_ms =
        GetDiscoveryTimeout();  // Get discovery timeout from config

    LOG_INFO("Starting discovery phase, listening for %d superframes (%d ms)",
             DEFAULT_SUPERFRAMES_TO_CREATE_NEW_NETWORK, discovery_timeout_ms);

    auto& rtos = GetRTOS();

    // Pointer to receive from queue
    std::unique_ptr<radio::RadioEvent>* event_ptr = nullptr;

    uint32_t start_time = GetCurrentTime();
    uint32_t end_time = start_time + discovery_timeout_ms;

    while (!stop_tasks_ && !rtos.ShouldStopOrPause() &&
           GetCurrentTime() < end_time) {
        discovery_timeout_ms = end_time - GetCurrentTime();
        if (discovery_timeout_ms < 0) {
            discovery_timeout_ms = 0;
        }

        // Check for messages from radio event handler
        if (rtos.ReceiveFromQueue(protocol_event_queue_, &event_ptr,
                                  discovery_timeout_ms) ==
            os::QueueResult::kOk) {

            // Process the received message
            Result result = ProcessReceivedRadioEvent(std::move(*event_ptr));

            if (result) {
                // Any valid message received indicates a network is present
                // Transition to JOINING state
                state_ = ProtocolState::JOINING;
                LOG_INFO("Network found, transitioning to joining state");

                // Send the received message to the protocol_event_queue_ to be processed again
                // This is necessary to ensure the message is processed in the joining state

                rtos.SendToQueue(protocol_event_queue_, event_ptr, 0);

                return Result::Success();
            }

            delete event_ptr;
        }
    }

    if (stop_tasks_) {
        LOG_INFO("Discovery phase stopped by user request");
        return Result::Success();
    }

    // No message received, check for timeout
    LOG_INFO("Discovery timeout - no network found, creating new network");

    // Create new network
    SetupNewNetwork();

    return Result::Success();
}

void LoRaMeshProtocol::SetupNewNetwork() {
    // Set the main node as the network manager
    state_ = ProtocolState::NETWORK_MANAGER;
    network_manager_ = node_address_;
    is_synchronized_ = true;  // Assume synchronized for new network
    last_sync_time_ = GetCurrentTime();
    current_superframe_.superframe_start_time = last_sync_time_;
    // Notify the slot manager task to start the superframe
    auto result = GetRTOS().NotifyTask(slot_manager_task_handle_, 0);
    if (result != os::QueueResult::kOk) {
        LOG_ERROR("Notify task failed in setup new network");
    }

    LOG_INFO("New network created, acting as network manager");
}

// Result LoRaMeshProtocol::ProcessRoutingUpdate(
//     AddressType source, const std::vector<uint8_t>& data) {
//     // TODO: Implement proper routing table update logic
//     // For now, just acknowledge receipt

//     // Stub implementation - just record that we've seen this node
//     bool found = false;
//     for (auto& node : network_nodes_) {
//         if (node.address == source) {
//             node.last_seen = GetCurrentTime();
//             found = true;
//             break;
//         }
//     }

//     if (!found) {
//         NetworkNode new_node;
//         new_node.address = source;
//         new_node.battery_level = 100;  // Default to full battery
//         new_node.last_seen = GetCurrentTime();
//         new_node.is_network_manager = false;
//         network_nodes_.push_back(new_node);
//     }

//     return Result::Success();
// }

Result LoRaMeshProtocol::LogicJoining() {
    return Result(LoraMesherErrorCode::kNotImplemented,
                  "TODO: LogicJoining not implemented yet");
}

Result LoRaMeshProtocol::SendRoutingTableUpdate() {
    // TODO: Implement routing table update sending
    // For now, return success without doing anything
    return Result::Success();
}

Result LoRaMeshProtocol::SendSlotRequest(uint8_t num_slots) {
    // TODO: Implement slot request sending
    // For now, return success without doing anything
    return Result::Success();
}

Result LoRaMeshProtocol::SendDiscoveryMessage() {
    // Get the first discovery message
    auto discovery_message = ExtractMessageQueueOfType(MessageType::DISCOVERY);
    if (!discovery_message) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Discovery message queue is empty, nothing to send");
    }

    // Send the discovery message
    Result result = SendMessage(*discovery_message);
    if (!result) {
        return result;
    }

    return Result::Success();
}

Result LoRaMeshProtocol::SendControlMessage() {
    // TODO: This should send at the begining the routing table.
    // TODO: Maybe here it will need to create the routing table and send it.
    // TODO: Maybe the protocol should check if the routing table is empty and create it.
    // Get the first control message
    auto control_message = ExtractMessageQueueOfType(MessageType::CONTROL);
    if (!control_message) {
        LOG_DEBUG("Control message queue is empty, nothing to send");
        return Result::Success();
    }

    // Send the discovery message
    Result result = SendMessage(*control_message);
    if (!result) {
        return result;
    }

    return Result::Success();
}

AddressType LoRaMeshProtocol::FindNextHop(AddressType destination) const {
    // Look for direct route to destination
    for (const auto& entry : routing_table_) {
        if (entry.destination == destination && entry.is_active) {
            return entry.next_hop;
        }
    }

    // If no direct route, find best route by hop count
    AddressType best_next_hop = 0;
    uint8_t best_hop_count =
        config_.getMaxHops() + 1;  // Higher than max allowed

    for (const auto& entry : routing_table_) {
        if (entry.destination == destination &&
            entry.hop_count < best_hop_count && entry.is_active) {
            best_hop_count = entry.hop_count;
            best_next_hop = entry.next_hop;
        }
    }

    return best_next_hop;  // Returns 0 if no route found
}

void LoRaMeshProtocol::HandleSlotTransition(uint16_t slot_number) {
    // Update current slot
    current_slot_ = slot_number;

    // Determine slot type
    SlotAllocation::SlotType slot_type =
        SlotAllocation::SlotType::SLEEP;  // Default to sleep

    // Find slot in allocation table
    for (const auto& slot : slot_table_) {
        if (slot.slot_number == slot_number) {
            slot_type = slot.type;
            break;
        }
    }

    Result result = Result::Success();

    // Handle based on slot type
    switch (slot_type) {
        case SlotAllocation::SlotType::TX:
        case SlotAllocation::SlotType::DISCOVERY_TX:
        case SlotAllocation::SlotType::CONTROL_TX:
            result = hardware_->setState(radio::RadioState::kTransmit);
            if (!result) {
                LOG_ERROR("Failed to set radio to transmit state: %s",
                          result.GetErrorMessage().c_str());
            }
            break;

        case SlotAllocation::SlotType::RX:
            result = hardware_->setState(radio::RadioState::kReceive);
            if (!result) {
                LOG_ERROR("Failed to set radio to receive state: %s",
                          result.GetErrorMessage().c_str());
            }
            break;

        case SlotAllocation::SlotType::DISCOVERY_RX:
            result = SendDiscoveryMessage();
            if (!result) {
                LOG_ERROR("Failed to send discovery message: %s",
                          result.GetErrorMessage().c_str());
            }
            break;

        case SlotAllocation::SlotType::CONTROL_RX:
            SendControlMessage();
            break;

        case SlotAllocation::SlotType::SLEEP:
            // Set radio to sleep mode
            result = hardware_->setState(radio::RadioState::kSleep);
            if (!result) {
                LOG_ERROR("Failed to set radio to sleep state: %s",
                          result.GetErrorMessage().c_str());
            }
            break;
    }
}

uint32_t LoRaMeshProtocol::GetCurrentTime() const {
#ifdef DEBUG
    if (get_time_function_) {
        return get_time_function_();
    }
#endif
    // Original implementation
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void LoRaMeshProtocol::AddMessageToMessageQueue(
    MessageType type, std::unique_ptr<BaseMessage> message) {
    message_queue_[type].push_back(std::move(message));
}

std::unique_ptr<BaseMessage> LoRaMeshProtocol::ExtractMessageQueueOfType(
    MessageType type) {
    auto it = message_queue_.find(type);
    if (it != message_queue_.end() && !it->second.empty()) {
        // Get the first message
        std::unique_ptr<BaseMessage> message = std::move(it->second.front());

        // Remove it from the queue
        it->second.erase(it->second.begin());

        // Return the message (will be moved)
        return message;
    }

    // No message found
    return nullptr;
}

}  // namespace protocols
}  // namespace loramesher