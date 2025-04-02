/**
 * @file protocol_manager.cpp
 * @brief Implementation of the protocol manager class
 */

#include "protocols/protocol_manager.hpp"
#include "protocols/ping_pong_protocol.hpp"

namespace loramesher {
namespace protocols {

std::unique_ptr<ProtocolManager> ProtocolManager::Create() {
    return std::make_unique<ProtocolManager>();
}

std::shared_ptr<Protocol> ProtocolManager::CreateProtocol(
    ProtocolType type, std::shared_ptr<hardware::IHardwareManager> hardware,
    AddressType node_address) {
    // Check if protocol already exists
    auto it = protocols_.find(type);
    if (it != protocols_.end()) {
        return it->second;
    }

    // Create a new protocol based on type
    std::shared_ptr<Protocol> protocol = nullptr;

    switch (type) {
        case ProtocolType::kPingPong:
            protocol = std::make_shared<PingPongProtocol>();
            break;
        // case ProtocolType::kLoraMesh:
        //     protocol = std::make_shared<LoraMeshProtocol>();
        //     break;
        // case ProtocolType::kCustomProtocol:
        //     protocol = std::make_shared<CustomProtocol>();
        //     break;
        default:
            return nullptr;
    }

    // Initialize the protocol
    if (protocol) {
        Result init_result = protocol->Init(hardware, node_address);
        if (!init_result) {
            LOG_ERROR("Failed to initialize protocol: %s",
                      init_result.GetErrorMessage().c_str());
            return nullptr;
        }

        // Store the protocol if created and initialized successfully
        protocols_[type] = protocol;
    }

    return protocol;
}

std::shared_ptr<Protocol> ProtocolManager::GetProtocol(ProtocolType type) {
    auto it = protocols_.find(type);
    if (it != protocols_.end()) {
        return it->second;
    }
    return nullptr;
}

Result ProtocolManager::InitAllProtocols(
    std::shared_ptr<hardware::IHardwareManager> hardware,
    AddressType node_address) {
    Result result = Result::Success();

    for (auto& [type, protocol] : protocols_) {
        Result init_result = protocol->Init(hardware, node_address);
        if (!init_result) {
            // Add error information with protocol type
            std::string error_msg = "Failed to initialize protocol type " +
                                    std::to_string(static_cast<int>(type)) +
                                    ": " + init_result.GetErrorMessage();

            result.AddError(init_result.getErrorCode(), error_msg);
        }
    }

    return result;
}

Result ProtocolManager::StartAllProtocols() {
    Result result = Result::Success();

    for (auto& [type, protocol] : protocols_) {
        Result start_result = protocol->Start();
        if (!start_result) {
            // Add error information with protocol type
            std::string error_msg = "Failed to start protocol type " +
                                    std::to_string(static_cast<int>(type)) +
                                    ": " + start_result.GetErrorMessage();

            result.AddError(start_result.getErrorCode(), error_msg);
        }
    }

    return result;
}

Result ProtocolManager::StopAllProtocols() {
    Result result = Result::Success();

    for (auto& [type, protocol] : protocols_) {
        Result stop_result = protocol->Stop();
        if (!stop_result) {
            // Add error information with protocol type
            std::string error_msg = "Failed to stop protocol type " +
                                    std::to_string(static_cast<int>(type)) +
                                    ": " + stop_result.GetErrorMessage();

            result.AddError(stop_result.getErrorCode(), error_msg);
        }
    }

    return result;
}

}  // namespace protocols
}  // namespace loramesher