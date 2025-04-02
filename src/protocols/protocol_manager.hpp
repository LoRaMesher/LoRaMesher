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

#include "types/error_codes/result.hpp"
#include "types/protocols/protocol.hpp"

namespace loramesher {
namespace protocols {

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
      * @brief Get an existing protocol instance by type
      * 
      * @param type The type of protocol to retrieve
      * @return std::shared_ptr<Protocol> Shared pointer to the protocol instance,
      *         or nullptr if no such protocol exists
      */
    std::shared_ptr<Protocol> GetProtocol(ProtocolType type);

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
     * @return Result Succes if all protocols started successfully, detailed error otherwise
     */
    Result StartAllProtocols();

    /**
     * @brief Stop all the protocols
     * 
     * @return Result Succes if all protocols stopped successfully, detailed error otherwiseRe
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