/**
 * @file lora_mesh_protocol.cpp
 * @brief Implementation of the LoRaMesh protocol
 */

#include "protocols/lora_mesh_protocol.hpp"

#include <algorithm>
#include <chrono>

#include "lora_mesh_protocol.hpp"
#include "os/os_port.hpp"
#include "types/messages/loramesher/join_request_message.hpp"
#include "types/messages/loramesher/join_response_message.hpp"
#include "types/messages/loramesher/routing_table_message.hpp"
#include "types/messages/loramesher/slot_allocation_message.hpp"
#include "types/messages/loramesher/slot_request_message.hpp"
#include "types/radio/radio_event.hpp"

namespace {
using namespace loramesher::types::protocols::lora_mesh;
}

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
    if (!event) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Empty radio event");
    }

    // Extract basic event info
    const auto& message = event->getMessage();
    AddressType source = message->GetSource();

    // TODO: Check if the message is for us or broadcast
    // TODO: If we are next hop, we need to check if the message is for us or broadcast
    // TODO: If we are next hop and not destination, we need to forward the message
    // TODO: Add a conversion of message type to slot and add it to the slot to be send in the next available slot

    // Process based on message type
    switch (message->GetType()) {
        case MessageType::ROUTE_TABLE: {
            // Handle routing table message
            Result result = ProcessRoutingTableMessage(*message);
            if (!result) {
                LOG_ERROR("Failed to process routing table message: %s",
                          result.GetErrorMessage().c_str());
                return result;
            }

            return result;
        }
        case MessageType::DATA_MSG: {
            // Handle data message - route it if needed
            AddressType destination = message->GetDestination();

            // If message is for us, process it locally
            if (destination == node_address_ ||
                destination == BROADCAST_ADDRESS) {
                // TODO: Process the data message locally
                // ...
            } else {
                // Message needs to be forwarded
                AddressType next_hop = FindNextHop(destination);
                if (next_hop != 0) {
                    // Forward the message at the next appropriate TX slot
                    // Create a copy of the message and add to queue
                    auto data_message = std::make_unique<BaseMessage>(*message);
                    AddMessageToMessageQueue(SlotAllocation::SlotType::TX,
                                             std::move(data_message));
                }
            }
            break;
        }
        default:
            LOG_WARNING("Unknown message type: %d",
                        static_cast<int>(message->GetType()));
            break;
    }

    return Result::Success();
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

    protocol->HandleSlotTransition(protocol->current_slot_);

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
//     AddressType source, const std::vector<uint8_t>& routing_data) {
//     bool routing_changed = false;

//     utils::ByteDeserializer deserializer(routing_data);

//     // Read header information (network_id, version, entry_count)
//     auto network_id = deserializer.ReadUint16();
//     auto table_version = deserializer.ReadUint8();
//     auto entry_count = deserializer.ReadUint8();

//     if (!network_id || !table_version || !entry_count) {
//         LOG_ERROR("Failed to read routing table header");
//         return Result(LoraMesherErrorCode::kSerializationError,
//                       "Failed to parse routing table header");
//     }

//     LOG_INFO(
//         "Received routing table update from 0x%04X: version %d, %d entries",
//         source, *table_version, *entry_count);

//     // Process each entry
//     for (uint8_t i = 0; i < *entry_count; i++) {
//         auto entry = RoutingTableEntry::Deserialize(deserializer);
//         if (!entry) {
//             LOG_ERROR("Failed to deserialize routing entry %d", i);
//             continue;
//         }

//         // Skip entries for ourselves
//         if (entry->destination == node_address_) {
//             continue;
//         }

//         // Process the entry and track if routing changed
//         bool entry_changed =
//             UpdateRoutingEntry(source, entry->destination, entry->hop_count,
//                                entry->link_quality, entry->allocated_slots);

//         routing_changed |= entry_changed;
//     }

//     // Record that we've seen this node
//     UpdateNetworkNodeInfo(source);

//     // If routing changed significantly, reallocate slots
//     if (routing_changed) {
//         LOG_INFO("Routing table updated from node 0x%04X, reallocating slots",
//                  source);
//         InitializeSlotTable(network_manager_ == node_address_);
//     }

//     return Result::Success();
// }

