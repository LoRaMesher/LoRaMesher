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
      radio_event_queue_(nullptr),
      stop_requested_(false),
      is_paused_(false) {}

LoRaMeshProtocol::~LoRaMeshProtocol() {
    // Note: Stop() not called in destructor to avoid virtual function call during destruction
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

    // Update superframe configuration
    uint32_t slot_duration_ms =
        hardware_->getTimeOnAir(config.getMaxPacketSize()) +
        100;  // 100ms guard time

    Result result =
        superframe_service_->UpdateSlotDuration(slot_duration_ms, false);

    if (!result) {
        LOG_ERROR("Failed to update superframe slot duration: %s",
                  result.GetErrorMessage().c_str());
        return result;
    }

    // Configure network service
    lora_mesh::INetworkService::NetworkConfig net_config =
        service_config_.network_config;
    net_config.node_address = config.getNodeAddress();

    result = network_service_->Configure(net_config);
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

    stop_requested_ = false;
    is_paused_ = false;

    // Resume protocol task
    GetRTOS().ResumeTask(protocol_task_handle_);

    LOG_INFO("LoRaMesh protocol started");
    return Result::Success();
}

Result LoRaMeshProtocol::Stop() {
    stop_requested_ = true;

    // Stop services
    if (superframe_service_) {
        superframe_service_->StopSuperframe();
    }

    // Clean up task
    if (protocol_task_handle_) {
        GetRTOS().DeleteTask(protocol_task_handle_);
        protocol_task_handle_ = nullptr;
    }

    // Clean up queue
    if (radio_event_queue_) {
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
    if (is_paused_) {
        return Result::Success();
    }

    is_paused_ = true;

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
    if (!is_paused_) {
        return Result::Success();
    }

    is_paused_ = false;

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

    LOG_INFO("Protocol task started");

    Result result = Result::Success();

    while (!protocol->stop_requested_ && !rtos.ShouldStopOrPause()) {
        // Process any queued radio events
        protocol->ProcessRadioEvents();

        // If paused, just wait
        if (protocol->is_paused_) {
            rtos.delay(100);
            continue;
        }

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
                // TODO: Optimize this to avoid repeatedly checking state
                result = protocol->AddRoutingMessageToQueueService();
                if (!result) {
                    LOG_ERROR("Failed to add routing message: %s",
                              result.GetErrorMessage().c_str());
                }
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

        // Yield to other tasks
        rtos.YieldTask();
    }

    LOG_INFO("Protocol task ending");
    rtos.DeleteTask(nullptr);
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
                network_service_->ProcessReceivedMessage(*message);
                LOG_DEBUG("Processed radio event with message");
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
        case SlotAllocation::SlotType::CONTROL_TX:
        case SlotAllocation::SlotType::DISCOVERY_TX: {
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

        case SlotAllocation::SlotType::RX:
        case SlotAllocation::SlotType::CONTROL_RX:
        case SlotAllocation::SlotType::DISCOVERY_RX:
            // Set radio to receive mode
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

}  // namespace protocols
}  // namespace loramesher