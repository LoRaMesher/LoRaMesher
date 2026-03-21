/**
 * @file lora_mesh_protocol.cpp
 * @brief Refactored implementation of LoRaMesh protocol using services
 */

#include "protocols/lora_mesh_protocol.hpp"
#include "lora_mesh_protocol.hpp"
#include "os/os_port.hpp"
#include "protocols/lora_mesh/routing/distance_vector_routing_table.hpp"
#include "types/messages/message_type.hpp"
#include "types/radio/radio_event.hpp"

namespace {
using namespace loramesher::types::protocols::lora_mesh;
}  // namespace

namespace loramesher {
namespace protocols {

/// Minimum time remaining in a slot (ms) needed to attempt a TX.
/// Covers worst-case LoRa ToA (~150ms) so a late-arriving handler does not
/// attempt a transmission that would overflow into the next slot.
static constexpr uint32_t kLateArrivalMarginMs = 150;

LoRaMeshProtocol::LoRaMeshProtocol()
    : Protocol(ProtocolType::kLoraMesh),
      protocol_task_handle_(nullptr),
      radio_event_queue_(nullptr),
      protocol_notification_queue_(nullptr),
      slot_transition_queue_(nullptr) {}

LoRaMeshProtocol::~LoRaMeshProtocol() {
    LOG_DEBUG("LoRaMeshProtocol destructor called");
    // Clean up protocol task if it exists
    if (protocol_task_handle_) {
        LOG_DEBUG("LoRaMeshProtocol destructor deleting task handle: %p",
                  protocol_task_handle_);
        GetRTOS().DeleteTask(protocol_task_handle_);
        protocol_task_handle_ = nullptr;
        LOG_DEBUG("LoRaMeshProtocol task handle set to nullptr");
    } else {
        LOG_DEBUG("LoRaMeshProtocol task handle already null");
    }

    // Clear callbacks to prevent use-after-free
    if (hardware_) {
        hardware_->setActionReceive(nullptr);
    }
    if (superframe_service_) {
        superframe_service_->SetSuperframeCallback(nullptr);
    }

    // Clean up radio event queue
    if (radio_event_queue_) {
        DrainRadioEventQueue();
        GetRTOS().DeleteQueue(radio_event_queue_);
        radio_event_queue_ = nullptr;
    }

    // Clean up protocol notification queue
    if (protocol_notification_queue_) {
        GetRTOS().DeleteQueue(protocol_notification_queue_);
        protocol_notification_queue_ = nullptr;
    }

    // Clean up slot transition queue
    if (slot_transition_queue_) {
        GetRTOS().DeleteQueue(slot_transition_queue_);
        slot_transition_queue_ = nullptr;
    }

    // Clear hardware reference
    hardware_.reset();

    LOG_DEBUG("LoRaMeshProtocol destructor completed");

    // Clear services
    message_queue_service_.reset();
    superframe_service_.reset();
    network_service_.reset();
}

Result LoRaMeshProtocol::Init(
    std::shared_ptr<hardware::IHardwareManager> hardware,
    AddressType node_address) {

    hardware->SetLocalAddress(node_address);

    // Initialize hardware
    Result hw_result = hardware->Initialize();
    if (!hw_result) {
        LOG_ERROR("Hardware initialization failed: %s",
                  hw_result.GetErrorMessage().c_str());
        return hw_result;
    }

    // Store hardware reference and node address
    hardware_ = hardware;
    node_address_ = node_address;

    // Create message queue service
    message_queue_service_ = std::make_shared<lora_mesh::MessageQueueService>(
        service_config_.message_queue_size);

    // Create superframe service
    superframe_service_ = std::make_shared<lora_mesh::SuperframeService>();
    superframe_service_->SetNodeAddress(node_address);

    // Create distance vector routing table
    auto routing_table =
        std::make_unique<lora_mesh::DistanceVectorRoutingTable>(
            node_address, 50);  // max 50 nodes

    // Create network service
    network_service_ = std::make_shared<lora_mesh::NetworkService>(
        node_address, message_queue_service_, superframe_service_, hardware_,
        std::move(routing_table));

    // Initialize radio event queue
    radio_event_queue_ =
        GetRTOS().CreateQueue(RADIO_QUEUE_SIZE, sizeof(radio::RadioEvent*));
    if (!radio_event_queue_) {
        return Result(LoraMesherErrorCode::kConfigurationError,
                      "Failed to create radio event queue");
    }

    // Initialize protocol notification queue for event-driven operation
    protocol_notification_queue_ = GetRTOS().CreateQueue(
        PROTOCOL_NOTIFICATION_QUEUE_SIZE, sizeof(ProtocolNotificationType));
    if (!protocol_notification_queue_) {
        return Result(LoraMesherErrorCode::kConfigurationError,
                      "Failed to create protocol notification queue");
    }

    // Initialize slot transition data queue (decouples superframe task from protocol task)
    slot_transition_queue_ =
        GetRTOS().CreateQueue(4, sizeof(SlotTransitionData));
    if (!slot_transition_queue_) {
        return Result(LoraMesherErrorCode::kConfigurationError,
                      "Failed to create slot transition queue");
    }

    // Set up hardware radio callback to send events to NetworkService
    hw_result = hardware_->setActionReceive(
        [this](std::unique_ptr<radio::RadioEvent> event) {
            if (!event) {
                LOG_WARNING("Received null radio event");
                return;
            }

            // Send to radio event queue
            // Transfer ownership by releasing unique_ptr to raw pointer
            radio::RadioEvent* raw_event = event.release();

            if (!radio_event_queue_) {
                LOG_ERROR("Radio event queue not initialized");
                delete raw_event;  // Clean up if queue is not ready
                return;
            }

            if (GetRTOS().SendToQueue(radio_event_queue_, &raw_event, 10) !=
                os::QueueResult::kOk) {
                LOG_ERROR("Failed to send radio event to queue");
                // Reclaim ownership and clean up if sending failed
                delete raw_event;
                return;
            }

            // Notify protocol task that radio event is available
            NotifyProtocolTask(ProtocolNotificationType::RADIO_EVENT);
        });

    if (!hw_result) {
        return hw_result;
    }

    // Set up callbacks from services

    // Superframe callback: post slot data to the queue and notify the protocol
    // task. Non-blocking so the superframe timing task is never stalled by
    // protocol processing (e.g. subslot delay + radio TX ~900ms).
    superframe_service_->SetSuperframeCallback(
        [this](uint16_t slot, bool new_superframe) {
            SlotTransitionData data{slot, new_superframe,
                                    superframe_service_->GetTimeInSlot()};
            GetRTOS().SendToQueue(slot_transition_queue_, &data, 0);
            NotifyProtocolTask(ProtocolNotificationType::SLOT_TRANSITION);
        });

    // Network service route update callback
    network_service_->SetRouteUpdateCallback(
        [this](bool updated, AddressType dest, AddressType next_hop,
               uint8_t hops) {
            LOG_DEBUG("Route %s: dest=0x%04X via=0x%04X hops=%d",
                      updated ? "updated" : "removed", dest, next_hop, hops);
            OnNetworkTopologyChange(updated, dest, next_hop, hops);
        });

    // State-change callback: wake up the protocol task immediately
    network_service_->SetStateChangeCallback(
        [this](lora_mesh::INetworkService::ProtocolState new_state) {
            OnStateChange(new_state);
        });

    // Create main protocol task
    bool task_created = GetRTOS().CreateTask(
        ProtocolTaskFunction, "LoRaMeshMain", PROTOCOL_TASK_STACK_SIZE, this,
        TASK_PRIORITY, &protocol_task_handle_);

    if (!task_created) {
        return Result(LoraMesherErrorCode::kConfigurationError,
                      "Failed to create protocol task");
    }

    // Wait until receive task is suspended
    GetRTOS().SuspendTask(protocol_task_handle_);

    // Apply default configuration
    config_ = LoRaMeshProtocolConfig(node_address);
#ifdef DEBUG
    service_config_ = CreateServiceConfigForTest(config_);
#else
    service_config_ = CreateServiceConfig(config_);
#endif  // DEBUG

    LOG_INFO("LoRaMesh protocol initialized for node 0x%04X", node_address_);

    return Result::Success();
}

Result LoRaMeshProtocol::Configure(const LoRaMeshProtocolConfig& config) {
    // Validate configuration
    std::string validation_error = config.Validate();
    if (!validation_error.empty()) {
        return Result(LoraMesherErrorCode::kInvalidParameter, validation_error);
    }

    // Store configuration
    config_ = config;
#ifdef DEBUG
    service_config_ = CreateServiceConfigForTest(config);
#else
    service_config_ = CreateServiceConfig(config);
#endif  // DEBUG

    // Configure network service
    lora_mesh::INetworkService::NetworkConfig net_config =
        service_config_.network_config;
    net_config.node_address = config.getNodeAddress();
    net_config.node_role = config.getNodeRole();
    net_config.target_duty_cycle = config.getTargetDutyCycle();
    net_config.min_sleep_fraction = config.getMinSleepFraction();

    Result result = network_service_->Configure(net_config);
    if (!result) {
        LOG_ERROR("Failed to configure network service: %s",
                  result.GetErrorMessage().c_str());
        return result;
    }

    // Configure message queue size
    message_queue_service_->SetMaxQueueSize(service_config_.message_queue_size);

    // Set power management callbacks from config
    if (config.getPrepareSleepCallback()) {
        prepare_sleep_callback_ = config.getPrepareSleepCallback();
    }
    if (config.getWakeUpCallback()) {
        wake_up_callback_ = config.getWakeUpCallback();
    }

    // Set node capabilities from config
    if (config.getNodeCapabilities() != 0) {
        network_service_->SetLocalNodeCapabilities(
            config.getNodeCapabilities());
    }

    return Result::Success();
}

Result LoRaMeshProtocol::Start() {
    if (!hardware_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Hardware not initialized");
    }

    LOG_DEBUG("Starting LoRaMesh protocol...");

    if (!radio_event_queue_) {
        radio_event_queue_ =
            GetRTOS().CreateQueue(RADIO_QUEUE_SIZE, sizeof(radio::RadioEvent*));
        if (!radio_event_queue_) {
            LOG_ERROR("Failed to create radio event queue");
            return Result(LoraMesherErrorCode::kConfigurationError,
                          "Failed to create radio event queue");
        }
    }

    if (!protocol_notification_queue_) {
        protocol_notification_queue_ = GetRTOS().CreateQueue(
            PROTOCOL_NOTIFICATION_QUEUE_SIZE, sizeof(ProtocolNotificationType));
        if (!protocol_notification_queue_) {
            LOG_ERROR("Failed to create protocol notification queue");
            return Result(LoraMesherErrorCode::kConfigurationError,
                          "Failed to create protocol notification queue");
        }
    }

    if (!slot_transition_queue_) {
        slot_transition_queue_ =
            GetRTOS().CreateQueue(4, sizeof(SlotTransitionData));
        if (!slot_transition_queue_) {
            LOG_ERROR("Failed to create slot transition queue");
            return Result(LoraMesherErrorCode::kConfigurationError,
                          "Failed to create slot transition queue");
        }
    }

    LOG_DEBUG("Starting LoRaMesh protocol... for node 0x%04X", node_address_);

    // Start hardware
    Result result = hardware_->Start();
    if (!result) {
        LOG_ERROR("Failed to start hardware: %s",
                  result.GetErrorMessage().c_str());
        return result;
    }

    // Start superframe service
    result = superframe_service_->StartSuperframe();
    if (!result) {
        LOG_ERROR("Failed to start superframe service: %s",
                  result.GetErrorMessage().c_str());
        return result;
    }

    result = StartDiscovery();
    if (!result) {
        LOG_ERROR("Failed to start discovery: %s",
                  result.GetErrorMessage().c_str());
        return result;
    }

    // Resume protocol task
    GetRTOS().ResumeTask(protocol_task_handle_);

    LOG_INFO("LoRaMesh protocol started");
    return Result::Success();
}

Result LoRaMeshProtocol::Stop() {
    LOG_DEBUG("Stopping LoRaMesh protocol... for node 0x%04X", node_address_);

    if (protocol_task_handle_) {
        LOG_DEBUG("Stop method deleting task handle: %p",
                  protocol_task_handle_);
        GetRTOS().DeleteTask(protocol_task_handle_);
        protocol_task_handle_ = nullptr;
        LOG_DEBUG("Stop method task handle set to nullptr");
    } else {
        LOG_DEBUG("Stop method task handle already null");
    }

    LOG_DEBUG("Protocol task deletion requested");

    // Stop services after task is suspended
    if (superframe_service_) {
        superframe_service_->StopSuperframe();
    }

    // Reset network state to prevent memory leaks (after task is suspended)
    if (network_service_) {
        network_service_->ResetNetworkState();
    }

    // Clear callbacks to prevent use-after-free
    if (hardware_) {
        hardware_->setActionReceive(nullptr);
    }
    if (superframe_service_) {
        superframe_service_->SetSuperframeCallback(nullptr);
    }

    // Clean up queues
    if (radio_event_queue_) {
        DrainRadioEventQueue();
        GetRTOS().DeleteQueue(radio_event_queue_);
        radio_event_queue_ = nullptr;
    }

    if (protocol_notification_queue_) {
        GetRTOS().DeleteQueue(protocol_notification_queue_);
        protocol_notification_queue_ = nullptr;
    }

    if (slot_transition_queue_) {
        GetRTOS().DeleteQueue(slot_transition_queue_);
        slot_transition_queue_ = nullptr;
    }

    LOG_INFO("LoRaMesh protocol stopped");
    return Result::Success();
}

Result LoRaMeshProtocol::SendMessage(const BaseMessage& message) {
    if (!hardware_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Hardware not initialized");
    }

    // Determine message type and queue it appropriately
    SlotAllocation::SlotType slot_type;

    switch (message.GetType()) {
        case MessageType::DATA:
            slot_type = SlotAllocation::SlotType::TX;
            break;
        case MessageType::ROUTE_TABLE:
        case MessageType::JOIN_REQUEST:
        case MessageType::JOIN_RESPONSE:
        case MessageType::SLOT_REQUEST:
        case MessageType::SLOT_ALLOCATION:
            slot_type = SlotAllocation::SlotType::CONTROL_TX;
            break;
        default:
            slot_type = SlotAllocation::SlotType::TX;
            break;
    }

    // Queue the message
    auto msg_copy = std::make_unique<BaseMessage>(message);
    message_queue_service_->AddMessageToQueue(slot_type, std::move(msg_copy));

    LOG_DEBUG("Message queued for transmission in %s slot",
              slot_utils::SlotTypeToString(slot_type).c_str());

    return Result::Success();
}

Result LoRaMeshProtocol::SendData(AddressType destination,
                                  const std::vector<uint8_t>& data) {
    if (!network_service_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Network service not initialized");
    }

    return network_service_->SendData(destination, data);
}

Result LoRaMeshProtocol::Pause() {
    // Suspend the protocol task
    if (protocol_task_handle_) {
        bool suspended = GetRTOS().SuspendTask(protocol_task_handle_);
        if (!suspended) {
            return Result(LoraMesherErrorCode::kInvalidState,
                          "Failed to suspend protocol task");
        }
    }

    // Pause superframe service
    if (superframe_service_) {
        superframe_service_->SetAutoAdvance(false);
    }

    // Set radio to sleep
    if (hardware_) {
        hardware_->setState(radio::RadioState::kSleep);
    }

    LOG_INFO("Protocol paused");
    return Result::Success();
}

Result LoRaMeshProtocol::Resume() {
    // Resume the protocol task
    if (protocol_task_handle_) {
        bool resumed = GetRTOS().ResumeTask(protocol_task_handle_);
        if (!resumed) {
            return Result(LoraMesherErrorCode::kInvalidState,
                          "Failed to resume protocol task");
        }
    }

    // Resume superframe service
    if (superframe_service_) {
        superframe_service_->SetAutoAdvance(true);
    }

    LOG_INFO("Protocol resumed");
    return Result::Success();
}

lora_mesh::INetworkService::ProtocolState LoRaMeshProtocol::GetState() const {
    return network_service_->GetState();
}

bool LoRaMeshProtocol::IsSynchronized() const {
    if (!network_service_ || !superframe_service_) {
        return false;  // Services not initialized
    }

    bool network_synchronized = network_service_->IsSynchronized();
    bool superframe_synchronized = superframe_service_->IsSynchronized();

    if (!network_synchronized) {
        LOG_WARNING("Network service is not synchronized");
    }

    if (!superframe_synchronized) {
        LOG_WARNING("Superframe service is not synchronized");
    }

    return network_synchronized &&
           superframe_synchronized;  // Both must be synchronized
}

AddressType LoRaMeshProtocol::GetNetworkManager() const {
    return network_service_->GetNetworkManagerAddress();
}

uint16_t LoRaMeshProtocol::GetCurrentSlot() const {
    return superframe_service_->GetCurrentSlot();
}

uint32_t LoRaMeshProtocol::GetSlotDuration() const {
    return superframe_service_->GetSlotDuration();
}

void LoRaMeshProtocol::SetRouteUpdateCallback(
    lora_mesh::INetworkService::RouteUpdateCallback callback) {
    network_service_->SetRouteUpdateCallback(callback);
}

void LoRaMeshProtocol::SetDataReceivedCallback(
    lora_mesh::INetworkService::DataReceivedCallback callback) {
    network_service_->SetDataReceivedCallback(callback);
}

void LoRaMeshProtocol::SetNodeCapabilities(uint8_t capabilities) {
    if (network_service_) {
        network_service_->SetLocalNodeCapabilities(capabilities);
    }
}

uint8_t LoRaMeshProtocol::GetLocalNodeCapabilities() const {
    if (network_service_) {
        return network_service_->GetLocalNodeCapabilities();
    }
    return 0;
}

uint8_t LoRaMeshProtocol::GetNodeCapabilities(AddressType node_address) const {
    if (network_service_) {
        return network_service_->GetNodeCapabilities(node_address);
    }
    return 0;
}

const std::vector<NetworkNodeRoute>& LoRaMeshProtocol::GetNetworkNodes() const {
    return network_service_->GetNetworkNodes();
}

std::vector<NetworkNodeRoute> LoRaMeshProtocol::GetNetworkNodesCopy() const {
    return network_service_->GetNetworkNodesCopy();
}

const LoRaMeshProtocol::ServiceConfiguration&
LoRaMeshProtocol::GetServiceConfiguration() const {
    return service_config_;
}

void LoRaMeshProtocol::ProtocolTaskFunction(void* parameters) {
    auto* protocol = static_cast<LoRaMeshProtocol*>(parameters);
    if (!protocol) {
        return;
    }

    auto& rtos = GetRTOS();

    // Set node address in RTOS for multi-node identification in logs
    char address_str[8];
    snprintf(address_str, sizeof(address_str), "0x%04X",
             protocol->GetNodeAddress());
    rtos.SetCurrentTaskNodeAddress(address_str);

    LOG_INFO("Protocol task started");

    Result result = Result::Success();

    while (!rtos.ShouldStopOrPause()) {
        // Calculate timeout based on current state
        uint32_t timeout_ms = QUEUE_WAIT_TIMEOUT_MS;

        // Add safety check to prevent use-after-free
        if (!protocol->network_service_) {
            LOG_DEBUG(
                "Network service no longer available, exiting protocol task");
            break;
        }

        auto state = protocol->network_service_->GetState();

        // Set state-specific timeouts for discovery and joining
        switch (state) {
            case lora_mesh::INetworkService::ProtocolState::DISCOVERY:
                timeout_ms =
                    std::min(timeout_ms, protocol->GetDiscoveryTimeout());
                break;
            case lora_mesh::INetworkService::ProtocolState::JOINING:
                timeout_ms = std::min(timeout_ms, protocol->GetJoinTimeout());
                break;
            case lora_mesh::INetworkService::ProtocolState::FAULT_RECOVERY:
                // Keep polling so we notice election_end_time_ expiry
                timeout_ms =
                    std::min(timeout_ms, protocol->GetDiscoveryTimeout());
                break;
            case lora_mesh::INetworkService::ProtocolState::NM_ELECTION:
                timeout_ms = std::min(
                    timeout_ms,
                    protocol->network_service_->GetNMElectionTimeout());
                break;
            default:
                // Use default timeout for other states
                break;
        }

        // Wait for protocol notifications with calculated timeout
        ProtocolNotificationType notification;
        auto queue_result = rtos.ReceiveFromQueue(
            protocol->protocol_notification_queue_, &notification, timeout_ms);

        if (queue_result == os::QueueResult::kOk) {
            // Process received notification
            switch (notification) {
                case ProtocolNotificationType::RADIO_EVENT:
                    protocol->ProcessRadioEvents();
                    break;

                case ProtocolNotificationType::STATE_TIMEOUT:
                    // Handle timeout-based state transitions
                    switch (state) {
                        case lora_mesh::INetworkService::ProtocolState::
                            DISCOVERY:
                            result =
                                protocol->network_service_->PerformDiscovery(
                                    protocol->GetDiscoveryTimeout());
                            if (!result) {
                                LOG_ERROR("Discovery failed: %s",
                                          result.GetErrorMessage().c_str());
                            }
                            break;

                        case lora_mesh::INetworkService::ProtocolState::JOINING:
                            result = protocol->network_service_->PerformJoining(
                                protocol->GetJoinTimeout());
                            if (!result) {
                                LOG_ERROR("Joining failed: %s",
                                          result.GetErrorMessage().c_str());
                            }
                            break;

                        case lora_mesh::INetworkService::ProtocolState::
                            FAULT_RECOVERY:
                            // Election countdown is handled by HandleSuperframeStart.
                            // For NODE_ONLY nodes (no election), restart discovery.
                            if (!protocol->network_service_
                                     ->IsElectionPending()) {
                                LOG_WARNING(
                                    "FAULT_RECOVERY: no election pending, "
                                    "restarting discovery");
                                result = protocol->StartDiscovery();
                                if (!result) {
                                    LOG_ERROR("Failed to restart discovery: %s",
                                              result.GetErrorMessage().c_str());
                                }
                            }
                            break;

                        case lora_mesh::INetworkService::ProtocolState::
                            NM_ELECTION:
                            result =
                                protocol->network_service_->PerformNMElection();
                            if (!result) {
                                LOG_ERROR("NM election failed: %s",
                                          result.GetErrorMessage().c_str());
                            }
                            break;

                        default:
                            // No timeout action needed for other states
                            break;
                    }
                    break;

                case ProtocolNotificationType::STATE_CHANGE:
                    // State changed, continue processing with new state
                    LOG_DEBUG("Protocol state changed, continuing processing");
                    break;

                case ProtocolNotificationType::SLOT_TRANSITION: {
                    // Drain all pending slot transitions (multiple may have queued
                    // while the protocol task was busy processing a previous slot).
                    SlotTransitionData data;
                    while (rtos.ReceiveFromQueue(
                               protocol->slot_transition_queue_, &data, 0) ==
                           os::QueueResult::kOk) {
                        protocol->current_slot_arrival_time_ms_ =
                            data.arrival_time_ms;
                        protocol->OnSlotTransition(data.slot,
                                                   data.new_superframe);
                    }
                    break;
                }

                case ProtocolNotificationType::SHUTDOWN:
                    LOG_INFO("Protocol shutdown requested");
                    return;

                default:
                    LOG_WARNING("Unknown protocol notification type: %d",
                                static_cast<int>(notification));
                    break;
            }
        } else if (queue_result == os::QueueResult::kTimeout) {
            // Timeout occurred - trigger state timeout processing
            switch (state) {
                case lora_mesh::INetworkService::ProtocolState::DISCOVERY:
                    result = protocol->network_service_->PerformDiscovery(
                        protocol->GetDiscoveryTimeout());
                    if (!result) {
                        LOG_ERROR("Discovery failed: %s",
                                  result.GetErrorMessage().c_str());
                    }
                    break;

                case lora_mesh::INetworkService::ProtocolState::JOINING:
                    result = protocol->network_service_->PerformJoining(
                        protocol->GetJoinTimeout());
                    if (!result) {
                        LOG_ERROR("Joining failed: %s",
                                  result.GetErrorMessage().c_str());
                    }
                    break;

                case lora_mesh::INetworkService::ProtocolState::FAULT_RECOVERY:
                    // For NODE_ONLY nodes (no election pending), restart discovery
                    if (!protocol->network_service_->IsElectionPending()) {
                        LOG_WARNING(
                            "FAULT_RECOVERY timeout - restarting discovery");
                        result = protocol->StartDiscovery();
                        if (!result) {
                            LOG_ERROR("Failed to restart discovery: %s",
                                      result.GetErrorMessage().c_str());
                        }
                    }
                    break;

                case lora_mesh::INetworkService::ProtocolState::NM_ELECTION:
                    result = protocol->network_service_->PerformNMElection();
                    if (!result) {
                        LOG_ERROR("NM election failed: %s",
                                  result.GetErrorMessage().c_str());
                    }
                    break;

                default:
                    // No timeout action needed for other states
                    break;
            }
        }

        // Yield to other tasks for responsive shutdown
        rtos.YieldTask();
    }

    LOG_INFO("Protocol task ending");
    // Note: Task handle is cleared and DeleteTask is called from Stop() method
    LOG_DEBUG("LoRaMeshProtocol ProtocolTaskFunction exiting naturally");
}

void LoRaMeshProtocol::ProcessRadioEvents() {
    radio::RadioEvent* raw_event_ptr = nullptr;

    // Check for any queued radio events (with zero timeout)
    if (GetRTOS().ReceiveFromQueue(radio_event_queue_, &raw_event_ptr, 0) ==
            os::QueueResult::kOk &&
        raw_event_ptr != nullptr) {

        // Immediately wrap in unique_ptr for automatic cleanup
        std::unique_ptr<radio::RadioEvent> event(raw_event_ptr);

        // Process the event
        if (event->HasMessage()) {
            const BaseMessage* message = event->getMessage();
            if (message) {
                // Only process received events as received messages
                if (event->getType() == radio::RadioEventType::kReceived) {
                    // Extract the reception timestamp from the RadioEvent
                    uint32_t reception_timestamp = event->getTimestamp();
                    network_service_->ProcessReceivedMessage(
                        *message, reception_timestamp);
                    // During subslotted slots, stay in RX to catch more
                    // transmissions from other subslots
                    if (in_subslotted_slot_) {
                        Result result =
                            hardware_->setState(radio::RadioState::kReceive);
                        if (!result) {
                            LOG_WARNING(
                                "Failed to set radio to receive in "
                                "subslotted slot: %s",
                                result.GetErrorMessage().c_str());
                        }
                    } else {
                        Result result =
                            hardware_->setState(radio::RadioState::kSleep);
                        if (!result) {
                            LOG_WARNING(
                                "Failed to set radio to sleep state: %s",
                                result.GetErrorMessage().c_str());
                        }
                    }
                } else if (event->getType() ==
                           radio::RadioEventType::kTransmitted) {
                    // After TX in subslotted slots, return to RX to catch
                    // transmissions from later subslots
                    if (in_subslotted_slot_) {
                        Result result =
                            hardware_->setState(radio::RadioState::kReceive);
                        if (!result) {
                            LOG_WARNING(
                                "Failed to set radio to receive after TX "
                                "in subslotted slot: %s",
                                result.GetErrorMessage().c_str());
                        }
                    } else {
                        Result result =
                            hardware_->setState(radio::RadioState::kSleep);
                        if (!result) {
                            LOG_WARNING(
                                "Failed to set radio to sleep state: %s",
                                result.GetErrorMessage().c_str());
                        }
                    }
                    LOG_DEBUG("Processed radio event for transmitted message");
                } else {
                    LOG_DEBUG("Processed radio event with message of type %d",
                              static_cast<int>(event->getType()));
                }
            }
        } else {
            // TODO: Handle radio events without messages
            LOG_DEBUG("Received radio event without message");
        }
    }
}

void LoRaMeshProtocol::OnSlotTransition(uint16_t current_slot,
                                        bool new_superframe) {
    // Reset subslotted slot flag at every slot transition
    in_subslotted_slot_ = false;

    // Finalize NM election once counter-claim window has closed
    if (network_service_->GetState() ==
        lora_mesh::INetworkService::ProtocolState::NM_ELECTION) {
        network_service_->PerformNMElection();
    }

    // Handle new superframe
    if (new_superframe) {
        network_service_->HandleSuperframeStart();
    }

    // Get current slot type from allocation table
    SlotAllocation::SlotType slot_type = SlotAllocation::SlotType::SLEEP;

    for (const auto& allocation : GetSlotTable()) {
        if (allocation.slot_number == current_slot) {
            slot_type = allocation.type;
            break;
        }
    }

    LOG_INFO("Slot %d transition: type=%s%s", current_slot,
             slot_utils::SlotTypeToString(slot_type).c_str(),
             new_superframe ? " (new superframe)" : "");

    // Process messages based on slot type
    ProcessSlotMessages(slot_type);
}

void LoRaMeshProtocol::OnStateChange(
    lora_mesh::INetworkService::ProtocolState new_state) {

    LOG_INFO("Protocol state changed to %d", static_cast<int>(new_state));

    // Notify protocol task of state change for immediate processing
    NotifyProtocolTask(ProtocolNotificationType::STATE_CHANGE);
}

void LoRaMeshProtocol::OnNetworkTopologyChange(bool route_updated,
                                               AddressType destination,
                                               AddressType next_hop,
                                               uint8_t hop_count) {
    if (!route_updated) {
        LOG_DEBUG("Route removed: dest=0x%04X via=0x%04X hops=%d", destination,
                  next_hop, hop_count);
        return;  // No route update needed
    }
    // Network topology changed - may need to update slot allocations
    // This would be handled by NetworkService internally
    // LOG_DEBUG("Network topology changed");

    // // switch for state change
    // auto state = network_service_->GetState();
    // switch (state) {
    //     case lora_mesh::INetworkService::ProtocolState::NORMAL_OPERATION:
    //     case lora_mesh::INetworkService::ProtocolState::NETWORK_MANAGER:
    //         // Update slot allocations based on new topology
    //         network_service_->UpdateSlotAllocations();
    //     case lora_mesh::INetworkService::ProtocolState::DISCOVERY:
    //     case lora_mesh::INetworkService::ProtocolState::INITIALIZING:
    //     case lora_mesh::INetworkService::ProtocolState::FAULT_RECOVERY:
    //         break;

    //     case lora_mesh::INetworkService::ProtocolState::JOINING:

    //     default:
    //         LOG_WARNING("Unhandled state for topology change: %d",
    //                     static_cast<int>(state));
    //         break;
    // }
}

void LoRaMeshProtocol::ProcessSlotMessages(SlotAllocation::SlotType slot_type) {
    Result result;

    // Handle wake-up callback when transitioning from SLEEP to an active slot
    // This allows applications to restore peripherals that were disabled during sleep
    if (current_power_state_ == power::PowerState::LIGHT_SLEEP &&
        slot_type != SlotAllocation::SlotType::SLEEP) {
        // Invoke wake callback before processing the new slot
        // This ensures peripherals are ready before any TX/RX operations
        if (wake_up_callback_) {
            LOG_DEBUG("Invoking wake-up callback from LIGHT_SLEEP");
            wake_up_callback_(power::PowerState::LIGHT_SLEEP);
        }
        // Update power state to ACTIVE now that we're in an active slot
        current_power_state_ = power::PowerState::ACTIVE;
    }

    switch (slot_type) {
        case SlotAllocation::SlotType::CONTROL_TX: {
            // State-based message sending for CONTROL_TX
            auto state = network_service_->GetState();

            if (state == lora_mesh::INetworkService::ProtocolState::
                             NORMAL_OPERATION ||
                state == lora_mesh::INetworkService::ProtocolState::
                             NETWORK_MANAGER) {
                // Normal operation - send routing table updates
                result = AddRoutingMessageToQueueService();
                if (!result) {
                    LOG_DEBUG("Failed to add routing message to queue: %s",
                              result.GetErrorMessage().c_str());
                }
            }
        }
        case SlotAllocation::SlotType::TX: {
            // Apply guard time delay for TX slots to allow RX nodes to prepare
            uint32_t guard_time_ms = config_.getGuardTime();
            if (guard_time_ms > 0) {
                LOG_DEBUG("Applying guard time delay: %u ms", guard_time_ms);
                GetRTOS().delay(guard_time_ms);
            }

            // Extract and send the queued message
            auto message =
                message_queue_service_->ExtractMessageOfType(slot_type);
            if (message) {
                // Send via hardware
                result = hardware_->SendMessage(*message);
                if (!result) {
                    LOG_ERROR("Failed to send control message: %s",
                              result.GetErrorMessage().c_str());
                } else {
                    LOG_DEBUG("Sent message type %d",
                              static_cast<int>(message->GetType()));
                }
            } else {
                LOG_DEBUG("No control message to send in slot %d", slot_type);
            }
            break;
        }
        case SlotAllocation::SlotType::DISCOVERY_TX: {
            // Subslot-based collision mitigation: discovering nodes transmit
            // at different offsets based on their address (ADDRESS_MODULO).
            // Radio starts in RX immediately to catch messages from other
            // subslots.
            result = hardware_->setState(radio::RadioState::kReceive);
            if (!result) {
                LOG_WARNING(
                    "Failed to set radio to receive for discovery TX: %s",
                    result.GetErrorMessage().c_str());
            }
            in_subslotted_slot_ = true;

            // Extract discovery message from queue
            auto message =
                message_queue_service_->ExtractMessageOfType(slot_type);
            if (message) {
                // Skip TX if the slot callback arrived too late to complete
                // transmission before the slot boundary.
                {
                    uint32_t time_in_slot = current_slot_arrival_time_ms_;
                    uint32_t slot_duration =
                        superframe_service_->GetSlotDuration();
                    if (time_in_slot + kLateArrivalMarginMs > slot_duration) {
                        LOG_WARNING(
                            "DISCOVERY_TX: arrived too late (%u ms), skipping "
                            "TX",
                            time_in_slot);
                        hardware_->setState(radio::RadioState::kReceive);
                        break;
                    }
                }

                // Compute subslot timing; use random identifier for RANDOM strategy
                uint16_t identifier = node_address_;
                if (config_.getDiscoverySubslotConfig().strategy ==
                    lora_mesh::SubslotAssignment::RANDOM) {
                    identifier = static_cast<uint16_t>(GetRTOS().GetRandom());
                }
                auto subslot_timing =
                    lora_mesh::SubslotScheduler::ComputeTiming(
                        superframe_service_->GetSlotDuration(),
                        config_.getDiscoverySubslotConfig(), identifier);

                // Wait until our subslot TX offset
                if (subslot_timing.is_valid) {
                    uint32_t time_in_slot =
                        superframe_service_->GetTimeInSlot();
                    if (time_in_slot < subslot_timing.tx_start_offset_ms) {
                        uint32_t delay_ms =
                            subslot_timing.tx_start_offset_ms - time_in_slot;
                        LOG_DEBUG(
                            "Waiting %u ms for discovery subslot %d "
                            "(addr=0x%04X)",
                            delay_ms, subslot_timing.assigned_subslot,
                            node_address_);
                        GetRTOS().delay(delay_ms);
                    }
                }

                // Send discovery message
                result = hardware_->SendMessage(*message);
                if (!result) {
                    LOG_ERROR("Failed to send discovery message: %s",
                              result.GetErrorMessage().c_str());
                } else {
                    LOG_DEBUG(
                        "Sent discovery message in subslot %d (addr=0x%04X)",
                        subslot_timing.assigned_subslot, node_address_);
                }
                // Radio will return to RX after TX via ProcessRadioEvents
            } else {
                LOG_DEBUG("No discovery message to send in DISCOVERY_TX slot");
            }
            break;
        }

        case SlotAllocation::SlotType::SYNC_BEACON_TX: {
            // Subslot-based collision mitigation: nodes at different hop
            // distances transmit at different offsets within the slot.
            // Radio starts in RX immediately to catch beacons from earlier
            // subslots.
            auto state = network_service_->GetState();

            // Compute subslot timing using ADDRESS_MODULO strategy
            auto subslot_timing = lora_mesh::SubslotScheduler::ComputeTiming(
                superframe_service_->GetSlotDuration(),
                config_.getSyncBeaconSubslotConfig(), node_address_);

            // Start in RX to catch beacons from earlier subslots
            result = hardware_->setState(radio::RadioState::kReceive);
            if (!result) {
                LOG_WARNING(
                    "Failed to set radio to receive for sync beacon: %s",
                    result.GetErrorMessage().c_str());
            }
            in_subslotted_slot_ = true;

            if (state ==
                lora_mesh::INetworkService::ProtocolState::NETWORK_MANAGER) {
                // Network Manager: queue the sync beacon
                result = network_service_->SendSyncBeacon();
                if (!result) {
                    LOG_ERROR("Failed to queue sync beacon: %s",
                              result.GetErrorMessage().c_str());
                    break;
                }
            }

            // Extract the message (NM's own beacon or forwarded beacon)
            auto message =
                message_queue_service_->ExtractMessageOfType(slot_type);
            if (message) {
                // Skip TX if the slot callback arrived too late to complete
                // transmission before the slot boundary. Radio is already in
                // kReceive from above, so it is safe to bail out here.
                {
                    uint32_t time_in_slot = current_slot_arrival_time_ms_;
                    uint32_t slot_duration =
                        superframe_service_->GetSlotDuration();
                    if (time_in_slot + kLateArrivalMarginMs > slot_duration) {
                        LOG_WARNING(
                            "SYNC_BEACON_TX: arrived too late (%u ms), "
                            "skipping TX",
                            time_in_slot);
                        break;
                    }
                }

                // Wait until our subslot TX offset
                if (subslot_timing.is_valid) {
                    uint32_t time_in_slot =
                        superframe_service_->GetTimeInSlot();
                    if (time_in_slot < subslot_timing.tx_start_offset_ms) {
                        uint32_t delay_ms =
                            subslot_timing.tx_start_offset_ms - time_in_slot;
                        LOG_DEBUG("Waiting %u ms for subslot %d (addr=0x%04X)",
                                  delay_ms, subslot_timing.assigned_subslot,
                                  node_address_);
                        GetRTOS().delay(delay_ms);
                    }
                }

                // Invoke pre-send callback to update time-sensitive fields
                message->InvokePreSendCallback();

                result = hardware_->SendMessage(*message);
                if (!result) {
                    LOG_ERROR("Failed to send sync beacon: %s",
                              result.GetErrorMessage().c_str());
                } else {
                    LOG_DEBUG("Sent sync beacon in subslot %d (addr=0x%04X)",
                              subslot_timing.assigned_subslot, node_address_);
                }
            } else {
                LOG_DEBUG("No sync beacon to send/forward");
            }
            break;
        }

        case SlotAllocation::SlotType::DISCOVERY_RX: {
            // Pure listen slot for receiving discovery messages.
            result = hardware_->setState(radio::RadioState::kReceive);
            if (!result) {
                LOG_ERROR("Failed to set radio to receive for discovery: %s",
                          result.GetErrorMessage().c_str());
            }
            in_subslotted_slot_ = true;

            // TODO: Join responses are queued as DISCOVERY_TX but the joining
            // node doesn't allocate a DISCOVERY_TX slot. The message is sent
            // from the first DISCOVERY_RX slot instead. This should be
            // refactored so that joining allocates a proper DISCOVERY_TX slot.

            // Check if there are any queued discovery TX messages (fallback)
            auto discovery_message =
                message_queue_service_->ExtractMessageOfType(
                    SlotAllocation::SlotType::DISCOVERY_TX);
            if (discovery_message) {
                // Skip TX if the slot callback arrived too late to complete
                // transmission before the slot boundary.
                {
                    uint32_t time_in_slot = current_slot_arrival_time_ms_;
                    uint32_t slot_duration =
                        superframe_service_->GetSlotDuration();
                    if (time_in_slot + kLateArrivalMarginMs > slot_duration) {
                        LOG_WARNING(
                            "DISCOVERY_RX fallback TX: arrived too late (%u "
                            "ms), skipping TX",
                            time_in_slot);
                        hardware_->setState(radio::RadioState::kReceive);
                        break;
                    }
                }

                // Compute subslot timing; use random identifier for RANDOM strategy
                uint16_t disc_identifier = node_address_;
                if (config_.getDiscoverySubslotConfig().strategy ==
                    lora_mesh::SubslotAssignment::RANDOM) {
                    disc_identifier =
                        static_cast<uint16_t>(GetRTOS().GetRandom());
                }
                auto subslot_timing =
                    lora_mesh::SubslotScheduler::ComputeTiming(
                        superframe_service_->GetSlotDuration(),
                        config_.getDiscoverySubslotConfig(), disc_identifier);

                // Wait until our subslot TX offset
                if (subslot_timing.is_valid) {
                    uint32_t time_in_slot =
                        superframe_service_->GetTimeInSlot();
                    if (time_in_slot < subslot_timing.tx_start_offset_ms) {
                        uint32_t delay_ms =
                            subslot_timing.tx_start_offset_ms - time_in_slot;
                        LOG_DEBUG(
                            "Waiting %u ms for discovery subslot %d "
                            "(addr=0x%04X)",
                            delay_ms, subslot_timing.assigned_subslot,
                            node_address_);
                        GetRTOS().delay(delay_ms);
                    }
                }

                // Send discovery message
                result = hardware_->SendMessage(*discovery_message);
                if (!result) {
                    LOG_ERROR("Failed to send discovery message: %s",
                              result.GetErrorMessage().c_str());
                } else {
                    LOG_DEBUG(
                        "Sent discovery message in subslot %d (addr=0x%04X) "
                        "from DISCOVERY_RX fallback",
                        subslot_timing.assigned_subslot, node_address_);
                }
                // Radio will return to RX after TX via ProcessRadioEvents
            }
            break;
        }
        case SlotAllocation::SlotType::RX:
        case SlotAllocation::SlotType::CONTROL_RX:
        case SlotAllocation::SlotType::SYNC_BEACON_RX:
            // Set radio to receive mode for all RX slot types
            result = hardware_->setState(radio::RadioState::kReceive);
            if (!result) {
                LOG_ERROR("Failed to set radio to receive: %s",
                          result.GetErrorMessage().c_str());
            }
            break;

        case SlotAllocation::SlotType::SLEEP:
        default: {
            // Build sleep context for user callback
            // This provides all information needed to make intelligent sleep decisions
            power::SleepContext ctx{};
            ctx.requested_state = power::PowerState::LIGHT_SLEEP;
            ctx.current_slot = superframe_service_->GetCurrentSlot();
            ctx.has_pending_messages = message_queue_service_->HasAnyMessages();

            // Calculate sleep duration (time until next slot)
            // Users can use this to decide if sleep is worthwhile for short durations
            uint32_t slot_duration = superframe_service_->GetSlotDuration();
            ctx.sleep_duration_ms = slot_duration;

            // Call user callback if registered
            // This allows application-level power management (disable GPS, sensors, etc.)
            if (prepare_sleep_callback_) {
                LOG_DEBUG("Invoking prepare-sleep callback for slot %u",
                          ctx.current_slot);
                auto sleep_result = prepare_sleep_callback_(ctx);

                if (!sleep_result.allow_sleep) {
                    // User vetoed sleep - still set radio to sleep for power savings
                    // but don't track as "sleeping" (wake callback won't fire)
                    LOG_DEBUG("Sleep vetoed by user callback");
                    result = hardware_->setState(radio::RadioState::kSleep);
                    if (!result) {
                        LOG_ERROR("Failed to set radio to sleep: %s",
                                  result.GetErrorMessage().c_str());
                    }
                    // Note: current_power_state_ remains ACTIVE
                    break;
                }
            }

            // Set radio to sleep mode
            result = hardware_->setState(radio::RadioState::kSleep);
            if (!result) {
                LOG_ERROR("Failed to set radio to sleep: %s",
                          result.GetErrorMessage().c_str());
            }

            // Sleep the MCU until the next slot (radio is already sleeping)
            GetRTOS().LightSleep(ctx.sleep_duration_ms);

            // Update power state to track for wake callback
            // This ensures WakeUpCallback fires on next active slot
            current_power_state_ = power::PowerState::LIGHT_SLEEP;
            break;
        }
    }
}

LoRaMeshProtocol::ServiceConfiguration LoRaMeshProtocol::CreateServiceConfig(
    const LoRaMeshProtocolConfig& config) {

    ServiceConfiguration service_config;

    // Network configuration
    service_config.network_config.node_address = config.getNodeAddress();
    service_config.network_config.hello_interval_ms = 60000;
    service_config.network_config.route_timeout_ms = 180000;
    service_config.network_config.node_timeout_ms = 300000;
    service_config.network_config.max_hops = config.getMaxHops();
    service_config.network_config.max_packet_size = config.getMaxPacketSize();
    service_config.network_config.default_data_slots =
        config.getDefaultDataSlots();
    service_config.network_config.max_network_nodes = 50;
    service_config.network_config.guard_time_ms = config.getGuardTime();

    // Message queue configuration
    service_config.message_queue_size = 10;

    // Superframe update interval
    service_config.superframe_update_interval_ms = 20;

    return service_config;
}

#ifdef DEBUG
LoRaMeshProtocol::ServiceConfiguration
LoRaMeshProtocol::CreateServiceConfigForTest(
    const LoRaMeshProtocolConfig& config) {

    ServiceConfiguration service_config;

    // Network configuration
    service_config.network_config.node_address = config.getNodeAddress();
    service_config.network_config.hello_interval_ms = DEFAULT_HELLO_INTERVAL_MS;
    service_config.network_config.route_timeout_ms =
        service_config.network_config.hello_interval_ms * 3;
    service_config.network_config.node_timeout_ms =
        service_config.network_config.hello_interval_ms * 3;
    service_config.network_config.max_hops = config.getMaxHops();
    service_config.network_config.max_packet_size = config.getMaxPacketSize();
    service_config.network_config.default_data_slots =
        config.getDefaultDataSlots();
    service_config.network_config.max_network_nodes = 50;

    // Message queue configuration
    service_config.message_queue_size = 10;

    // Superframe update interval
    service_config.superframe_update_interval_ms = 20;

    return service_config;
}
#endif  // DEBUG

Result LoRaMeshProtocol::StartDiscovery() {
    if (!superframe_service_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Superframe service not initialized");
    }

    // Clean DISCOVERY_TX data messages
    if (message_queue_service_) {
        message_queue_service_->ClearQueue(
            SlotAllocation::SlotType::DISCOVERY_TX);
    }

    // Start discovery in superframe service
    Result result = superframe_service_->StartSuperframeDiscovery();
    if (!result) {
        return result;
    }

    // Notify network service to start discovery
    return network_service_->StartDiscovery(
        superframe_service_->GetDiscoveryTimeout());
}

uint32_t LoRaMeshProtocol::GetDiscoveryTimeout() {
    if (!superframe_service_) {
        return 0;  // No discovery timeout if service not initialized
    }

    return superframe_service_->GetDiscoveryTimeout();
}

uint32_t LoRaMeshProtocol::GetJoinTimeout() {
    if (!network_service_) {
        return 0;  // No join timeout if service not initialized
    }

    // Join timeout is typically the superframe duration
    return network_service_->GetJoinTimeout();
}

uint32_t LoRaMeshProtocol::GetSlotDuration() {
    if (!superframe_service_) {
        return 0;  // No slot duration if service not initialized
    }

    return superframe_service_->GetSlotDuration();
}

uint32_t LoRaMeshProtocol::GetTimeUntilNextDataSlot(
    uint32_t guard_time_ms) const {
    if (!superframe_service_ || !network_service_)
        return 0;

    const uint32_t superframe_duration =
        superframe_service_->GetSuperframeDuration();
    if (superframe_duration == 0)
        return 0;

    const uint32_t now = GetRTOS().getTickCount();
    uint32_t best_wait = UINT32_MAX;

    for (const auto& slot : network_service_->GetSlotTable()) {
        if (slot.type !=
            types::protocols::lora_mesh::SlotAllocation::SlotType::TX)
            continue;

        uint32_t slot_start =
            superframe_service_->GetSlotStartTime(slot.slot_number);
        uint32_t time_until = (slot_start > now) ? (slot_start - now) : 0;

        // If the slot is too close or already past, push to the next superframe
        if (time_until <= guard_time_ms) {
            time_until += superframe_duration;
        }

        uint32_t adjusted = time_until - guard_time_ms;
        if (adjusted < best_wait) {
            best_wait = adjusted;
        }
    }

    return (best_wait == UINT32_MAX) ? 0 : best_wait;
}

uint8_t LoRaMeshProtocol::GetDataSlotsPerSuperframe() const {
    if (!network_service_)
        return 0;
    uint8_t count = 0;
    for (const auto& slot : network_service_->GetSlotTable()) {
        if (slot.type ==
            types::protocols::lora_mesh::SlotAllocation::SlotType::TX)
            count++;
    }
    return count;
}

size_t LoRaMeshProtocol::GetTxQueueSize() const {
    return message_queue_service_->GetQueueSize(
        types::protocols::lora_mesh::SlotAllocation::SlotType::TX);
}

size_t LoRaMeshProtocol::GetRxQueueSize() const {
    return message_queue_service_->GetQueueSize(
        types::protocols::lora_mesh::SlotAllocation::SlotType::RX);
}

Result LoRaMeshProtocol::AddRoutingMessageToQueueService() {
    if (!message_queue_service_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Message queue service not initialized");
    }

    // Search if message_queue_service contains a routing_message and remove it
    message_queue_service_->RemoveMessage(loramesher::MessageType::ROUTE_TABLE);

    // Create a new routing message with broadcast destination
    auto routing_message =
        network_service_->CreateRoutingTableMessage(kBroadcastAddress);
    if (!routing_message) {
        return Result(LoraMesherErrorCode::kMemoryError,
                      "Failed to create routing message");
    }

    // Add routing message to the message queue
    message_queue_service_->AddMessageToQueue(
        SlotAllocation::SlotType::CONTROL_TX, std::move(routing_message));

    LOG_DEBUG("Routing message added to queue for transmission");

    return Result::Success();
}

void LoRaMeshProtocol::DrainRadioEventQueue() {
    if (!radio_event_queue_) {
        return;
    }

    radio::RadioEvent* raw_event_ptr = nullptr;

    // Drain all remaining events from the queue
    while (GetRTOS().ReceiveFromQueue(radio_event_queue_, &raw_event_ptr, 0) ==
               os::QueueResult::kOk &&
           raw_event_ptr != nullptr) {
        // Wrap in unique_ptr for automatic cleanup
        std::unique_ptr<radio::RadioEvent> event(raw_event_ptr);
        // Event is automatically cleaned up when it goes out of scope
    }
}

void LoRaMeshProtocol::NotifyProtocolTask(
    ProtocolNotificationType notification_type) {
    if (!protocol_notification_queue_) {
        LOG_WARNING("Protocol notification queue not initialized");
        return;
    }

    auto& rtos = GetRTOS();
    auto result =
        rtos.SendToQueue(protocol_notification_queue_, &notification_type, 0);

    if (result != os::QueueResult::kOk) {
        LOG_WARNING(
            "Failed to send protocol notification (type: %d), queue full",
            static_cast<int>(notification_type));
    } else {
        LOG_DEBUG("Sent protocol notification (type: %d)",
                  static_cast<int>(notification_type));
    }
}

}  // namespace protocols
}  // namespace loramesher