Result LoRaMeshProtocol::LogicJoining() {
    LOG_INFO("Starting joining phase");

    // Set radio to receive mode to listen for network information
    Result result = hardware_->setState(radio::RadioState::kReceive);
    if (!result) {
        return result;
    }

    auto& rtos = GetRTOS();
    std::unique_ptr<radio::RadioEvent>* event_ptr = nullptr;

    // Wait for control messages from network manager
    uint32_t joining_timeout_ms = config_.getJoiningTimeout();
    uint32_t start_time = GetCurrentTime();
    uint32_t end_time = start_time + joining_timeout_ms;
    AddressType detected_network_manager = 0;
    AddressType network_mng_addr = 0;

    while (!stop_tasks_ && !rtos.ShouldStopOrPause() &&
           GetCurrentTime() < end_time && detected_network_manager == 0) {

        joining_timeout_ms = end_time - GetCurrentTime();
        if (joining_timeout_ms < 0) {
            joining_timeout_ms = 0;
        }

        // Check for messages from radio event handler
        if (rtos.ReceiveFromQueue(protocol_event_queue_, &event_ptr,
                                  joining_timeout_ms) == os::QueueResult::kOk) {

            // Process the received message
            const auto& message = (*event_ptr)->getMessage();

            if (message->GetType() == MessageType::ROUTE_TABLE) {
                // Extract network information from routing table message
                auto routing_message_opt =
                    RoutingTableMessage::CreateFromSerialized(
                        *message->Serialize());
                if (routing_message_opt) {
                    // We found a valid routing message - use it to join the network
                    detected_network_manager = routing_message_opt->GetSource();
                    network_mng_addr = routing_message_opt->GetNetworkManager();

                    LOG_INFO(
                        "Found network manager 0x%04X with network Manager "
                        "Address 0x%04X",
                        detected_network_manager, network_mng_addr);
                }
            }

            // Clean up event pointer
            delete event_ptr;
        }
    }

    if (stop_tasks_) {
        LOG_INFO("Joining phase stopped by user request");
        return Result::Success();
    }

    if (detected_network_manager == 0) {
        LOG_INFO("Joining timeout - no valid network manager detected");
        // Fall back to discovery mode
        state_ = ProtocolState::DISCOVERY;
        return Result::Success();
    }

    // Join the detected network
    Result join_result = JoinNetwork(detected_network_manager);
    if (!join_result) {
        LOG_ERROR("Failed to join network: %s",
                  join_result.GetErrorMessage().c_str());
        // Fall back to discovery mode
        state_ = ProtocolState::DISCOVERY;
        return join_result;
    }

    // Request slot allocation from the network manager
    Result slot_request_result = SendSlotRequest(config_.getDefaultDataSlots());
    if (!slot_request_result) {
        LOG_WARNING("Failed to send slot request: %s",
                    slot_request_result.GetErrorMessage().c_str());
        // Continue anyway - we'll use default allocation
    }

    // Successfully joined network
    LOG_INFO("Successfully joined network with manager 0x%04X",
             detected_network_manager);
    state_ = ProtocolState::NORMAL_OPERATION;

    return Result::Success();
}

Result LoRaMeshProtocol::SendRoutingTableUpdate() {
    // Create a routing table message
    auto message = CreateRoutingTableMessage();
    if (!message) {
        return Result(LoraMesherErrorCode::kMemoryError,
                      "Failed to create routing table message");
    }

    // Add the message to the control message queue
    AddMessageToMessageQueue(SlotAllocation::SlotType::CONTROL_TX,
                             std::move(message));

    LOG_DEBUG("Routing table update message queued for transmission");
    return Result::Success();
}

Result LoRaMeshProtocol::SendDiscoveryMessage() {
    // Get the first discovery message
    auto discovery_message =
        ExtractMessageQueueOfType(SlotAllocation::SlotType::DISCOVERY_TX);
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

Result LoRaMeshProtocol::SendDataMessage() {
    // Get the first discovery message
    auto data_message = ExtractMessageQueueOfType(SlotAllocation::SlotType::TX);
    if (!data_message) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Discovery message queue is empty, nothing to send");
    }

    // Send the discovery message
    Result result = SendMessage(*data_message);
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
    // First check if there's a routing table update to send
    // if (GetCurrentTime() - last_routing_update_time_ >=
    //     routing_update_interval_ms_) {
    // Time to send a routing update
    // TODO: Send periodically the routing table?
    Result result = SendRoutingTableUpdate();
    if (!result) {
        LOG_ERROR("Failed to send routing table update: %s",
                  result.GetErrorMessage().c_str());
    }
    //  else {
    //     last_routing_update_time_ = GetCurrentTime();
    // }
    // }

    // Then handle other control messages
    auto control_message =
        ExtractMessageQueueOfType(SlotAllocation::SlotType::CONTROL_TX);
    if (!control_message) {
        LOG_DEBUG("Control message queue is empty, nothing to send");
        return Result::Success();
    }

    // Send the control message
    result = SendMessage(*control_message);
    if (!result) {
        return result;
    }

    return Result::Success();
}

