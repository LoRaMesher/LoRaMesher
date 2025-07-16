/**
 * @file lora_mesh_protocol.cpp
 * @brief Refactored implementation of LoRaMesh protocol using services
 */

#include "protocols/lora_mesh_protocol.hpp"
#include "lora_mesh_protocol.hpp"
#include "os/os_port.hpp"
#include "types/messages/message_type.hpp"
#include "types/radio/radio_event.hpp"

namespace {
using namespace loramesher::types::protocols::lora_mesh;
}  // namespace

namespace loramesher {
namespace protocols {

LoRaMeshProtocol::LoRaMeshProtocol()
    : Protocol(ProtocolType::kLoraMesh),
      protocol_task_handle_(nullptr),
      radio_event_queue_(nullptr) {}

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

    // Clear hardware callback to prevent use-after-free
    if (hardware_) {
        hardware_->setActionReceive(nullptr);
    }

    // Clean up radio event queue
    if (radio_event_queue_) {
        DrainRadioEventQueue();
        GetRTOS().DeleteQueue(radio_event_queue_);
        radio_event_queue_ = nullptr;
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

    // Store hardware reference and node address
    hardware_ = hardware;
    node_address_ = node_address;

    // Create message queue service
    message_queue_service_ = std::make_shared<lora_mesh::MessageQueueService>(
        service_config_.message_queue_size);

    // Create superframe service
    superframe_service_ = std::make_shared<lora_mesh::SuperframeService>();
    superframe_service_->SetNodeAddress(node_address);

    // Create network service
    network_service_ = std::make_shared<lora_mesh::NetworkService>(
        node_address, message_queue_service_, superframe_service_);

    // Initialize radio event queue
    radio_event_queue_ =
        GetRTOS().CreateQueue(RADIO_QUEUE_SIZE, sizeof(radio::RadioEvent*));
    if (!radio_event_queue_) {
        return Result(LoraMesherErrorCode::kConfigurationError,
                      "Failed to create radio event queue");
    }

    // Set up hardware radio callback to send events to NetworkService
    Result hw_result = hardware_->setActionReceive(
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
        });

    if (!hw_result) {
        GetRTOS().DeleteQueue(radio_event_queue_);
        radio_event_queue_ = nullptr;
        return hw_result;
    }

    // Set up callbacks from services

    // Superframe callback for slot transitions
    superframe_service_->SetSuperframeCallback(
        [this](uint16_t slot, bool new_superframe) {
            OnSlotTransition(slot, new_superframe);
        });

    // Network service route update callback
    network_service_->SetRouteUpdateCallback(
        [this](bool updated, AddressType dest, AddressType next_hop,
               uint8_t hops) {
            LOG_DEBUG("Route %s: dest=0x%04X via=0x%04X hops=%d",
                      updated ? "updated" : "removed", dest, next_hop, hops);
            OnNetworkTopologyChange(updated, dest, next_hop, hops);
        });

    // Create main protocol task
    bool task_created = GetRTOS().CreateTask(
        ProtocolTaskFunction, "LoRaMeshMain", PROTOCOL_TASK_STACK_SIZE, this,
        TASK_PRIORITY, &protocol_task_handle_);

