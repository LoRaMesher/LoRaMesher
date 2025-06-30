/**
 * @file i_network_discovery_service.hpp
 * @brief Interface for network discovery functionality
 */

#pragma once

#include "types/error_codes/result.hpp"
#include "types/messages/base_message.hpp"

namespace loramesher {
namespace protocols {

/**
 * @brief Interface for network discovery service
 * 
 * Handles discovering existing networks and creating new ones
 */
class INetworkDiscoveryService {
   public:
    virtual ~INetworkDiscoveryService() = default;

    /**
     * @brief Start the network discovery process
     * 
     * Listens for existing networks for a specified timeout period.
     * If no network is found, triggers network creation.
     * 
     * @return Result Success if discovery completed, error details otherwise
     */
    virtual Result StartDiscovery() = 0;

    /**
     * @brief Check if a network was found during discovery
     * 
     * @return bool True if network was found, false otherwise
     */
    virtual bool IsNetworkFound() const = 0;

    /**
     * @brief Check if this node created a new network
     * 
     * @return bool True if this node created a network, false otherwise
     */
    virtual bool IsNetworkCreator() const = 0;

    /**
     * @brief Process a message received during discovery
     * 
     * @param message The received message
     * @return Result Success if processed correctly, error details otherwise
     */
    virtual Result ProcessReceivedMessage(const BaseMessage& message) = 0;

    /**
     * @brief Set the discovery timeout
     * 
     * @param timeout_ms Timeout in milliseconds
     */
    virtual void SetDiscoveryTimeout(uint32_t timeout_ms) = 0;

    /**
     * @brief Get the network manager address
     * 
     * @return AddressType Address of the network manager, or 0 if not found
     */
    virtual AddressType GetNetworkManagerAddress() const = 0;
};

}  // namespace protocols
}  // namespace loramesher