AddressType LoRaMeshProtocol::FindNextHop(AddressType destination) const {
    return 0;
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
        case SlotAllocation::SlotType::RX:
        case SlotAllocation::SlotType::DISCOVERY_RX:
        case SlotAllocation::SlotType::CONTROL_RX:
            result = hardware_->setState(radio::RadioState::kReceive);
            if (!result) {
                LOG_ERROR("Failed to set radio to transmit state: %s",
                          result.GetErrorMessage().c_str());
            }
            break;

        case SlotAllocation::SlotType::TX:
            result = SendDataMessage();
            if (!result) {
                LOG_ERROR("Failed to send data message: %s",
                          result.GetErrorMessage().c_str());
            }
            break;

        case SlotAllocation::SlotType::DISCOVERY_TX:
            result = SendDiscoveryMessage();
            if (!result) {
                LOG_ERROR("Failed to send discovery message: %s",
                          result.GetErrorMessage().c_str());
            }
            break;

        case SlotAllocation::SlotType::CONTROL_TX:
            result = SendControlMessage();
            if (!result) {
                LOG_ERROR("Failed to send control message: %s",
                          result.GetErrorMessage().c_str());
            }
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
    SlotAllocation::SlotType type, std::unique_ptr<BaseMessage> message) {
    message_queue_[type].push_back(std::move(message));
}

std::unique_ptr<BaseMessage> LoRaMeshProtocol::ExtractMessageQueueOfType(
    SlotAllocation::SlotType type) {
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

Result LoRaMeshProtocol::InitializeSlotTable(bool is_network_manager) {
    // Clear existing slot table
    slot_table_.clear();

    // Calculate slot counts based on superframe configuration
    uint16_t total_slots = current_superframe_.total_slots;
    uint16_t data_slots = current_superframe_.data_slots;
    uint16_t discovery_slots = current_superframe_.discovery_slots;
    uint16_t control_slots = current_superframe_.control_slots;

    // Validate slot counts
    if (data_slots + discovery_slots + control_slots > total_slots) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Sum of slot types exceeds total slots");
    }

    // Create sleep slots as default (fill entire table with sleep first)
    for (uint16_t i = 0; i < total_slots; i++) {
        SlotAllocation slot;
        slot.slot_number = i;
        slot.type = SlotAllocation::SlotType::SLEEP;
        slot_table_.push_back(slot);
    }

    // Distribute control slots evenly through the superframe
    // Network manager sends, others receive
    if (control_slots > 0) {
        uint16_t control_interval = total_slots / control_slots;
        for (uint16_t i = 0; i < control_slots; i++) {
            uint16_t slot_num = i * control_interval;
            slot_table_[slot_num].type =
                is_network_manager ? SlotAllocation::SlotType::CONTROL_TX
                                   : SlotAllocation::SlotType::CONTROL_RX;
        }
    }

    // Distribute discovery slots evenly through the superframe
    // All nodes need both TX and RX for discovery
    if (discovery_slots > 0) {
        uint16_t discovery_interval = total_slots / discovery_slots;
        for (uint16_t i = 0; i < discovery_slots; i++) {
            uint16_t slot_num =
                (i * discovery_interval) + (discovery_interval / 2);
            slot_num %= total_slots;  // Ensure we stay within bounds

            // Skip if this slot is already assigned
            if (slot_table_[slot_num].type != SlotAllocation::SlotType::SLEEP) {
                continue;
            }

            // Alternate between TX and RX for discovery
            slot_table_[slot_num].type =
                (i % 2 == 0) ? SlotAllocation::SlotType::DISCOVERY_TX
                             : SlotAllocation::SlotType::DISCOVERY_RX;
        }
    }

    // Allocate data slots based on routing table and network topology
    AllocateDataSlotsBasedOnRouting(is_network_manager, data_slots);

    // Log slot allocation for debugging
    LOG_DEBUG(
        "Slot table initialized with %d total slots (%d data, %d discovery, %d "
        "control)",
        total_slots, data_slots, discovery_slots, control_slots);

    return Result::Success();
}

void LoRaMeshProtocol::AllocateDataSlotsBasedOnRouting(
    bool is_network_manager, uint16_t available_data_slots) {}

uint16_t LoRaMeshProtocol::FindNextAvailableSlot(uint16_t start_slot) {
    uint16_t total_slots = current_superframe_.total_slots;

    // Search from start_slot to the end
    for (uint16_t i = start_slot; i < total_slots; i++) {
        if (slot_table_[i].type == SlotAllocation::SlotType::SLEEP) {
            return i;
        }
    }

    // If not found, search from beginning to start_slot
    for (uint16_t i = 0; i < start_slot; i++) {
        if (slot_table_[i].type == SlotAllocation::SlotType::SLEEP) {
            return i;
        }
    }

    // No available slots
    return UINT16_MAX;
}

