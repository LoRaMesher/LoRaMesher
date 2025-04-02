/**
 * @file loramesher.cpp
 * @brief Implementation of the LoraMesher library
 */
#include "loramesher.hpp"
#include "os/rtos.hpp"

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
    protocols::ProtocolType protocol_type = protocol_config.getProtocolType();

    // Get or generate node address
    AddressType configured_address = protocol_config.getNodeAddress();
    if (configured_address == 0) {
        // Auto-generate address if not specified
        // In a real implementation, this might use a MAC address or other unique identifier
        node_address_ = static_cast<AddressType>(
            os::RTOS::instance().getTickCount() & 0xFFFF);
        if (node_address_ == 0)
            node_address_ = 1;  // Avoid address 0
    } else {
        node_address_ = configured_address;
    }

    LOG_INFO("Node address set to 0x%04X", node_address_);

    // Create the active protocol - passing the shared hardware manager
    active_protocol_ = protocol_manager_->CreateProtocol(
        protocol_type, hardware_manager_, node_address_);
    if (!active_protocol_) {
        return Result(LoraMesherErrorCode::kConfigurationError,
                      "Failed to create protocol of type " +
                          std::to_string(static_cast<int>(protocol_type)));
    }

    LOG_INFO("Protocol initialized successfully");
    return Result::Success();
}

Result LoraMesher::Start() {
    if (is_running_) {
        LOG_WARNING("LoraMesher already running");
        return Result::Success();
    }

    LOG_INFO("Starting LoraMesher");

    Result init_result = Initialize();
    if (!init_result) {
        return init_result;
    }

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

void LoraMesher::SetMessageReceivedCallback(MessageReceivedCallback callback) {
    message_callback_ = callback;
}

protocols::ProtocolType LoraMesher::GetActiveProtocolType() const {
    if (!active_protocol_) {
        // Return a default if no protocol is active
        return protocols::ProtocolType::kPingPong;
    }
    return active_protocol_->GetProtocolType();
}

std::shared_ptr<protocols::PingPongProtocol> LoraMesher::GetPingPongProtocol() {
    if (!active_protocol_ || active_protocol_->GetProtocolType() !=
                                 protocols::ProtocolType::kPingPong) {
        return nullptr;
    }

    return std::static_pointer_cast<protocols::PingPongProtocol>(
        active_protocol_);
}

void LoraMesher::OnRadioEvent(std::unique_ptr<radio::RadioEvent> event) {
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