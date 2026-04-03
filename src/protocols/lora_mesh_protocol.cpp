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

/// Margin for the receiver to finish processing a packet before the slot ends.
/// Covers SPI transfer, interrupt latency, and queue insertion on the RX side.
static constexpr uint32_t kRxProcessingMarginMs = 20;

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
    net_config.link_quality_ewma_alpha = config.getLinkQualityEwmaAlpha();
    net_config.consecutive_missed_for_inactivation =
        config.getConsecutiveMissedForInactivation();
    net_config.min_consecutive_for_reactivation =
        config.getMinConsecutiveForReactivation();

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
        case MessageType::DATA_BROADCAST:
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

Result LoRaMeshProtocol::SendBroadcast(std::span<const uint8_t> data) {
    if (!network_service_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Network service not initialized");
    }

    return network_service_->SendBroadcast(data);
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

    LOG_INFO("Slot %d transition: type=%s start=%u%s", current_slot,
             slot_utils::SlotTypeToString(slot_type).c_str(),
             superframe_service_->GetSlotStartTime(current_slot),
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

bool LoRaMeshProtocol::CanFitInSlot(uint8_t message_size,
                                    uint32_t additional_delay_ms) const {
    if (!hardware_ || !superframe_service_)
        return false;

    uint32_t toa_ms = hardware_->getTimeOnAir(message_size);
    uint32_t time_in_slot = superframe_service_->GetTimeInSlot();
    uint32_t slot_duration = superframe_service_->GetSlotDuration();

    uint32_t needed =
        time_in_slot + additional_delay_ms + toa_ms + kRxProcessingMarginMs;
    if (needed > slot_duration) {
        LOG_WARNING(
            "TX skip: in_slot=%u + delay=%u + ToA=%u + margin=%u = %u > "
            "slot=%u",
            time_in_slot, additional_delay_ms, toa_ms, kRxProcessingMarginMs,
            needed, slot_duration);
        return false;
    }
    return true;
}

Result LoRaMeshProtocol::TrySendGuardedMessage(
    SlotAllocation::SlotType slot_type) {
    uint32_t guard_time_ms = config_.getGuardTime();

    auto message = message_queue_service_->ExtractMessageOfType(slot_type);
    if (!message)
        return Result::Success();

    if (!CanFitInSlot(static_cast<uint8_t>(message->GetTotalSize()),
                      guard_time_ms)) {
        message_queue_service_->AddMessageToQueue(slot_type,
                                                  std::move(message));
        return Result::Success();
    }

    if (guard_time_ms > 0) {
        GetRTOS().delay(guard_time_ms);
    }

    message->InvokePreSendCallback();
    return hardware_->SendMessage(*message);
}

Result LoRaMeshProtocol::TrySendSubslottedMessage(
    SlotAllocation::SlotType slot_type, const lora_mesh::SubslotConfig& config,
    uint16_t identifier) {
    auto message = message_queue_service_->ExtractMessageOfType(slot_type);
    if (!message)
        return Result::Success();

    uint8_t msg_size = static_cast<uint8_t>(message->GetTotalSize());

    auto subslot_timing = lora_mesh::SubslotScheduler::ComputeTiming(
        superframe_service_->GetSlotDuration(), config, identifier);

    bool use_subslot = false;
    if (subslot_timing.is_valid) {
        uint32_t time_in_slot = superframe_service_->GetTimeInSlot();
        uint32_t subslot_delay =
            (time_in_slot < subslot_timing.tx_start_offset_ms)
                ? (subslot_timing.tx_start_offset_ms - time_in_slot)
                : 0;

        if (CanFitInSlot(msg_size, subslot_delay)) {
            use_subslot = true;
            if (subslot_delay > 0) {
                LOG_DEBUG("Waiting %u ms for subslot %d", subslot_delay,
                          subslot_timing.assigned_subslot);
                GetRTOS().delay(subslot_delay);
            }
        }
    }

    if (!use_subslot) {
        if (!CanFitInSlot(msg_size)) {
            message_queue_service_->AddMessageToQueue(slot_type,
                                                      std::move(message));
            return Result::Success();
        }
        LOG_DEBUG("Subslot doesn't fit, sending immediately");
    }

    message->InvokePreSendCallback();
    return hardware_->SendMessage(*message);
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
            auto state = network_service_->GetState();
            if (state == lora_mesh::INetworkService::ProtocolState::
                             NORMAL_OPERATION ||
                state == lora_mesh::INetworkService::ProtocolState::
                             NETWORK_MANAGER) {
                result = AddRoutingMessageToQueueService();
                if (!result) {
                    LOG_DEBUG("Failed to add routing message to queue: %s",
                              result.GetErrorMessage().c_str());
                }
            }
            [[fallthrough]];
        }
        case SlotAllocation::SlotType::TX: {
            result = TrySendGuardedMessage(slot_type);
            if (!result) {
                LOG_ERROR("Failed to send message: %s",
                          result.GetErrorMessage().c_str());
            }
            break;
        }

        case SlotAllocation::SlotType::DISCOVERY_TX: {
            result = hardware_->setState(radio::RadioState::kReceive);
            if (!result) {
                LOG_WARNING(
                    "Failed to set radio to receive for discovery TX: %s",
                    result.GetErrorMessage().c_str());
            }
            in_subslotted_slot_ = true;

            uint16_t identifier = node_address_;
            if (config_.getDiscoverySubslotConfig().strategy ==
                lora_mesh::SubslotAssignment::RANDOM) {
                identifier = static_cast<uint16_t>(GetRTOS().GetRandom());
            }
            result = TrySendSubslottedMessage(
                slot_type, config_.getDiscoverySubslotConfig(), identifier);
            if (!result) {
                LOG_ERROR("Failed to send discovery message: %s",
                          result.GetErrorMessage().c_str());
            }
            break;
        }

        case SlotAllocation::SlotType::SYNC_BEACON_TX: {
            auto state = network_service_->GetState();

            if (state ==
                lora_mesh::INetworkService::ProtocolState::NETWORK_MANAGER) {
                result = network_service_->SendSyncBeacon();
                if (!result) {
                    LOG_ERROR("Failed to queue sync beacon: %s",
                              result.GetErrorMessage().c_str());
                    break;
                }
                result = TrySendGuardedMessage(slot_type);
            } else {
                result = hardware_->setState(radio::RadioState::kReceive);
                if (!result) {
                    LOG_WARNING(
                        "Failed to set radio to receive for sync beacon: %s",
                        result.GetErrorMessage().c_str());
                }
                in_subslotted_slot_ = true;
                result = TrySendSubslottedMessage(
                    slot_type, config_.getSyncBeaconSubslotConfig(),
                    node_address_);
            }
            if (!result) {
                LOG_ERROR("Failed to send sync beacon: %s",
                          result.GetErrorMessage().c_str());
            }
            break;
        }

        case SlotAllocation::SlotType::DISCOVERY_RX: {
            result = hardware_->setState(radio::RadioState::kReceive);
            if (!result) {
                LOG_ERROR("Failed to set radio to receive for discovery: %s",
                          result.GetErrorMessage().c_str());
            }
            in_subslotted_slot_ = true;

            // Fallback TX: join responses are queued as DISCOVERY_TX but the
            // joining node has no DISCOVERY_TX slot yet.
            uint16_t identifier = node_address_;
            if (config_.getDiscoverySubslotConfig().strategy ==
                lora_mesh::SubslotAssignment::RANDOM) {
                identifier = static_cast<uint16_t>(GetRTOS().GetRandom());
            }
            result = TrySendSubslottedMessage(
                SlotAllocation::SlotType::DISCOVERY_TX,
                config_.getDiscoverySubslotConfig(), identifier);
            if (!result) {
                LOG_ERROR("Failed to send discovery fallback TX: %s",
                          result.GetErrorMessage().c_str());
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

            // Calculate sleep duration (time until next slot minus wake-up guard)
            // The guard ensures the MCU wakes before the slot boundary for
            // callback execution, radio transition, and peripheral init
            uint32_t slot_duration = superframe_service_->GetSlotDuration();
            uint32_t wake_guard = config_.getWakeUpGuardTime();
            ctx.sleep_duration_ms =
                (slot_duration > wake_guard) ? (slot_duration - wake_guard) : 0;

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

            // Sleep the MCU until the next slot when power management is active
            if (prepare_sleep_callback_ && ctx.sleep_duration_ms > 0) {
                GetRTOS().LightSleep(ctx.sleep_duration_ms);
            }

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