bool LoRaMeshProtocol::UpdateRoutingEntry(AddressType source,
                                          AddressType destination,
                                          uint8_t hop_count,
                                          uint8_t link_quality,
                                          uint8_t allocated_slots) {
    // return UpdateRoutingEntry(source, destination, hop_count, link_quality,
    //                           allocated_slots,
    //                           false);  // Don't update last seen time here
    return false;
}

std::unique_ptr<BaseMessage> LoRaMeshProtocol::CreateRoutingTableMessage() {
    // // Collect active routes
    // std::vector<RoutingTableEntry> entries_to_share;

    // for (const auto& entry : routing_table_) {
    //     if (entry.is_active) {
    //         // Create an entry with the information to share
    //         RoutingTableEntry shared_entry(entry.destination, entry.hop_count,
    //                                        entry.link_quality,
    //                                        entry.allocated_slots);

    //         entries_to_share.push_back(shared_entry);
    //     }
    // }

    // // Generate a new routing table version
    // static uint8_t table_version = 0;
    // table_version++;  // Increment for each new message

    // if (table_version > 255) {
    //     table_version = 0;  // Wrap around if needed
    // }

    // auto routing_table_msg = RoutingTableMessage::Create(
    //     network_manager_, node_address_, network_manager_, table_version,
    //     entries_to_share);
    // if (!routing_table_msg) {
    //     LOG_ERROR("Failed to create routing table message");
    //     return nullptr;
    // }

    // // Return as unique pointer
    // return std::make_unique<BaseMessage>(routing_table_msg->ToBaseMessage());
    return nullptr;  // Placeholder for now
}

uint8_t LoRaMeshProtocol::CalculateLinkQuality(AddressType neighbor_address) {
    // TODO: This is a placeholder - in a real implementation you'd use signal strength,
    // packet loss rate, etc. to determine link quality

    // For now, return a default value
    return 80;  // 80% quality
}

Result LoRaMeshProtocol::SendSlotRequest(uint8_t num_slots) {
    // Create a simple slot request message (just contains number of slots requested)
    std::vector<uint8_t> payload = {num_slots};

    auto message =
        BaseMessage::Create(network_manager_,  // Send to network manager
                            node_address_,     // From this node
                            MessageType::SLOT_REQUEST, payload);

    if (!message) {
        return Result(LoraMesherErrorCode::kMemoryError,
                      "Failed to create slot request message");
    }

    // Add to the message queue for transmission in the next appropriate slot
    AddMessageToMessageQueue(SlotAllocation::SlotType::CONTROL_TX,
                             std::make_unique<BaseMessage>(*message));

    LOG_INFO(
        "Slot request message queued for transmission, requesting %d slots",
        num_slots);
    return Result::Success();
}

Result LoRaMeshProtocol::JoinNetwork(AddressType network_manager_address) {
    // Set up as a regular node
    state_ = ProtocolState::NORMAL_OPERATION;
    network_manager_ = network_manager_address;
    is_synchronized_ = true;  // Assume synchronized for now
    last_sync_time_ = GetCurrentTime();
    current_superframe_.superframe_start_time = last_sync_time_;

    // Initialize the slot table for a regular node
    Result init_result = InitializeSlotTable(false);
    if (!init_result) {
        return init_result;
    }

    // Notify the slot manager task to start the superframe
    auto result = GetRTOS().NotifyTask(slot_manager_task_handle_, 0);
    if (result != os::QueueResult::kOk) {
        return Result(LoraMesherErrorCode::kInterruptError,
                      "Failed to notify slot manager task");
    }

    LOG_INFO("Joined network, manager address: 0x%04X",
             network_manager_address);
    return Result::Success();
}

bool LoRaMeshProtocol::UpdateNetworkNodeInfo(AddressType node_address,
                                             uint8_t battery_level) {
    // bool found = false;
    // bool new_node = false;

    // // Don't track ourselves as a separate node
    // if (node_address == node_address_) {
    //     return false;
    // }

    // for (auto& node : network_nodes_) {
    //     if (node.address == node_address) {
    //         node.last_seen = GetCurrentTime();

    //         // Only update battery level if a new value was provided
    //         if (battery_level > 0) {
    //             node.battery_level = battery_level;
    //         }

    //         found = true;
    //         break;
    //     }
    // }

    // if (!found) {
    //     NetworkNode new_node_entry;
    //     new_node_entry.address = node_address;
    //     new_node_entry.battery_level = (battery_level > 0)
    //                                        ? battery_level
    //                                        : 100;  // Default to full battery
    //     new_node_entry.last_seen = GetCurrentTime();
    //     new_node_entry.is_network_manager = (node_address == network_manager_);

    //     network_nodes_.push_back(new_node_entry);
    //     new_node = true;

    //     LOG_INFO("New node 0x%04X discovered in the network", node_address);
    // }

    // return new_node;

    return false;
}

