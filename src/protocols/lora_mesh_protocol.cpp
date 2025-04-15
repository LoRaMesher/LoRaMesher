/**
 * @file lora_mesh_protocol.cpp
 * @brief Implementation of the LoRaMesh protocol
 */

#include "protocols/lora_mesh_protocol.hpp"

#include <algorithm>
#include <chrono>

#include "os/os_port.hpp"
#include "types/radio/radio_event.hpp"

namespace loramesher {
namespace protocols {

LoRaMeshProtocol::LoRaMeshProtocol()
    : Protocol(ProtocolType::kLoraMesh),
      radio_task_handle_(nullptr),
      slot_manager_task_handle_(nullptr),
      protocol_task_handle_(nullptr),
      radio_event_queue_(nullptr),
      protocol_event_queue_(nullptr),
      state_(ProtocolState::INITIALIZING),
      stop_tasks_(false),
      current_slot_(0),
      is_synchronized_(false),
      network_manager_(0),
      last_sync_time_(0) {
    // Initialize superframe with default values
    current_superframe_.total_slots = DEFAULT_SLOTS_PER_SUPERFRAME;
    current_superframe_.data_slots = DEFAULT_SLOTS_PER_SUPERFRAME * 7 / 10;
    current_superframe_.discovery_slots = DEFAULT_SLOTS_PER_SUPERFRAME * 1 / 10;
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

    // Initialize the queues
    radio_event_queue_ = GetRTOS().CreateQueue(
        RADIO_QUEUE_SIZE, sizeof(std::unique_ptr<radio::RadioEvent>*));
    if (!radio_event_queue_) {
        return Result(
            LoraMesherErrorCode::kConfigurationError,
            "Failed to create radio event queue for LoRaMesh protocol");
    }

    protocol_event_queue_ =
        GetRTOS().CreateQueue(PROTOCOL_QUEUE_SIZE, sizeof(void*));
    if (!protocol_event_queue_) {
        // Clean up existing queue
        GetRTOS().DeleteQueue(radio_event_queue_);
        radio_event_queue_ = nullptr;

        return Result(
            LoraMesherErrorCode::kConfigurationError,
            "Failed to create protocol event queue for LoRaMesh protocol");
    }

    // Create the tasks
    bool radio_task_created = GetRTOS().CreateTask(
        RadioEventTaskFunction, "LoRaMeshRadio", RADIO_TASK_STACK_SIZE, this,
        TASK_PRIORITY, &radio_task_handle_);

    if (!radio_task_created) {
        // Clean up queues
        GetRTOS().DeleteQueue(radio_event_queue_);
        GetRTOS().DeleteQueue(protocol_event_queue_);
        radio_event_queue_ = nullptr;
        protocol_event_queue_ = nullptr;

        return Result(
            LoraMesherErrorCode::kConfigurationError,
            "Failed to create radio event task for LoRaMesh protocol");
    }

    bool slot_task_created = GetRTOS().CreateTask(
        SlotManagerTaskFunction, "LoRaMeshSlot", SLOT_MANAGER_TASK_STACK_SIZE,
        this, TASK_PRIORITY, &slot_manager_task_handle_);

    if (!slot_task_created) {
        // Clean up existing task and queues
        GetRTOS().DeleteTask(radio_task_handle_);
        GetRTOS().DeleteQueue(radio_event_queue_);
        GetRTOS().DeleteQueue(protocol_event_queue_);
        radio_task_handle_ = nullptr;
        radio_event_queue_ = nullptr;
        protocol_event_queue_ = nullptr;

        return Result(
            LoraMesherErrorCode::kConfigurationError,
            "Failed to create slot manager task for LoRaMesh protocol");
    }

    bool protocol_task_created = GetRTOS().CreateTask(
        ProtocolLogicTaskFunction, "LoRaMeshProtocol", PROTOCOL_TASK_STACK_SIZE,
        this, TASK_PRIORITY, &protocol_task_handle_);

    if (!protocol_task_created) {
        // Clean up existing tasks and queues
        GetRTOS().DeleteTask(radio_task_handle_);
        GetRTOS().DeleteTask(slot_manager_task_handle_);
        GetRTOS().DeleteQueue(radio_event_queue_);
        GetRTOS().DeleteQueue(protocol_event_queue_);
        radio_task_handle_ = nullptr;
        slot_manager_task_handle_ = nullptr;
        radio_event_queue_ = nullptr;
        protocol_event_queue_ = nullptr;

        return Result(
            LoraMesherErrorCode::kConfigurationError,
            "Failed to create protocol logic task for LoRaMesh protocol");
    }

    // Set up hardware event callback
    Result hw_result = hardware_->setActionReceive(
        [this](std::unique_ptr<radio::RadioEvent> event) {
            // Create a copy of the pointer that can be passed to the queue
            auto event_ptr =
                new std::unique_ptr<radio::RadioEvent>(std::move(event));

            // Send to queue with short timeout (shouldn't block in interrupt)
            auto queue_result =
                GetRTOS().SendToQueue(radio_event_queue_, &event_ptr, 0);

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

    // Update state to discovery
    state_ = ProtocolState::DISCOVERY;
    current_superframe_.superframe_start_time = GetCurrentTime();

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

    if (protocol_task_handle_ != nullptr) {
        GetRTOS().DeleteTask(protocol_task_handle_);
        protocol_task_handle_ = nullptr;
    }

    // Clean up queues
    if (radio_event_queue_ != nullptr) {
        GetRTOS().DeleteQueue(radio_event_queue_);
        radio_event_queue_ = nullptr;
    }

    if (protocol_event_queue_ != nullptr) {
        GetRTOS().DeleteQueue(protocol_event_queue_);
        protocol_event_queue_ = nullptr;
    }

    return Result::Success();
}

void LoRaMeshProtocol::RadioEventTaskFunction(void* parameters) {
    // Get protocol instance
    LoRaMeshProtocol* protocol = static_cast<LoRaMeshProtocol*>(parameters);
    if (!protocol) {
        return;
    }

    // RTOS instance
    auto& rtos = GetRTOS();

    // Pointer to receive from queue
    std::unique_ptr<radio::RadioEvent>* event_ptr = nullptr;

    // Task loop
    while (!protocol->stop_tasks_ && !rtos.ShouldStopOrPause()) {
        // Wait for radio event from the queue
        if (rtos.ReceiveFromQueue(protocol->radio_event_queue_, &event_ptr,
                                  QUEUE_WAIT_TIMEOUT_MS) ==
            os::QueueResult::kOk) {
            // Process the event
            Result result =
                protocol->ProcessReceivedRadioEvent(std::move(*event_ptr));

            if (!result) {
                // Log error but continue
            }

            // Clean up pointer wrapper
            delete event_ptr;
        }

        // Give other tasks a chance to run
        rtos.YieldTask();
    }

    // Task cleanup
    rtos.DeleteTask(nullptr);  // Delete self
}

void LoRaMeshProtocol::SlotManagerTaskFunction(void* parameters) {
    // Get protocol instance
    LoRaMeshProtocol* protocol = static_cast<LoRaMeshProtocol*>(parameters);
    if (!protocol) {
        return;
    }

    // RTOS instance
    auto& rtos = GetRTOS();

    // Initialize slot tracking
    uint16_t current_slot = 0;
    uint32_t last_slot_time = protocol->GetCurrentTime();
    uint32_t slot_duration = protocol->current_superframe_.slot_duration_ms;

    // Task loop
    while (!protocol->stop_tasks_ && !rtos.ShouldStopOrPause()) {
        // Get current time
        uint32_t current_time = protocol->GetCurrentTime();

        // Check if we should move to next slot
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

        // Short delay to prevent busy-waiting
        rtos.delay(slot_duration / 10);
    }

    // Task cleanup
    rtos.DeleteTask(nullptr);  // Delete self
}

void LoRaMeshProtocol::ProtocolLogicTaskFunction(void* parameters) {
    // Get protocol instance
    LoRaMeshProtocol* protocol = static_cast<LoRaMeshProtocol*>(parameters);
    if (!protocol) {
        return;
    }

    // RTOS instance
    auto& rtos = GetRTOS();

    // Task loop
    while (!protocol->stop_tasks_ && !rtos.ShouldStopOrPause()) {
        // Handle protocol state machine
        switch (protocol->state_) {
            case ProtocolState::DISCOVERY:
                // TODO: Implement discovery logic
                break;

            case ProtocolState::JOINING:
                // TODO: Implement joining logic
                break;

            case ProtocolState::NORMAL_OPERATION:
                // TODO: Implement normal operation logic
                break;

            case ProtocolState::NETWORK_MANAGER:
                // TODO: Implement network manager logic
                break;

            case ProtocolState::FAULT_RECOVERY:
                // TODO: Implement fault recovery logic
                break;

            default:
                // Should not reach here
                break;
        }

        // Sleep to give other tasks time to run
        rtos.delay(protocol->current_superframe_.slot_duration_ms);
    }

    // Task cleanup
    rtos.DeleteTask(nullptr);  // Delete self
}

Result LoRaMeshProtocol::ProcessReceivedRadioEvent(
    std::unique_ptr<radio::RadioEvent> event) {

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

    // TODO: Implement proper message type checking and dispatch
    // For now, just handle it as a generic message

    // Extract source address and payload
    AddressType source = message->GetHeader().GetSource();
    auto payload = message->GetPayload();

    // Basic message routing based on current state
    switch (state_) {
        case ProtocolState::DISCOVERY:
            // In discovery mode, look for network announcements
            return ProcessDiscovery(source, payload);

        case ProtocolState::NORMAL_OPERATION:
        case ProtocolState::NETWORK_MANAGER:
            // In normal operation, process based on first byte of payload (message type)
            if (payload.empty()) {
                return Result(LoraMesherErrorCode::kInvalidParameter,
                              "Empty payload in LoRaMesh message");
            }

            // TODO: Replace with proper message type enum
            uint8_t message_type = payload[0];
            switch (message_type) {
                case 0x01:  // Routing update
                    return ProcessRoutingUpdate(source, payload);
                case 0x02:  // Slot allocation
                    return ProcessSlotAllocation(source, payload);
                case 0x03:  // Control message
                    return ProcessControlMessage(source, payload);
                default:
                    return Result(LoraMesherErrorCode::kInvalidParameter,
                                  "Unknown LoRaMesh message type");
            }

        default:
            // In other states, just acknowledge receipt but don't process
            return Result::Success();
    }
}

Result LoRaMeshProtocol::SendMessage(const BaseMessage& message) {
    if (!hardware_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Hardware not initialized in LoRaMesh protocol");
    }

    // Forward to hardware for transmission
    return hardware_->SendMessage(message);
}

void LoRaMeshProtocol::SetRouteUpdateCallback(RouteUpdateCallback callback) {
    route_update_callback_ = callback;
}

Result LoRaMeshProtocol::ProcessRoutingUpdate(
    AddressType source, const std::vector<uint8_t>& data) {
    // TODO: Implement proper routing table update logic
    // For now, just acknowledge receipt

    // Stub implementation - just record that we've seen this node
    bool found = false;
    for (auto& node : network_nodes_) {
        if (node.address == source) {
            node.last_seen = GetCurrentTime();
            found = true;
            break;
        }
    }

    if (!found) {
        NetworkNode new_node;
        new_node.address = source;
        new_node.battery_level = 100;  // Default to full battery
        new_node.last_seen = GetCurrentTime();
        new_node.is_network_manager = false;
        network_nodes_.push_back(new_node);
    }

    return Result::Success();
}

Result LoRaMeshProtocol::ProcessSlotAllocation(
    AddressType source, const std::vector<uint8_t>& data) {
    // TODO: Implement slot allocation processing
    // For now, just acknowledge receipt
    return Result::Success();
}

Result LoRaMeshProtocol::ProcessDiscovery(AddressType source,
                                          const std::vector<uint8_t>& data) {
    // TODO: Implement discovery message processing
    // For now, just acknowledge receipt
    return Result::Success();
}

Result LoRaMeshProtocol::ProcessControlMessage(
    AddressType source, const std::vector<uint8_t>& data) {
    // TODO: Implement control message processing
    // For now, just acknowledge receipt
    return Result::Success();
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
    // TODO: Implement discovery message sending
    // For now, return success without doing anything
    return Result::Success();
}

Result LoRaMeshProtocol::SendSyncMessage() {
    // TODO: Implement sync message sending
    // For now, return success without doing anything
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

    // Handle based on slot type
    switch (slot_type) {
        case SlotAllocation::SlotType::TX:
            // TODO: Implement transmission behavior
            // For now, just log that it's a TX slot
            break;

        case SlotAllocation::SlotType::RX:
            // TODO: Implement reception behavior
            // For now, just log that it's an RX slot
            break;

        case SlotAllocation::SlotType::DISCOVERY:
            // During discovery slots, all nodes must listen
            // TODO: Implement discovery behavior
            if (state_ == ProtocolState::DISCOVERY) {
                // If in discovery state, send discovery messages
                SendDiscoveryMessage();
            }
            break;

        case SlotAllocation::SlotType::CONTROL:
            // During control slots, all nodes must listen
            // TODO: Implement control slot behavior
            if (state_ == ProtocolState::NETWORK_MANAGER) {
                // If network manager, send control messages
                SendSyncMessage();
            }
            break;

        case SlotAllocation::SlotType::SLEEP:
            // TODO: Implement sleep behavior
            // For now, just log that it's a sleep slot
            break;
    }
}

uint32_t LoRaMeshProtocol::GetCurrentTime() const {
    // Use system time for timestamps
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

}  // namespace protocols
}  // namespace loramesher