/**
 * @file loramesher.cpp
 * @brief Implementation of the LoraMesher library
 */
#include "loramesher.hpp"
#include "os/os_port.hpp"

namespace loramesher {

LoraMesher::LoraMesher(const Config& config) : config_(config) {}

LoraMesher::~LoraMesher() {
    Stop();
}

Result LoraMesher::Initialize() {
    if (is_initialized_) {
        return Result::Success();
    }

    LOG_INFO("Initializing LoraMesher");

    // Create hardware manager as its concrete type
    auto concrete_hw_manager = std::make_shared<hardware::HardwareManager>(
        config_.getPinConfig(), config_.getRadioConfig());

    // Then cast it to the interface type
    hardware_manager_ = std::static_pointer_cast<hardware::IHardwareManager>(
        concrete_hw_manager);

    // Initialize hardware
    Result hw_result = hardware_manager_->Initialize();
    if (!hw_result) {
        LOG_ERROR("Hardware initialization failed: %s",
                  hw_result.GetErrorMessage().c_str());
        return hw_result;
    }

    // Initialize protocol
    Result protocol_result = InitializeProtocol();
    if (!protocol_result) {
        LOG_ERROR("Protocol initialization failed: %s",
                  protocol_result.GetErrorMessage().c_str());
        return protocol_result;
    }

    is_initialized_ = true;
    LOG_INFO("LoraMesher initialized successfully");
    return Result::Success();
}

Result LoraMesher::InitializeProtocol() {
    // Create protocol manager
    protocol_manager_ = protocols::ProtocolManager::Create();
    if (!protocol_manager_) {
        return Result(LoraMesherErrorCode::kConfigurationError,
                      "Failed to create protocol manager");
    }

    // Get configuration for protocol setup
    ProtocolConfig protocol_config = config_.getProtocolConfig();

    // Get or generate node address
    AddressType configured_address = protocol_config.getNodeAddress();
    if (configured_address == 0) {
        // Auto-generate address if not specified
        // TODO: In a real implementation, this might use a MAC address or other unique identifier
        node_address_ = static_cast<AddressType>(
            os::RTOS::instance().getTickCount() & 0xFFFF);
        if (node_address_ == 0)
            node_address_ = 1;  // Avoid address 0

        // Update the protocol configuration with the generated address
        protocol_config.setNodeAddress(node_address_);
    } else {
        node_address_ = configured_address;
    }

    LOG_INFO("Node address set to 0x%04X", node_address_);

    // Create the active protocol using configuration
    active_protocol_ = protocol_manager_->CreateProtocolWithConfig(
        protocol_config, hardware_manager_);

    if (!active_protocol_) {
        return Result(LoraMesherErrorCode::kConfigurationError,
                      "Failed to create protocol with configuration");
    }

    LOG_INFO("Protocol initialized successfully");
    return Result::Success();
}

Result LoraMesher::Start() {
    if (is_running_) {
        LOG_WARNING("LoraMesher already running");
        return Result::Success();
    }

    if(!is_initialized_) {
        Result init_result = Initialize();
        if (!init_result) {
            return init_result;
        }
    }

    LOG_INFO("Starting LoraMesher");

    Result protocol_result = Result::Success();

    // Start protocols
    if (protocol_manager_) {
        Result protocol_result = protocol_manager_->StartAllProtocols();
        if (!protocol_result) {
            LOG_ERROR("Failed to start protocols: %s",
                      protocol_result.GetErrorMessage().c_str());
            protocol_result.MergeErrors(protocol_result);
        }
    }

    is_running_ = true;
    LOG_INFO("LoraMesher started successfully");
    return protocol_result;
}

void LoraMesher::Stop() {
    if (!is_running_) {
        return;
    }

    LOG_INFO("Stopping LoraMesher");

    // Stop components in reverse order of initialization
    if (protocol_manager_) {
        Result protocol_result = protocol_manager_->StopAllProtocols();
        if (!protocol_result) {
            LOG_ERROR("Protocol Stop with errors %s",
                      protocol_result.GetErrorMessage().c_str());
        }
    }

    if (hardware_manager_) {
        hardware_manager_->Stop();
    }

    is_running_ = false;
    LOG_INFO("LoraMesher stopped");
}

Result LoraMesher::SendMessage(const BaseMessage& msg) {
    if (!is_running_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "LoraMesher not running");
    }

    LOG_DEBUG("SendMessage called for message to 0x%04X",
              msg.GetHeader().GetDestination());

    // Send via the active protocol
    if (active_protocol_) {
        Result protocol_result = active_protocol_->SendMessage(msg);
        if (!protocol_result) {
            LOG_ERROR("Protocol failed to send message: %s",
                      protocol_result.GetErrorMessage().c_str());
            return protocol_result;
        }
        return Result::Success();
    } else {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "No active protocol available");
    }
}

AddressType LoraMesher::GetNodeAddress() const {
    return node_address_;
}

// Application Layer Interface Implementation

Result LoraMesher::Send(AddressType destination,
                        const std::vector<uint8_t>& data) {
    if (!is_running_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "LoraMesher not running");
    }

    LOG_DEBUG("Send called for destination 0x%04X with %zu bytes", destination,
              data.size());

    // Create BaseMessage with DATA type
    auto message_opt = BaseMessage::Create(destination, node_address_,
                                           MessageType::DATA, data);

    if (!message_opt) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Failed to create data message");
    }

    // Send via existing SendMessage method
    return SendMessage(*message_opt);
}