/**
 * @brief Sends a join request to the network
 * 
 * @param manager_address Address of the network manager to join
 * @param requested_slots Number of data slots requested
 * @return Result Success if request was sent, error otherwise
 */
Result LoRaMeshProtocol::SendJoinRequest(AddressType manager_address,
                                         uint8_t requested_slots) {
    // Determine the appropriate capabilities for this node
    uint8_t capabilities = 0;

    // Add router capability if we have routes to share
    if (!routing_table_.empty()) {
        capabilities |= JoinRequestHeader::ROUTER;
    }

    // TODO: Set other capabilities based on node configuration
    // if (config_.isGateway()) {
    //     capabilities |= JoinRequestHeader::GATEWAY;
    // }

    // TODO: Get actual battery level from hardware if available
    uint8_t battery_level = 100;

    // Create the join request message
    auto join_request = JoinRequestMessage::Create(
        manager_address,  // Destination is the manager
        node_address_,    // From this node
        capabilities,     // Node capabilities
        battery_level,    // Battery level
        requested_slots   // Requested number of data slots
    );

    if (!join_request) {
        return Result(LoraMesherErrorCode::kMemoryError,
                      "Failed to create join request message");
    }

    // Convert to base message for transmission
    BaseMessage base_message = join_request->ToBaseMessage();

    // Send the message
    Result result = SendMessage(base_message);
    if (!result) {
        return result;
    }

    LOG_INFO("Join request sent to manager 0x%04X, requesting %d slots",
             manager_address, requested_slots);

    return Result::Success();
}

Result LoRaMeshProtocol::ProcessJoinRequest(const BaseMessage& message) {
    // Only the network manager should process join requests
    if (network_manager_ != node_address_) {
        LOG_WARNING("Ignoring join request as we are not the network manager");
        return Result::Success();  // Not an error, just not our job
    }

    auto opt_serialized = message.Serialize();
    if (!opt_serialized) {
        LOG_ERROR("Failed to serialize join request message");
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to serialize join request message");
    }

    auto procces_join_req_opt =
        JoinRequestMessage::CreateFromSerialized(*opt_serialized);

    if (!procces_join_req_opt) {
        LOG_ERROR("Failed to deserialize join request message");
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to deserialize join request message");
    }

    AddressType node_address = message.GetSource();

    auto capabilities = procces_join_req_opt->GetCapabilities();
    auto battery_level = procces_join_req_opt->GetBatteryLevel();
    auto requested_slots = procces_join_req_opt->GetRequestedSlots();

    LOG_INFO(
        "Received join request from 0x%04X: capabilities=0x%02X, battery=%d%%, "
        "slots=%d",
        node_address, capabilities, battery_level, requested_slots);

    // Determine if the node should be allowed to join
    auto [accepted, allocated_slots] =
        ShouldAcceptJoin(node_address, capabilities, requested_slots);

    // Update network node information
    UpdateNetworkNodeInfo(node_address, battery_level);

    // Send join response
    JoinResponseHeader::ResponseStatus status =
        accepted ? JoinResponseHeader::ACCEPTED : JoinResponseHeader::REJECTED;

    Result response_result =
        SendJoinResponse(node_address, status, allocated_slots);
    if (!response_result) {
        LOG_ERROR("Failed to send join response: %s",
                  response_result.GetErrorMessage().c_str());
        return response_result;
    }

    // If accepted, update routing and slot tables
    if (accepted) {
        LOG_INFO("Node 0x%04X accepted into network with %d slots",
                 node_address, allocated_slots);

        // Update slot allocation for the entire network
        UpdateSlotAllocation();

        // Notify all nodes about the updated slot allocation
        BroadcastSlotAllocation();
    } else {
        LOG_INFO("Node 0x%04X rejected from network", node_address);
    }

    return Result::Success();
}

