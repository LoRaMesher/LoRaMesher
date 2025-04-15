/**
 * @file src/protocols/protocol_manager.hpp
 * @brief Manager class for protocol instantiation and management
 * 
 * This file contains the ProtocolManager class which is responsible for
 * creating and managing protocol instances based on their types.
 */

#pragma once

#include <memory>
#include <unordered_map>

#include "types/configurations/protocol_configuration.hpp"
#include "types/error_codes/result.hpp"
#include "types/protocols/protocol.hpp"

namespace loramesher {
namespace protocols {

// Forward declarations
class LoRaMeshProtocol;
class PingPongProtocol;

/**
 * @brief Manager class for protocol instantiation and lifecycle
 * 
 * This class implements the factory pattern to create appropriate protocol
 * instances based on the requested protocol type. It also manages the
 * lifecycle of created protocol instances.
 */
class ProtocolManager {
   public:
    /**
     * @brief Constructor for ProtocolManager
     */
    ProtocolManager() = default;

    /**
     * @brief Destructor for ProtocolManager
     */
    ~ProtocolManager() = default;

    /**
     * @brief Creates a new instance of the ProtocolManager
     * 
     * @return std::unique_ptr<ProtocolManager> A new instance of the ProtocolManager
     */
    static std::unique_ptr<ProtocolManager> Create();

    /**
     * @brief Creates a protocol instance of the specified type
     * 
     * @param type The type of protocol to create
     * @param hardware Shared pointer to the hardware manager for the protocol to use
     * @param node_address The node address for this protocol instance
     * @return std::shared_ptr<Protocol> Shared pointer to the created protocol, 
     *         or nullptr if creation failed
     */
    std::shared_ptr<Protocol> CreateProtocol(
        ProtocolType type, std::shared_ptr<hardware::IHardwareManager> hardware,
        AddressType node_address);

    /**
     * @brief Creates a protocol instance with specific configuration
     * 
     * @param config The protocol configuration to use
     * @param hardware Shared pointer to the hardware manager for the protocol to use
     * @return std::shared_ptr<Protocol> Shared pointer to the created protocol,
     *         or nullptr if creation failed
     */
    std::shared_ptr<Protocol> CreateProtocolWithConfig(
        const ProtocolConfig& config,
        std::shared_ptr<hardware::IHardwareManager> hardware);

    /**
     * @brief Configure an existing protocol with new settings
     * 
     * @param type The type of protocol to configure
     * @param config The configuration to apply
     * @return Result Success if configuration was applied, error details otherwise
     */
    Result ConfigureProtocol(ProtocolType type, const ProtocolConfig& config);

    /**
     * @brief Get an existing protocol instance by type
     * 
     * @param type The type of protocol to retrieve
     * @return std::shared_ptr<Protocol> Shared pointer to the protocol instance,
     *         or nullptr if no such protocol exists
     */
    std::shared_ptr<Protocol> GetProtocol(ProtocolType type);

    /**
     * @brief Get an existing protocol instance as a specific type
     * 
     * @tparam T The protocol type to cast to (e.g., LoRaMeshProtocol)
     * @param type The protocol type enum value
     * @return std::shared_ptr<T> Shared pointer to the protocol instance cast to T,
     *         or nullptr if no such protocol exists or cast fails
     */
    template <typename T>
    std::shared_ptr<T> GetProtocolAs(ProtocolType type) {
        auto protocol = GetProtocol(type);
        if (!protocol) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<T>(protocol);
    }

    /**
     * @brief Initializes all created protocols
     * 
     * @param hardware Shared pointer to the hardware manager for the protocols to use
     * @param node_address The node address
     * @return Result Success if all protocols initialized successfully, detailed error otherwise
     */
    Result InitAllProtocols(
        std::shared_ptr<hardware::IHardwareManager> hardware,
        AddressType node_address);

    /**
    * @brief Start all the protocols
    * 
    * @return Result Success if all protocols started successfully, detailed error otherwise
    */
    Result StartAllProtocols();

    /**
    * @brief Stop all the protocols
    * 
    * @return Result Success if all protocols stopped successfully, detailed error otherwise
    */
    Result StopAllProtocols();

   private:
    /**
     * @brief Map of created protocol instances by type
     */
    std::unordered_map<ProtocolType, std::shared_ptr<Protocol>> protocols_;
};

}  // namespace protocols
}  // namespace loramesher