void LoraMesher::SetDataCallback(DataReceivedCallback callback) {
    data_callback_ = callback;

    // Forward the callback to the LoRaMesh protocol
    auto mesh_protocol = GetLoRaMeshProtocol();
    if (mesh_protocol) {
        mesh_protocol->SetDataReceivedCallback(callback);
        LOG_DEBUG("Data callback registered with LoRaMesh protocol");
    } else {
        LOG_WARNING("LoRaMesh protocol not available for data callback");
    }
}

std::vector<RouteEntry> LoraMesher::GetRoutingTable() const {
    std::vector<RouteEntry> routes;

    auto mesh_protocol = GetLoRaMeshProtocol();
    if (!mesh_protocol) {
        LOG_WARNING("LoRaMesh protocol not available for routing table");
        return routes;
    }

    const auto& network_nodes = mesh_protocol->GetNetworkNodes();
    routes.reserve(network_nodes.size());

    for (const auto& node : network_nodes) {
        RouteEntry entry;
        entry.destination = node.routing_entry.destination;
        entry.next_hop = node.next_hop;
        entry.hop_count = node.routing_entry.hop_count;
        entry.link_quality = node.GetLinkQuality();
        entry.last_seen_ms = node.last_seen;
        entry.is_valid = node.is_active;
        routes.push_back(entry);
    }

    return routes;
}

NetworkStatus LoraMesher::GetNetworkStatus() const {
    NetworkStatus status{};

    auto mesh_protocol = GetLoRaMeshProtocol();
    if (!mesh_protocol) {
        LOG_WARNING("LoRaMesh protocol not available for network status");
        status.current_state =
            protocols::lora_mesh::INetworkService::ProtocolState::INITIALIZING;
        status.network_manager = 0;
        status.current_slot = 0;
        status.is_synchronized = false;
        status.time_since_last_sync_ms = 0;
        status.connected_nodes = 0;
        return status;
    }

    status.current_state = mesh_protocol->GetState();
    status.network_manager = mesh_protocol->GetNetworkManager();
    status.current_slot = mesh_protocol->GetCurrentSlot();
    status.is_synchronized = mesh_protocol->IsSynchronized();
    status.connected_nodes = mesh_protocol->GetNetworkNodes().size();

    // TODO: Add time_since_last_sync_ms when available in protocol
    status.time_since_last_sync_ms = 0;

    return status;
}

const std::vector<types::protocols::lora_mesh::SlotAllocation>&
LoraMesher::GetSlotTable() const {
    auto mesh_protocol = GetLoRaMeshProtocol();
    if (!mesh_protocol) {
        static const std::vector<types::protocols::lora_mesh::SlotAllocation>
            empty_table;
        LOG_WARNING("LoRaMesh protocol not available for slot table");
        return empty_table;
    }

    return mesh_protocol->GetSlotTable();
}

protocols::ProtocolType LoraMesher::GetActiveProtocolType() const {
    if (!active_protocol_) {
        // Return a default if no protocol is active
        return protocols::ProtocolType::kPingPong;
    }
    return active_protocol_->GetProtocolType();
}

std::shared_ptr<protocols::PingPongProtocol> LoraMesher::GetPingPongProtocol() {
    if (!protocol_manager_) {
        return nullptr;
    }

    return protocol_manager_->GetProtocolAs<protocols::PingPongProtocol>(
        protocols::ProtocolType::kPingPong);
}

std::shared_ptr<protocols::LoRaMeshProtocol> LoraMesher::GetLoRaMeshProtocol() {
    if (!protocol_manager_) {
        return nullptr;
    }

    return protocol_manager_->GetProtocolAs<protocols::LoRaMeshProtocol>(
        protocols::ProtocolType::kLoraMesh);
}

std::shared_ptr<const protocols::LoRaMeshProtocol>
LoraMesher::GetLoRaMeshProtocol() const {
    // Use const_cast to access the protocol manager since the method doesn't modify state
    auto* non_const_this = const_cast<LoraMesher*>(this);
    return non_const_this->GetLoRaMeshProtocol();
}

void LoraMesher::OnRadioEvent(std::unique_ptr<radio::RadioEvent> event) {
    (void)event;
    // Skip if the event is not a received message
    // if (event->getType() != radio::RadioEventType::kReceived) {
    //     LOG_DEBUG("Received non-message radio event: %d",
    //               static_cast<int>(event->getType()));
    //     return;
    // }

    // const BaseMessage* msg = event->getMessage();

    // LOG_DEBUG("Hardware received message from 0x%04X to 0x%04X, type: %d",
    //           msg->GetHeader().source, msg->getBaseHeader().destination,
    //           static_cast<int>(msg->getBaseHeader().type));

    // // Let the protocol handle the message (even if not addressed to us,
    // // as the protocol might need to handle routing or other tasks)
    // if (active_protocol_) {
    //     Result protocol_result =
    //         active_protocol_->ProcessReceivedRadioEvent(std::move(event));
    //     if (!protocol_result) {
    //         LOG_WARNING("Protocol failed to process message: %s",
    //                     protocol_result.GetErrorMessage().c_str());
    //     }
    // }

    // Check if message is for this node
    // const AddressType BROADCAST_ADDRESS = 0xFFFF;
    // const bool is_for_us =
    //     (msg->getBaseHeader().destination == node_address_ ||
    //      msg->getBaseHeader().destination == BROADCAST_ADDRESS);

    // Notify the application layer only if message is addressed to us and callback is registered
    // if (is_for_us && message_callback_) {
    //     LOG_DEBUG("Dispatching message to application callback");
    //     message_callback_(*msg);
    // }
}

}  // namespace loramesher