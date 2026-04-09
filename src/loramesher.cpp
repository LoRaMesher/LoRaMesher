/**
 * @file loramesher.cpp
 * @brief Implementation of the LoraMesher library
 */
#include "loramesher.hpp"

#include "hardware/hal_factory.hpp"
#include "os/os_port.hpp"
#include "utils/address_generator.hpp"

namespace loramesher {

LoraMesher::LoraMesher(const Config& config) : config_(config) {
    Result init_result = Initialize();
    if (!init_result) {
        LOG_ERROR("LoraMesher initialization failed: %s",
                  init_result.GetErrorMessage().c_str());
    }
}

LoraMesher::~LoraMesher() {
    Stop();
}

Result LoraMesher::Initialize() {
    if (is_initialized_) {
        return Result::Success();
    }

    LOG_INFO("Initializing LoraMesher");

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

    // Create hardware manager
    auto concrete_hw_manager = std::make_shared<hardware::HardwareManager>(
        config_.getPinConfig(), config_.getRadioConfig());
    hardware_manager_ = std::static_pointer_cast<hardware::IHardwareManager>(
        concrete_hw_manager);

    // Get configuration and resolve node address
    ProtocolConfig protocol_config = config_.getProtocolConfig();
    node_address_ = ResolveNodeAddress(protocol_config);

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

    if (!is_initialized_) {
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

    auto mesh_protocol = GetLoRaMeshProtocol();
    if (mesh_protocol) {
        return mesh_protocol->SendData(destination, data);
    }

    // Fallback for non-mesh protocols (e.g., PingPong)
    auto message_opt = BaseMessage::Create(destination, node_address_,
                                           MessageType::DATA, data);

    if (!message_opt) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Failed to create data message");
    }

    return SendMessage(*message_opt);
}

Result LoraMesher::SendBroadcast(std::span<const uint8_t> data) {
    if (!is_running_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "LoraMesher not running");
    }

    auto mesh_protocol = GetLoRaMeshProtocol();
    if (!mesh_protocol) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "LoRaMesh protocol not active");
    }

    return mesh_protocol->SendBroadcast(data);
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

void LoraMesher::SetNodeCapabilities(uint8_t capabilities) {
    auto mesh_protocol = GetLoRaMeshProtocol();
    if (mesh_protocol) {
        mesh_protocol->SetNodeCapabilities(capabilities);
    }
}

uint8_t LoraMesher::GetNodeCapabilities() const {
    auto mesh_protocol = GetLoRaMeshProtocol();
    if (mesh_protocol) {
        return mesh_protocol->GetLocalNodeCapabilities();
    }
    return 0;
}

uint8_t LoraMesher::GetNodeCapabilities(AddressType node_address) const {
    auto mesh_protocol = GetLoRaMeshProtocol();
    if (mesh_protocol) {
        return mesh_protocol->GetNodeCapabilities(node_address);
    }
    return 0;
}

std::optional<RouteEntry> LoraMesher::GetClosestNodeByCapability(
    uint8_t capability) const {
    auto mesh_protocol = GetLoRaMeshProtocol();
    if (!mesh_protocol) {
        return std::nullopt;
    }

    const auto network_nodes = mesh_protocol->GetNetworkNodesCopy();
    uint32_t now = GetRTOS().getTickCount();

    std::optional<RouteEntry> best;
    uint16_t best_cost = UINT16_MAX;

    for (const auto& node : network_nodes) {
        if (!node.is_active) {
            continue;
        }
        if ((node.routing_entry.capabilities & capability) == 0) {
            continue;
        }

        uint16_t cost =
            types::protocols::lora_mesh::NetworkNodeRoute::CalculateRouteCost(
                node.routing_entry.hop_count, node.GetLinkQuality());

        if (cost < best_cost) {
            best_cost = cost;
            RouteEntry entry;
            entry.destination = node.routing_entry.destination;
            entry.next_hop = node.next_hop;
            entry.hop_count = node.routing_entry.hop_count;
            entry.link_quality = node.GetLinkQuality();
            entry.last_seen_ms = now - node.last_seen;
            entry.is_valid = node.is_active;
            entry.capabilities = node.routing_entry.capabilities;
            entry.is_network_manager = node.is_network_manager;
            best = entry;
        }
    }

    return best;
}

std::optional<RouteEntry> LoraMesher::GetClosestGateway() const {
    return GetClosestNodeByCapability(NodeCapabilities::GATEWAY);
}