    if (!task_created) {
        GetRTOS().DeleteQueue(radio_event_queue_);
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

    // TODO: THIS IS NEEDED?
    // Update superframe configuration
    // uint32_t slot_duration_ms =
    //     hardware_->getTimeOnAir(config.getMaxPacketSize()) +
    //     100;  // 100ms guard time

    // Result result =
    //     superframe_service_->UpdateSlotDuration(slot_duration_ms, false);

    // if (!result) {
    //     LOG_ERROR("Failed to update superframe slot duration: %s",
    //               result.GetErrorMessage().c_str());
    //     return result;
    // }

    // Configure network service
    lora_mesh::INetworkService::NetworkConfig net_config =
        service_config_.network_config;
    net_config.node_address = config.getNodeAddress();

    Result result = network_service_->Configure(net_config);
    if (!result) {
        LOG_ERROR("Failed to configure network service: %s",
                  result.GetErrorMessage().c_str());
        return result;
    }

    // Configure message queue size
    message_queue_service_->SetMaxQueueSize(service_config_.message_queue_size);

    return Result::Success();
}

Result LoRaMeshProtocol::Start() {
    if (!hardware_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Hardware not initialized");
    }

    if (!radio_event_queue_) {
        radio_event_queue_ =
            GetRTOS().CreateQueue(RADIO_QUEUE_SIZE, sizeof(radio::RadioEvent*));
        if (!radio_event_queue_) {
            LOG_ERROR("Failed to create radio event queue");
            return Result(LoraMesherErrorCode::kConfigurationError,
                          "Failed to create radio event queue");
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

    // Clear hardware callback to prevent use-after-free
    if (hardware_) {
        hardware_->setActionReceive(nullptr);
    }

    // Clean up queue
    if (radio_event_queue_) {
        DrainRadioEventQueue();
        GetRTOS().DeleteQueue(radio_event_queue_);
        radio_event_queue_ = nullptr;
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
        case MessageType::DATA_MSG:
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

void LoRaMeshProtocol::SetRouteUpdateCallback(
    lora_mesh::INetworkService::RouteUpdateCallback callback) {
    network_service_->SetRouteUpdateCallback(callback);
}

const std::vector<NetworkNodeRoute>& LoRaMeshProtocol::GetNetworkNodes() const {
    return network_service_->GetNetworkNodes();
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
        // Process any queued radio events
        protocol->ProcessRadioEvents();

        // Check current state and perform state-specific actions
        // Add safety check to prevent use-after-free
        if (!protocol->network_service_) {
            LOG_DEBUG(
                "Network service no longer available, exiting protocol task");
            break;
        }
        auto state = protocol->network_service_->GetState();

        switch (state) {
            case lora_mesh::INetworkService::ProtocolState::DISCOVERY:
                // Discovery is handled by NetworkService
                result = protocol->network_service_->PerformDiscovery(
                    protocol->GetDiscoveryTimeout());
                if (!result) {
                    LOG_ERROR("Discovery failed: %s",
                              result.GetErrorMessage().c_str());
                }
                break;

            case lora_mesh::INetworkService::ProtocolState::JOINING:
                // Joining is handled by NetworkService
                result = protocol->network_service_->PerformJoining(
                    protocol->GetJoinTimeout());
                if (!result) {
                    LOG_ERROR("Discovery failed: %s",
                              result.GetErrorMessage().c_str());
                }
                break;

            case lora_mesh::INetworkService::ProtocolState::NORMAL_OPERATION:
            case lora_mesh::INetworkService::ProtocolState::NETWORK_MANAGER:
                // Normal operation - messages are sent based on slot schedule
                break;

            case lora_mesh::INetworkService::ProtocolState::FAULT_RECOVERY:
                // Fault recovery - may need to restart discovery
                LOG_WARNING("Protocol in fault recovery state");
                result = protocol->StartDiscovery();
                if (!result) {
                    LOG_ERROR("Failed to restart discovery: %s",
                              result.GetErrorMessage().c_str());
                }
                break;

            default:
                break;
        }

        // Yield to other tasks with shorter delay for faster shutdown
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
                    int32_t reception_timestamp = event->getTimestamp();
                    network_service_->ProcessReceivedMessage(
                        *message, reception_timestamp);
                } else if (event->getType() ==
                           radio::RadioEventType::kTransmitted) {
                    // TODO: Handle transmitted events (e.g., update transmission statistics)
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
    // Get current slot type from allocation table
    SlotAllocation::SlotType slot_type = SlotAllocation::SlotType::SLEEP;

    for (const auto& allocation : GetSlotTable()) {
        if (allocation.slot_number == current_slot) {
            slot_type = allocation.type;
            break;
        }
    }

    LOG_DEBUG("Slot %d transition: type=%s%s", current_slot,
              slot_utils::SlotTypeToString(slot_type).c_str(),
              new_superframe ? " (new superframe)" : "");

    // Handle new superframe
    if (new_superframe) {
        // Schedule routing message expectations for link quality tracking
        network_service_->ScheduleRoutingMessageExpectations();
    }

    // Process messages based on slot type
    ProcessSlotMessages(slot_type);
}

void LoRaMeshProtocol::OnStateChange(
    lora_mesh::INetworkService::ProtocolState new_state) {

    LOG_INFO("Protocol state changed to %d", static_cast<int>(new_state));

    // Handle state-specific initialization
    switch (new_state) {
        case lora_mesh::INetworkService::ProtocolState::NETWORK_MANAGER:
        case lora_mesh::INetworkService::ProtocolState::NORMAL_OPERATION:
            // Update slot table when entering operational states
            // Note: NetworkService should call this internally
            break;

        default:
            break;
    }
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

    switch (slot_type) {
        case SlotAllocation::SlotType::TX:
        case SlotAllocation::SlotType::CONTROL_TX: {
            // Apply guard time delay for TX slots to allow RX nodes to prepare
            uint32_t guard_time_ms = config_.getGuardTime();
            if (guard_time_ms > 0) {
                LOG_DEBUG("Applying guard time delay: %u ms", guard_time_ms);
                GetRTOS().delay(guard_time_ms);
            }
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
                    LOG_DEBUG("Sent control message type %d from state %d",
                              static_cast<int>(message->GetType()),
                              static_cast<int>(state));
                }
            } else {
                LOG_DEBUG("No control message to send in state %d",
                          static_cast<int>(state));
            }
            break;
        }
        case SlotAllocation::SlotType::DISCOVERY_TX: {
            // Apply guard time delay for discovery transmission
            uint32_t guard_time_ms = config_.getGuardTime();
            if (guard_time_ms > 0) {
                LOG_DEBUG("Applying guard time delay for discovery: %u ms",
                          guard_time_ms);
                GetRTOS().delay(guard_time_ms);
            }

            // Get next message from queue
            auto message =
                message_queue_service_->ExtractMessageOfType(slot_type);
            if (message) {
                // Send via hardware
                result = hardware_->SendMessage(*message);
                if (!result) {
                    LOG_ERROR("Failed to send message: %s",
                              result.GetErrorMessage().c_str());
                }
            } else {
                if (slot_type == SlotAllocation::SlotType::CONTROL_TX) {
                    // Create a new routing message with broadcast destination
                    auto routing_message =
                        network_service_->CreateRoutingTableMessage(
                            lora_mesh::INetworkService::kBroadcastAddress);
                    if (!routing_message) {
                        LOG_ERROR("Failed to create routing message");
                        return;
                    }
                    result = hardware_->SendMessage(*routing_message);
                }
            }
            break;
        }

        case SlotAllocation::SlotType::SYNC_BEACON_TX: {
            // Apply guard time delay for sync beacon transmission
            uint32_t guard_time_ms = config_.getGuardTime();
            if (guard_time_ms > 0) {
                LOG_DEBUG("Applying guard time delay for sync beacon: %u ms",
                          guard_time_ms);
                GetRTOS().delay(guard_time_ms);
            }

            // Handle sync beacon transmission based on network role and hop distance
            auto state = network_service_->GetState();

            if (state ==
                lora_mesh::INetworkService::ProtocolState::NETWORK_MANAGER) {
                // Apply pending join requests at superframe boundary (before sync beacon)
                result = network_service_->ApplyPendingJoin();
                if (!result) {
                    LOG_ERROR("Failed to apply pending join: %s",
                              result.GetErrorMessage().c_str());
                    // Continue with sync beacon transmission despite join application failure
                }

                // Network Manager: First queue the sync beacon, then extract and send it
                result = network_service_->SendSyncBeacon();
                if (!result) {
                    LOG_ERROR("Failed to queue sync beacon: %s",
                              result.GetErrorMessage().c_str());
                    break;
                }

                // Extract the queued sync beacon and send it
                auto message =
                    message_queue_service_->ExtractMessageOfType(slot_type);
                if (message) {
                    // TODO: Send using this -> 10.1.3 Collision Mitigation for Same-Hop Forwarders
                    result = hardware_->SendMessage(*message);
                    if (!result) {
                        LOG_ERROR("Failed to send sync beacon: %s",
                                  result.GetErrorMessage().c_str());
                    } else {
                        LOG_DEBUG("Network Manager sent sync beacon");
                    }
                } else {
                    LOG_ERROR("Failed to extract queued sync beacon");
                }
            } else {
                // Regular nodes: Check queue for sync beacon to forward
                auto message =
                    message_queue_service_->ExtractMessageOfType(slot_type);
                if (message) {
                    // Forward the sync beacon
                    result = hardware_->SendMessage(*message);
                    if (!result) {
                        LOG_ERROR("Failed to forward sync beacon: %s",
                                  result.GetErrorMessage().c_str());
                    } else {
                        LOG_DEBUG("Regular node forwarded sync beacon");
                    }
                } else {
                    LOG_DEBUG("No sync beacon to forward for regular node");
                }
            }
            break;
        }

        case SlotAllocation::SlotType::DISCOVERY_RX: {
            // Check if there are any discovery messages to transmit first
            auto discovery_message =
                message_queue_service_->ExtractMessageOfType(
                    SlotAllocation::SlotType::DISCOVERY_TX);
            if (discovery_message) {
                // Apply guard time delay for discovery transmission
                uint32_t guard_time_ms = config_.getGuardTime();
                if (guard_time_ms > 0) {
                    LOG_DEBUG(
                        "Applying guard time delay for discovery in RX slot: "
                        "%u ms",
                        guard_time_ms);
                    GetRTOS().delay(guard_time_ms);
                }

                // TODO: Send using this -> 10.1.3 Collision Mitigation for Same-Hop Forwarders
                // Send discovery message instead of receiving
                result = hardware_->SendMessage(*discovery_message);
                if (!result) {
                    LOG_ERROR("Failed to send discovery message: %s",
                              result.GetErrorMessage().c_str());
                } else {
                    LOG_DEBUG(
                        "Sent discovery message during DISCOVERY_RX slot");
                }
            } else {
                // No discovery messages to send, set radio to receive mode
                result = hardware_->setState(radio::RadioState::kReceive);
                if (!result) {
                    LOG_ERROR("Failed to set radio to receive: %s",
                              result.GetErrorMessage().c_str());
                }
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
        default:
            // Set radio to sleep mode
            result = hardware_->setState(radio::RadioState::kSleep);
            if (!result) {
                LOG_ERROR("Failed to set radio to sleep: %s",
                          result.GetErrorMessage().c_str());
            }
            break;
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

Result LoRaMeshProtocol::StartDiscovery() {
    if (!superframe_service_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Superframe service not initialized");
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

Result LoRaMeshProtocol::AddRoutingMessageToQueueService() {
    if (!message_queue_service_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Message queue service not initialized");
    }

    // Search if message_queue_service contains a routing_message
    if (message_queue_service_->HasMessage(
            loramesher::MessageType::ROUTE_TABLE)) {
        return Result::Success();  // Already exists, no need to add
    }

    // Create a new routing message with broadcast destination
    auto routing_message = network_service_->CreateRoutingTableMessage(
        lora_mesh::INetworkService::kBroadcastAddress);
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

}  // namespace protocols
}  // namespace loramesher