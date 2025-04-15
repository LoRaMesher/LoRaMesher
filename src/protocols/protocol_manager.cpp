/**
 * @file protocol_manager.cpp
 * @brief Implementation of the protocol manager class
 */

#include "protocols/protocol_manager.hpp"
#include "protocols/lora_mesh_protocol.hpp"
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
        case ProtocolType::kLoraMesh:
            protocol = std::make_shared<LoRaMeshProtocol>();
            break;
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

std::shared_ptr<Protocol> ProtocolManager::CreateProtocolWithConfig(
    const ProtocolConfig& config,
    std::shared_ptr<hardware::IHardwareManager> hardware) {

    // Get protocol type from configuration
    ProtocolType type = config.getProtocolType();

    // Check if protocol already exists
    auto it = protocols_.find(type);
    if (it != protocols_.end()) {
        // Protocol exists, configure it
        ConfigureProtocol(type, config);
        return it->second;
    }

    // Create a new protocol based on type
    std::shared_ptr<Protocol> protocol = nullptr;

    switch (type) {
        case ProtocolType::kPingPong: {
            auto ping_pong = std::make_shared<PingPongProtocol>();
            protocol = ping_pong;

            // Initialize first
            Result init_result =
                ping_pong->Init(hardware, config.getNodeAddress());
            if (!init_result) {
                LOG_ERROR("Failed to initialize PingPong protocol: %s",
                          init_result.GetErrorMessage().c_str());
                return nullptr;
            }

            // Apply specific configuration
            // In a production implementation, you would apply the PingPong-specific config here
            break;
        }

        case ProtocolType::kLoraMesh: {
            auto lora_mesh = std::make_shared<LoRaMeshProtocol>();
            protocol = lora_mesh;

            // Initialize first
            Result init_result =
                lora_mesh->Init(hardware, config.getNodeAddress());
            if (!init_result) {
                LOG_ERROR("Failed to initialize LoRaMesh protocol: %s",
                          init_result.GetErrorMessage().c_str());
                return nullptr;
            }

            // Apply specific configuration
            try {
                const auto& lora_config = config.getLoRaMeshConfig();
                Result config_result = lora_mesh->Configure(lora_config);
                if (!config_result) {
                    LOG_ERROR("Failed to configure LoRaMesh protocol: %s",
                              config_result.GetErrorMessage().c_str());
                    return nullptr;
                }
            } catch (const std::bad_cast& e) {
                LOG_ERROR("Invalid configuration type for LoRaMesh protocol");
                return nullptr;
            }
            break;
        }

            // case ProtocolType::kCustomProtocol:
            //     // Similar pattern for custom protocols
            //     break;

        default:
            return nullptr;
    }

    if (protocol) {
        // Store the protocol
        protocols_[type] = protocol;
    }

    return protocol;
}

Result ProtocolManager::ConfigureProtocol(ProtocolType type,
                                          const ProtocolConfig& config) {
    // Get the protocol
    auto it = protocols_.find(type);
    if (it == protocols_.end()) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Protocol not found for configuration");
    }

    // Apply configuration based on protocol type
    switch (type) {
        case ProtocolType::kPingPong: {
            // Cast to PingPong protocol
            auto ping_pong =
                std::dynamic_pointer_cast<PingPongProtocol>(it->second);
            if (!ping_pong) {
                return Result(LoraMesherErrorCode::kInvalidState,
                              "Failed to cast to PingPong protocol");
            }

            // In a production implementation, you would apply the PingPong-specific config here
            // For now, just ensure the node address is updated
            // TODO: Implement PingPong-specific configuration logic
            // if (ping_pong->GetNodeAddress() != config.getNodeAddress() &&
            //     config.getNodeAddress() != 0) {
            //     // Re-init with new address if needed
            //     return ping_pong->Init(ping_pong->hardware_,
            //                            config.getNodeAddress());
            // }

            return Result::Success();
        }

        case ProtocolType::kLoraMesh: {
            // Cast to LoRaMesh protocol
            auto lora_mesh =
                std::dynamic_pointer_cast<LoRaMeshProtocol>(it->second);
            if (!lora_mesh) {
                return Result(LoraMesherErrorCode::kInvalidState,
                              "Failed to cast to LoRaMesh protocol");
            }

            // Apply LoRaMesh configuration
            try {
                const auto& lora_config = config.getLoRaMeshConfig();
                return lora_mesh->Configure(lora_config);
            } catch (const std::bad_cast& e) {
                return Result(
                    LoraMesherErrorCode::kInvalidParameter,
                    "Invalid configuration type for LoRaMesh protocol");
            }
        }

            // case ProtocolType::kCustomProtocol:
            //     // Similar pattern for custom protocols
            //     break;

        default:
            return Result(LoraMesherErrorCode::kInvalidParameter,
                          "Unknown protocol type");
    }
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