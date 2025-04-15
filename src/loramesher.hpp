/**
 * @file loramesher.hpp
 * @brief Main LoraMesher library interface
 */
#pragma once

#include <functional>
#include <memory>

#include "config/system_config.hpp"
#include "hardware/hardware_manager.hpp"
#include "protocols/lora_mesh_protocol.hpp"
#include "protocols/ping_pong_protocol.hpp"
#include "protocols/protocol_manager.hpp"
#include "types/configurations/loramesher_configuration.hpp"
#include "types/messages/base_message.hpp"
#include "utils/logger.hpp"

namespace loramesher {

/**
 * @brief Main class of the LoraMesher library
 * 
 * Provides a unified interface for hardware and protocol management,
 * message handling, and network operations.
 */
class LoraMesher {
   public:
    class Builder;  // Forward declaration of Builder

    /**
     * @brief Callback type for received messages
     */
    using MessageReceivedCallback = std::function<void(const BaseMessage&)>;

    /**
     * @brief Destructor
     */
    ~LoraMesher();

    // Non-copyable
    LoraMesher(const LoraMesher&) = delete;
    LoraMesher& operator=(const LoraMesher&) = delete;

    // Moveable
    LoraMesher(LoraMesher&&) noexcept = default;
    LoraMesher& operator=(LoraMesher&&) noexcept = default;

    /**
     * @brief Start the LoraMesher network
     * 
     * Initializes hardware, protocols, and starts all required tasks.
     * 
     * @return Result Success if started successfully, error details otherwise
     */
    [[nodiscard]] Result Start();

    /**
     * @brief Stop the LoraMesher network
     * 
     * Stops all tasks and releases resources.
     */
    void Stop();

    /**
     * @brief Send a message over the network
     * 
     * @param msg The message to send
     * @return Result Success if message was sent, error details otherwise
     */
    [[nodiscard]] Result SendMessage(const BaseMessage& msg);

    /**
     * @brief Get the node's address in the network
     * 
     * @return AddressType The node address
     */
    AddressType GetNodeAddress() const;

    /**
     * @brief Set callback for received messages
     * @warning This callback should be small or transfer the message into another task.
     * 
     * @param callback Function to call when a message is received
     */
    void SetMessageReceivedCallback(MessageReceivedCallback callback);

    /**
     * @brief Get the active protocol type
     * 
     * @return protocols::ProtocolType The active protocol type
     */
    protocols::ProtocolType GetActiveProtocolType() const;

    /**
     * @brief Get the ping-pong protocol interface if active
     * 
     * @return std::shared_ptr<protocols::PingPongProtocol> The protocol instance or nullptr
     */
    std::shared_ptr<protocols::PingPongProtocol> GetPingPongProtocol();

    /**
     * @brief Get the LoRaMesh protocol instance
     * @return std::shared_ptr<protocols::LoRaMeshProtocol> Shared pointer to the LoRaMesh protocol,
     *         or nullptr if not available
     */
    std::shared_ptr<protocols::LoRaMeshProtocol> GetLoRaMeshProtocol();

    /**
     * @brief Get the hardware manager
     * 
     * @return std::shared_ptr<hardware::HardwareManagerInterface> The hardware manager
     */
    std::shared_ptr<hardware::IHardwareManager> GetHardwareManager() const {
        return hardware_manager_;
    }

   private:
    friend class Builder;  // Allow Builder to access private constructor

    /**
     * @brief Private constructor
     * 
     * @param config The configuration
     */
    explicit LoraMesher(const Config& config);

    /**
     * @brief Initialize the system components
     * 
     * @return Result Success if initialization was successful
     */
    [[nodiscard]] Result Initialize();

    /**
     * @brief Create and initialize the protocol
     * 
     * @return Result Success if protocol initialization was successful
     */
    [[nodiscard]] Result InitializeProtocol();

    /**
     * @brief Hardware event callback
     * 
     * @param event The radio event
     */
    void OnRadioEvent(std::unique_ptr<radio::RadioEvent> event);

    // Core components
    std::shared_ptr<hardware::IHardwareManager> hardware_manager_;
    std::unique_ptr<protocols::ProtocolManager> protocol_manager_;
    std::shared_ptr<protocols::Protocol> active_protocol_;

    // Configuration
    Config config_;

    // State
    bool is_initialized_ = false;
    bool is_running_ = false;
    AddressType node_address_ = 0;

    // Callbacks
    MessageReceivedCallback message_callback_ = nullptr;
};

/**
 * @brief Builder class for LoraMesher configuration
 * 
 * Provides a fluent interface for configuring and creating LoraMesher instances.
 */
class LoraMesher::Builder {
   public:
    /**
     * @brief Constructor
     */
    Builder() : config_(Config::CreateDefault()) {}

    /**
     * @brief Set the radio configuration
     * 
     * @param config The radio configuration
     * @return Builder& Reference to this builder for method chaining
     */
    Builder& withRadioConfig(const RadioConfig& config) {
        config_.setRadioConfig(config);
        return *this;
    }

    /**
     * @brief Set the protocol configuration
     * 
     * @param config The protocol configuration
     * @return Builder& Reference to this builder for method chaining
     */
    Builder& withProtocolConfig(const ProtocolConfig& config) {
        config_.setProtocolConfig(config);
        return *this;
    }

    /**
     * @brief Set the pin configuration
     * 
     * @param config The pin configuration
     * @return Builder& Reference to this builder for method chaining
     */
    Builder& withPinConfig(const PinConfig& config) {
        config_.setPinConfig(config);
        return *this;
    }

    /**
     * @brief Set the node address
     * 
     * @param address The node address (0 for auto-assign)
     * @return Builder& Reference to this builder for method chaining
     */
    Builder& withNodeAddress(AddressType address) {
        auto protocol_config = config_.getProtocolConfig();
        protocol_config.setNodeAddress(address);
        config_.setProtocolConfig(protocol_config);
        return *this;
    }

    /**
     * @brief Configure for PingPong protocol
     * 
     * @param config The PingPong protocol configuration
     * @return Builder& Reference to this builder for method chaining
     */
    Builder& withPingPongProtocol(
        const PingPongProtocolConfig& config = PingPongProtocolConfig()) {
        ProtocolConfig protocol_config;
        protocol_config.setPingPongConfig(config);
        config_.setProtocolConfig(protocol_config);
        return *this;
    }

    /**
     * @brief Configure for LoRaMesh protocol
     * 
     * @param config The LoRaMesh protocol configuration
     * @return Builder& Reference to this builder for method chaining
     */
    Builder& withLoRaMeshProtocol(
        const LoRaMeshProtocolConfig& config = LoRaMeshProtocolConfig()) {
        ProtocolConfig protocol_config;
        protocol_config.setLoRaMeshConfig(config);
        config_.setProtocolConfig(protocol_config);
        return *this;
    }

    /**
     * @brief Build the LoraMesher instance
     * 
     * @return std::unique_ptr<LoraMesher> The created instance
     * @throws std::invalid_argument If configuration is invalid
     */
    [[nodiscard]] std::unique_ptr<LoraMesher> Build() {
        if (!config_.IsValid()) {
            throw std::invalid_argument("Invalid configuration: " +
                                        config_.Validate());
        }
        return std::unique_ptr<LoraMesher>(new LoraMesher(config_));
    }

   private:
    Config config_;  ///< The configuration being built
};

}  // namespace loramesher