Result LoRaMeshProtocol::ProcessJoinResponse(const BaseMessage& message) {
    // Only nodes attempting to join should process join responses
    if (state_ != ProtocolState::JOINING) {
        LOG_WARNING("Ignoring join response as we are not in joining state");
        return Result::Success();  // Not an error, just unexpected
    }

    auto opt_serialized = message.Serialize();
    if (!opt_serialized) {
        LOG_ERROR("Failed to serialize join response message");
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to serialize join response message");
    }

    auto join_response_opt =
        JoinResponseMessage::CreateFromSerialized(*opt_serialized);
    if (!join_response_opt) {
        LOG_ERROR("Failed to deserialize join response message");
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to deserialize join response message");
    }

    // Convert raw status to enum
    auto status = join_response_opt->GetStatus();
    auto network_id = join_response_opt->GetNetworkId();
    auto allocated_slots = join_response_opt->GetAllocatedSlots();

    AddressType manager_address = message.GetSource();

    LOG_INFO(
        "Received join response from manager 0x%04X: status=%d, "
        "network=0x%04X, slots=%d",
        manager_address, status, network_id, allocated_slots);

    // Process based on join status
    if (status == JoinResponseHeader::ACCEPTED) {
        LOG_INFO("Join request accepted! Joining network 0x%04X with %d slots",
                 network_id, allocated_slots);

        // Update our network state
        network_manager_ = manager_address;
        is_synchronized_ = true;
        last_sync_time_ = GetCurrentTime();

        // Transition to normal operation
        state_ = ProtocolState::NORMAL_OPERATION;

        // Initialize slot table based on our position in the network
        Result init_result = InitializeSlotTable(false);
        if (!init_result) {
            LOG_ERROR("Failed to initialize slot table: %s",
                      init_result.GetErrorMessage().c_str());
        }

        // Start the slot manager
        auto result = GetRTOS().NotifyTask(slot_manager_task_handle_, 0);
        if (result != os::QueueResult::kOk) {
            LOG_ERROR("Failed to notify slot manager task");
        }
    } else {
        LOG_WARNING("Join request rejected with status %d", status);

        // Handle specific rejection reasons
        switch (status) {
            case JoinResponseHeader::CAPACITY_EXCEEDED:
                LOG_WARNING("Network at capacity, cannot join");
                break;

            case JoinResponseHeader::AUTHENTICATION_FAILED:
                LOG_WARNING("Authentication failed, cannot join");
                break;

            case JoinResponseHeader::RETRY_LATER:
                LOG_INFO("Network busy, retry joining later");
                // TODO: Implement retry mechanism with backoff
                break;

            default:
                LOG_WARNING("Join request rejected for unknown reason");
                break;
        }

        // Return to discovery state
        state_ = ProtocolState::DISCOVERY;
    }

    return Result::Success();
}

std::pair<bool, uint8_t> LoRaMeshProtocol::ShouldAcceptJoin(
    AddressType node_address, uint8_t capabilities, uint8_t requested_slots) {
    // TODO: Implement application-defined callback for join acceptance
    // if (join_acceptance_callback_) {
    //     return join_acceptance_callback_(node_address, capabilities, requested_slots);
    // }

    // Default implementation: accept all nodes

    // Check network capacity
    // if (network_nodes_.size() >= config_.getMaxNetworkNodes()) {
    //     LOG_WARNING("Network at capacity (%zu nodes), rejecting join request",
    //                 network_nodes_.size());
    //     return {false, 0};
    // }

    // // Check if slots are available
    // uint8_t available_slots =
    //     MAX_DATA_SLOTS - GetAlocatedDataSlotsFromRoutingTable();

    // uint8_t allocated_slots = std::min(requested_slots, available_slots);

    // if (allocated_slots == 0) {
    //     LOG_WARNING(
    //         "No slots available for allocation, rejecting join request");
    //     return {false, 0};
    // }

    // if (allocated_slots < requested_slots) {
    //     LOG_WARNING("Not enough slots available (requested %d, allocated %d)",
    //                 requested_slots, allocated_slots);
    // }

    // // Accept with requested slots
    // return {true, allocated_slots};

    return {false, 0};
}

Result LoRaMeshProtocol::SendJoinResponse(
    AddressType dest, JoinResponseHeader::ResponseStatus status,
    uint8_t allocated_slots) {
    // Only the network manager should send join responses
    if (network_manager_ != node_address_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Only network manager can send join responses");
    }

    // Create superframe information to share with the node
    // std::vector<uint8_t> superframe_info(8);
    // utils::ByteSerializer serializer(superframe_info);

    // // Serialize superframe configuration
    // serializer.WriteUint16(current_superframe_.total_slots);
    // serializer.WriteUint16(current_superframe_.data_slots);
    // serializer.WriteUint16(current_superframe_.discovery_slots);
    // serializer.WriteUint16(current_superframe_.control_slots);

    // Create the join response message
    auto join_response = JoinResponseMessage::Create(
        dest,              // Destination is the joining node
        node_address_,     // From this node (network manager)
        network_manager_,  // Network ID
        allocated_slots,   // Allocated data slots
        status             // Response status
        // ,
        // superframe_info    // Superframe configuration
    );

    if (!join_response) {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "Failed to create join response message");
    }

    // Convert to base message for transmission
    BaseMessage base_message = join_response->ToBaseMessage();

    // Send the message
    Result result = SendMessage(base_message);
    if (!result) {
        return result;
    }

    LOG_INFO("Join response sent to node 0x%04X: status=%d, slots=%d", dest,
             static_cast<int>(status), allocated_slots);

    return Result::Success();
}

