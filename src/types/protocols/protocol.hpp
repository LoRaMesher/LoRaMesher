/**
 * @file src/protocols/protocol.hpp
 * @brief Definition of base Protocol class
 */

#pragma once

#include "config/system_config.hpp"
#include "types/error_codes/result.hpp"
#include "types/hardware/i_hardware_manager.hpp"
#include "types/messages/base_message.hpp"

namespace loramesher {
namespace protocols {

/**
* @brief Enumeration of supported protocol types
*/
enum class ProtocolType {
    kPingPong,       ///< Simple ping-pong protocol
    kLoraMesh,       ///< LoRa mesh networking protocol
    kCustomProtocol  ///< User-defined custom protocol
};

/**
* @brief Abstract base class for all communication protocols
* 
* This class defines the interface that all protocol implementations must follow,
* providing common functionality for message handling in LoRa communications.
*/
class Protocol {
   public:
    /**
     * @brief Callback type for received messages
     */
    using MessageReceivedCallback = std::function<void(const BaseMessage&)>;

    /**
    * @brief Virtual destructor to ensure proper cleanup in derived classes
    */
    virtual ~Protocol() = default;

    /**
    * @brief Initialize the protocol
    * 
    * Sets up any required resources and prepares the protocol for operation.
    * 
    * @param hardware Shared pointer to the hardware manager for radio access
    * @param node_address The address of this node
    * @return Result Success if initialization was successful, error details otherwise
    */
    virtual Result Init(std::shared_ptr<hardware::IHardwareManager> hardware,
                        AddressType node_address) = 0;

    /**
    * @brief Start the protocol operation
    *
    * @return Result Success if started successfully, error details otherwise
    */
    virtual Result Start() = 0;

    /**
    * @brief Stop the protocol operation
    *
    * @return Result Success if stopped successfully, error details otherwise
    */
    virtual Result Stop() = 0;

    /**
    * @brief Send a message using this protocol
    * 
    * @param message The message to be sent
    * @return Result Success if message was sent successfully, error details otherwise
    */
    virtual Result SendMessage(const BaseMessage& message) = 0;

    /**
    * @brief Get the type of this protocol
    * 
    * @return ProtocolType The protocol type identifier
    */
    ProtocolType GetProtocolType() const { return protocol_type_; }

    /**
    * @brief Get the node address
    * 
    * @return AddressType The address of this node
    */
    AddressType GetNodeAddress() const { return node_address_; }

    /**
     * @brief Set received message callback
     */
    void SetMessageReceivedCallback(MessageReceivedCallback callback) {
        message_received_callback_ = callback;
    }

   protected:
    /**
    * @brief Protected constructor to prevent direct instantiation
    * 
    * @param type The type of protocol being created
    */
    explicit Protocol(ProtocolType type)
        : protocol_type_(type), hardware_(nullptr), node_address_(0) {}

    /**
    * @brief The type of this protocol instance
    */
    const ProtocolType protocol_type_;

    /**
    * @brief Pointer to the hardware manager for radio communication
    */
    std::shared_ptr<hardware::IHardwareManager> hardware_;

    /**
    * @brief The address of this node in the network
    */
    AddressType node_address_;

    /**
     * @brief Callback for received messages
     */
    MessageReceivedCallback message_received_callback_;
};

}  // namespace protocols
}  // namespace loramesher