uint32_t LoraMesher::GetTimeUntilNextDataSlot(uint32_t guard_time_ms) const {
    auto protocol = GetLoRaMeshProtocol();
    if (!protocol)
        return 0;
    return protocol->GetTimeUntilNextDataSlot(guard_time_ms);
}

uint8_t LoraMesher::GetDataSlotsPerSuperframe() const {
    auto protocol = GetLoRaMeshProtocol();
    if (!protocol)
        return 0;
    return protocol->GetDataSlotsPerSuperframe();
}

size_t LoraMesher::GetTxQueueSize() const {
    auto protocol = GetLoRaMeshProtocol();
    if (!protocol)
        return 0;
    return protocol->GetTxQueueSize();
}

size_t LoraMesher::GetRxQueueSize() const {
    auto protocol = GetLoRaMeshProtocol();
    if (!protocol)
        return 0;
    return protocol->GetRxQueueSize();
}

std::vector<RouteEntry> LoraMesher::GetRoutingTable() const {
    std::vector<RouteEntry> routes;

    auto mesh_protocol = GetLoRaMeshProtocol();
    if (!mesh_protocol) {
        LOG_WARNING("LoRaMesh protocol not available for routing table");
        return routes;
    }

    const auto network_nodes = mesh_protocol->GetNetworkNodesCopy();
    uint32_t now = GetRTOS().getTickCount();
    routes.reserve(network_nodes.size());

    for (const auto& node : network_nodes) {
        RouteEntry entry;
        entry.destination = node.routing_entry.destination;
        entry.next_hop = node.next_hop;
        entry.hop_count = node.routing_entry.hop_count;
        entry.link_quality = node.GetLinkQuality();
        entry.last_seen_ms = now - node.last_seen;
        entry.is_valid = node.is_active;
        entry.capabilities = node.routing_entry.capabilities;
        entry.is_network_manager = node.is_network_manager;
        entry.last_rssi = node.link_stats.last_rssi;
        entry.last_snr = node.link_stats.last_snr;
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

std::span<const types::protocols::lora_mesh::SlotAllocation>
LoraMesher::GetSlotTable() const {
    auto mesh_protocol = GetLoRaMeshProtocol();
    if (!mesh_protocol) {
        LOG_WARNING("LoRaMesh protocol not available for slot table");
        return {};
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

AddressType LoraMesher::GenerateAddressFromHardware() {
    // Create temporary HAL via factory (default PinConfig; SPI not needed here)
    auto hal = hal::HalFactory::createHal(PinConfig{});
    if (!hal) {
        LOG_WARNING("Failed to create HAL for address generation");
        return 0;
    }

    uint8_t hardware_id[6] = {0};
    if (!hal->GetHardwareUniqueId(hardware_id, sizeof(hardware_id))) {
        LOG_WARNING("Hardware ID not available");
        return 0;
    }

    utils::AddressGenerator::Config addr_config{};
    addr_config.use_hardware_id = true;
    addr_config.avoid_reserved_addresses = true;

    return utils::AddressGenerator::GenerateFromHardwareId(
        hardware_id, sizeof(hardware_id), addr_config);
}

AddressType LoraMesher::ResolveNodeAddress(ProtocolConfig& protocol_config) {
    AddressType configured_address = protocol_config.getNodeAddress();

    // Use configured address if provided
    if (configured_address != 0) {
        LOG_INFO("Using configured node address 0x%04X", configured_address);
        return configured_address;
    }

    LOG_INFO("No address configured, auto-generating...");
    AddressType generated_address = 0;

    // Try hardware-based generation if enabled
    if (config_.getAutoAddressFromHardware()) {
        generated_address = GenerateAddressFromHardware();
        if (generated_address != 0) {
            LOG_INFO("Generated node address 0x%04X from hardware",
                     generated_address);
        }
    }

    // Fallback generation if hardware failed or disabled
    if (generated_address == 0) {
        LOG_WARNING("Using fallback generation");
        utils::AddressGenerator::Config addr_config{};
        addr_config.avoid_reserved_addresses = true;
        generated_address =
            utils::AddressGenerator::GenerateFallback(addr_config);
        LOG_INFO("Generated fallback node address 0x%04X", generated_address);
    }

    // Final safety - should never happen
    if (generated_address == 0) {
        LOG_ERROR("Address generation failed completely, using 0x0001");
        generated_address = 0x0001;
    }

    // Update config with resolved address
    protocol_config.setNodeAddress(generated_address);
    return generated_address;
}

}  // namespace loramesher