Result LoRaMeshProtocol::ProcessSlotRequest(const BaseMessage& message) {
    // Only the network manager should process slot requests
    // if (network_manager_ != node_address_) {
    //     LOG_WARNING("Ignoring slot request as we are not the network manager");
    //     return Result::Success();  // Not an error, just not our job
    // }

    // auto opt_serialized = message.Serialize();
    // if (!opt_serialized) {
    //     LOG_ERROR("Failed to serialize slot request message");
    //     return Result(LoraMesherErrorCode::kSerializationError,
    //                   "Failed to serialize slot request message");
    // }

    // auto slot_request =
    //     SlotRequestMessage::CreateFromSerialized(*opt_serialized);
    // if (!slot_request) {
    //     LOG_ERROR("Failed to deserialize slot request message");
    //     return Result(LoraMesherErrorCode::kSerializationError,
    //                   "Failed to deserialize slot request message");
    // }

    // auto requested_slots = slot_request->GetRequestedSlots();

    // AddressType node_address = message.GetSource();

    // LOG_INFO("Received slot request from 0x%04X: requested %d slots",
    //          node_address, requested_slots);

    // // Check if node is part of the network
    // bool is_known_node = false;
    // for (const auto& node : network_nodes_) {
    //     if (node.address == node_address) {
    //         is_known_node = true;
    //         break;
    //     }
    // }

    // if (!is_known_node) {
    //     LOG_WARNING("Slot request from unknown node 0x%04X, ignoring",
    //                 node_address);
    //     return Result::Success();
    // }

    // // Determine how many slots to allocate
    // uint8_t available_slots =
    //     MAX_DATA_SLOTS - GetAlocatedDataSlotsFromRoutingTable();
    // uint8_t allocated_slots = std::min(requested_slots, available_slots);

    // if (allocated_slots == 0) {
    //     LOG_WARNING(
    //         "No slots available for allocation, rejecting slot request");
    //     return Result(LoraMesherErrorCode::kInvalidState,
    //                   "No slots available for allocation");
    // }

    // if (allocated_slots < requested_slots) {
    //     LOG_WARNING("Not enough slots available (requested %d, allocated %d)",
    //                 requested_slots, allocated_slots);
    // }

    // // Update slot allocation for the node
    // // TODO: Update node-specific slot allocation tracking
    // // TODO: Send slot allocation message to the node

    // // Update slot allocation for the entire network
    // UpdateSlotAllocation();

    // // Notify all nodes about the updated slot allocation
    // BroadcastSlotAllocation();

    // LOG_INFO("Allocated %d slots to node 0x%04X", allocated_slots,
    //          node_address);

    // return Result::Success();

    return Result(LoraMesherErrorCode::kNotImplemented,
                  "Slot request processing not implemented yet");
}

Result LoRaMeshProtocol::UpdateSlotAllocation() {
    // Only the network manager should update slot allocation
    if (network_manager_ != node_address_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Only network manager can update slot allocation");
    }

    // Reinitialize slot table for the entire network
    Result result = InitializeSlotTable(true);
    if (!result) {
        return result;
    }

    // TODO: Update any internal state tracking slot allocation

    LOG_INFO("Slot allocation updated for the network");

    return Result::Success();
}

Result LoRaMeshProtocol::BroadcastSlotAllocation() {
    // Only the network manager should broadcast slot allocation
    // if (network_manager_ != node_address_) {
    //     return Result(LoraMesherErrorCode::kInvalidState,
    //                   "Only network manager can broadcast slot allocation");
    // }

    // // Get total number of active nodes in the network
    // uint8_t total_nodes =
    //     static_cast<uint8_t>(network_nodes_.size() + 1);  // +1 for manager

    // // For each node in the network, send a slot allocation message
    // for (const auto& node : network_nodes_) {
    //     // TODO: Get actual allocated slots for this node
    //     uint8_t allocated_slots = 1;  // Default to 1 slot per node

    //     // Create slot allocation message
    //     auto slot_allocation = SlotAllocationMessage::Create(
    //         node.address,      // Destination is the node
    //         node_address_,     // From this node (network manager)
    //         network_manager_,  // Network ID
    //         allocated_slots,   // Allocated data slots
    //         total_nodes        // Total nodes in network
    //     );

    //     if (!slot_allocation) {
    //         LOG_ERROR(
    //             "Failed to create slot allocation message for node 0x%04X",
    //             node.address);
    //         continue;
    //     }

    //     // Convert to base message for transmission
    //     BaseMessage base_message = slot_allocation->ToBaseMessage();

    //     // Send the message
    //     Result result = SendMessage(base_message);
    //     if (!result) {
    //         LOG_ERROR("Failed to send slot allocation to node 0x%04X: %s",
    //                   node.address, result.GetErrorMessage().c_str());
    //     } else {
    //         LOG_INFO("Slot allocation sent to node 0x%04X: %d slots",
    //                  node.address, allocated_slots);
    //     }
    // }

    // return Result::Success();
    return Result(LoraMesherErrorCode::kNotImplemented,
                  "Slot allocation broadcasting not implemented yet");
}

uint8_t LoRaMeshProtocol::GetAlocatedDataSlotsFromRoutingTable() {
    // Count the number of active routes in the routing table
    uint8_t allocated_slots = 0;
    for (const auto& entry : routing_table_) {
        if (entry.is_active) {
            allocated_slots++;
        }
    }

    return allocated_slots;
}

Result LoRaMeshProtocol::ProcessSlotAllocation(const BaseMessage& message) {
    // Only accept slot allocation from the network manager
    if (message.GetSource() != network_manager_) {
        LOG_WARNING("Ignoring slot allocation from non-manager node 0x%04X",
                    message.GetSource());
        return Result::Success();  // Not an error, just unexpected
    }

    auto opt_serialized = message.Serialize();
    if (!opt_serialized) {
        LOG_ERROR("Failed to serialize slot allocation message");
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to serialize slot allocation message");
    }

    auto slot_allocation_msg =
        SlotAllocationMessage::CreateFromSerialized(*opt_serialized);
    if (!slot_allocation_msg) {
        LOG_ERROR("Failed to deserialize slot allocation message");
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to deserialize slot allocation message");
    }

    auto network_id = slot_allocation_msg->GetNetworkId();
    auto allocated_slots = slot_allocation_msg->GetAllocatedSlots();
    auto total_nodes = slot_allocation_msg->GetTotalNodes();
    auto source = slot_allocation_msg->GetSource();

    LOG_INFO(
        "Received slot allocation: network=0x%04X, slots=%d, total_nodes=%d",
        network_id, allocated_slots, total_nodes);

    // Update our slot allocation
    // TODO: Update any internal state tracking slot allocation

    // Reinitialize slot table based on our position in the network
    Result init_result = InitializeSlotTable(false);
    if (!init_result) {
        LOG_ERROR("Failed to initialize slot table: %s",
                  init_result.GetErrorMessage().c_str());
        return init_result;
    }

    LOG_INFO("Slot table updated with %d allocated slots", allocated_slots);

    return Result::Success();
}

Result LoRaMeshProtocol::ProcessRoutingTableMessage(
    const BaseMessage& message) {
    auto opt_serialized = message.Serialize();
    if (!opt_serialized) {
        LOG_ERROR("Failed to serialize routing message");
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to serialize routing message");
    }

    auto routing_msg =
        RoutingTableMessage::CreateFromSerialized(*opt_serialized);
    if (!routing_msg) {
        LOG_ERROR("Failed to deserialize routing message");
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Failed to deserialize routing message");
    }

    auto source = message.GetSource();
    auto table_version = routing_msg->GetTableVersion();
    auto entries = routing_msg->GetEntries();

    LOG_INFO(
        "Received routing table update from 0x%04X: version %d, %d entries",
        source, table_version, entries.size());

    // Process routing entries
    bool routing_changed = false;

    // for (uint8_t i = 0; i < entries.size(); i++) {

    //     const auto& entry = entries[i];
    //     auto dest = entry.address;
    //     auto hop_count = entry.hop_count;
    //     auto link_quality = entry.link_quality;
    //     auto allocated_slots = entry.allocated_slots;

    //     // Skip entries for ourselves
    //     if (dest == node_address_) {
    //         continue;
    //     }

    //     // Update routing entry and track changes
    //     bool entry_changed = UpdateRoutingEntry(source, dest, hop_count,
    //                                             link_quality, allocated_slots);

    //     routing_changed |= entry_changed;
    // }

    // // If routing changed significantly, reallocate slots
    // if (routing_changed) {
    //     LOG_INFO("Routing table updated from node 0x%04X, reallocating slots",
    //              source);
    //     InitializeSlotTable(network_manager_ == node_address_);
    // }

    return Result(LoraMesherErrorCode::kNotImplemented,
                  "Routing table processing not implemented yet");
}

}  // namespace protocols
}  // namespace loramesher