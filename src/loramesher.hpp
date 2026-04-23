/**
 * @file loramesher.hpp
 * @brief Main LoraMesher library interface
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>

#include "config/system_config.hpp"
#include "hardware/hardware_manager.hpp"
#include "protocols/lora_mesh_protocol.hpp"
#include "protocols/ping_pong_protocol.hpp"
#include "protocols/protocol_manager.hpp"
#include "types/application/application_types.hpp"
#include "types/configurations/loramesher_configuration.hpp"
#include "types/messages/base_message.hpp"
#include "types/node_capabilities.hpp"
#include "utils/compat/span.hpp"
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
     * @brief Send a broadcast message to all nodes in the mesh network
     *
     * The message propagates through the mesh using TTL-based flooding.
     * Each node receives the message once (de-duplicated) and re-broadcasts
     * it to its neighbors until TTL expires.
     *
     * @param data User data payload
     * @return Result Success if message was queued, error details otherwise
     */
    [[nodiscard]] Result SendBroadcast(std::span<const uint8_t> data);

    /**
     * @brief Generate address from hardware ID without full initialization
     *
     * This static method allows users to determine the auto-generated hardware
     * address before building LoraMesher. This is useful for configuring
     * role-based settings (like NodeRole) based on the address.
     *
     * @return AddressType The address that would be auto-generated, or 0 if hardware ID unavailable
     * @note Can be called before Build() to configure role-based settings
     */
    static AddressType GenerateAddressFromHardware();

    /**
     * @brief Get the node's address in the network
     *
     * @return AddressType The node address
     */
    AddressType GetNodeAddress() const;

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
     * @brief Get the LoRaMesh protocol instance (const version)
     * @return std::shared_ptr<const protocols::LoRaMeshProtocol> Shared pointer to the LoRaMesh protocol,
     *         or nullptr if not available
     */
    std::shared_ptr<const protocols::LoRaMeshProtocol> GetLoRaMeshProtocol()
        const;

    /**
     * @brief Get the hardware manager
     *
     * @return std::shared_ptr<hardware::HardwareManagerInterface> The hardware manager
     */
    std::shared_ptr<hardware::IHardwareManager> GetHardwareManager() const {
        return hardware_manager_;
    }

    // Application Layer Interface

    /**
     * @brief Simple data send method for application layer
     *
     * @param destination Destination node address
     * @param data Data payload to send
     * @return Result Success if message was queued, error details otherwise
     */
    [[nodiscard]] Result Send(AddressType destination,
                              const std::vector<uint8_t>& data);

    /**
     * @brief Set callback for received data messages
     * @warning This callback should be small or transfer the message into another task for processing.
     *
     * @param callback Function to call when data is received
     */
    void SetDataCallback(DataReceivedCallback callback);

    /**
     * @brief Get current routing table with raw entry data
     *
     * @return std::vector<RouteEntry> Vector of routing entries
     */
    std::vector<RouteEntry> GetRoutingTable() const;

    /**
     * @brief Get current network status information
     *
     * @return NetworkStatus Current network status
     */
    NetworkStatus GetNetworkStatus() const;

    /**
     * @brief Get current slot allocation table
     *
     * @return Span over active slot allocations (valid for object lifetime)
     */
    std::span<const types::protocols::lora_mesh::SlotAllocation> GetSlotTable()
        const;

    /**
     * @brief Get milliseconds to sleep before calling Send() for the next TX data slot
     *
     * Returns how long to delay so that Send() + protocol processing completes
     * before the next TX data slot begins. Subtracts guard_time_ms from the
     * raw slot start time.
     *
     * Returns 0 if no TX slots are allocated or the node has not joined a network.
     *
     * @param guard_time_ms Milliseconds subtracted from slot start (default 200ms)
     * @return uint32_t Milliseconds to sleep before calling Send()
     */
    uint32_t GetTimeUntilNextDataSlot(
        uint32_t guard_time_ms =
            protocols::LoRaMeshProtocol::kDefaultDataSlotGuardTimeMs) const;

    /**
     * @brief Get number of TX data slots allocated to this node per superframe
     *
     * @return uint8_t Number of TX slots, or 0 if not in NORMAL_OPERATION
     */
    uint8_t GetDataSlotsPerSuperframe() const;

    /**
     * @brief Get number of messages pending in the TX queue
     *
     * @return size_t Number of messages waiting to be transmitted
     */
    size_t GetTxQueueSize() const;

    /**
     * @brief Get number of messages pending in the RX queue
     *
     * @return size_t Number of messages waiting to be processed
     */
    size_t GetRxQueueSize() const;

    /**
     * @brief Set local node capabilities
     *
     * Updates the capabilities for this node. Changes will be propagated
     * in the next routing table broadcast.
     *
     * @param capabilities Capabilities bitmap (NodeCapabilities flags)
     */
    void SetNodeCapabilities(uint8_t capabilities);

    /**
     * @brief Request a runtime change of this node's NodeRole
     *
     * Thread-safe. The request is queued on the protocol task and applied
     * at a safe point, driving the appropriate state transition:
     *  - NODE_ONLY/AUTO -> NETWORK_MANAGER before joining a network: the
     *    node creates a network immediately.
     *  - NODE_ONLY/AUTO -> NETWORK_MANAGER while already joined: the node
     *    broadcasts NM_CLAIM with its new priority; the incumbent NM
     *    yields via the standard merge protocol.
     *  - NETWORK_MANAGER -> NODE_ONLY/AUTO while acting as NM: the node
     *    surrenders and enters DISCOVERY; the rest of the network runs an
     *    election to pick a new NM (~5 superframes of disruption).
     *
     * Typical use: boot all nodes as NODE_ONLY; promote one to
     * NETWORK_MANAGER from an external event (e.g. Wi-Fi connect).
     *
     * @param role Desired NodeRole
     * @return Result Success if queued; error if LoraMesher is not running
     */
    [[nodiscard]] Result SetNodeRole(NodeRole role);

    /**
     * @brief Get the node role currently in effect
     *
     * May transiently differ from a just-queued SetNodeRole() call until
     * the protocol task drains the notification queue.
     *
     * @return NodeRole Current role
     */
    NodeRole GetNodeRole() const;

    /**
     * @brief Get local node capabilities
     *
     * @return uint8_t Local node capabilities bitmap
     */
    uint8_t GetNodeCapabilities() const;

    /**
     * @brief Get capabilities for a remote node
     *
     * @param node_address Address of the node to query
     * @return uint8_t Node capabilities bitmap (0 if unknown)
     */
    uint8_t GetNodeCapabilities(AddressType node_address) const;

    /**
     * @brief Find the closest node that has a specific capability
     *
     * Searches all active routes for nodes matching the given capability flag
     * and returns the one with the lowest route cost (based on hop count and
     * link quality).
     *
     * @param capability Capability bitmap to match (e.g. NodeCapabilities::GATEWAY)
     * @return std::optional<RouteEntry> The closest matching node, or nullopt if none found
     */
    std::optional<RouteEntry> GetClosestNodeByCapability(
        uint8_t capability) const;

    /**
     * @brief Find the closest gateway node in the network
     *
     * Convenience method equivalent to
     * GetClosestNodeByCapability(NodeCapabilities::GATEWAY).
     *
     * @return std::optional<RouteEntry> The closest gateway, or nullopt if none found
     */
    std::optional<RouteEntry> GetClosestGateway() const;

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
     * @brief Resolve the node address from configuration or auto-generation
     *
     * Encapsulates all address resolution logic: checks for configured address,
     * attempts hardware-based generation, falls back to random generation.
     *
     * @param protocol_config The protocol configuration (may be updated with generated address)
     * @return AddressType The resolved node address
     */
    AddressType ResolveNodeAddress(ProtocolConfig& protocol_config);

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
    DataReceivedCallback data_callback_ = nullptr;
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
     * @brief Enable/disable automatic address generation from hardware
     *
     * When enabled (default), automatic address generation will use hardware unique IDs
     * (such as ESP32 eFuse MAC) to generate collision-resistant addresses.
     * When disabled, falls back to enhanced random generation.
     *
     * @param enable True to use hardware ID for address generation (default: true)
     * @return Builder& Reference to this builder for method chaining
     */
    Builder& withAutoAddressFromHardware(bool enable = true) {
        config_.setAutoAddressFromHardware(enable);
        return *this;
    }

    /**
     * @brief Set node capabilities
     *
     * @param capabilities Node capabilities bitmap (NodeCapabilities flags)
     * @return Builder& Reference to this builder for method chaining
     */
    Builder& withNodeCapabilities(uint8_t capabilities) {
        auto protocol_config = config_.getProtocolConfig();
        if (protocol_config.getProtocolType() ==
            protocols::ProtocolType::kLoraMesh) {
            auto lora_config = protocol_config.getLoRaMeshConfig();
            lora_config.setNodeCapabilities(capabilities);
            protocol_config.setLoRaMeshConfig(lora_config);
            config_.setProtocolConfig(protocol_config);
        }
        return *this;
    }

    /**
     * @brief Set the callback invoked before device enters sleep
     *
     * @param callback Function to call before sleep
     * @return Builder& Reference to this builder for method chaining
     */
    Builder& withPrepareSleepCallback(power::PrepareSleepCallback callback) {
        auto protocol_config = config_.getProtocolConfig();
        if (protocol_config.getProtocolType() ==
            protocols::ProtocolType::kLoraMesh) {
            auto lora_config = protocol_config.getLoRaMeshConfig();
            lora_config.setPrepareSleepCallback(std::move(callback));
            protocol_config.setLoRaMeshConfig(lora_config);
            config_.setProtocolConfig(protocol_config);
        }
        return *this;
    }

    /**
     * @brief Set the callback invoked when device wakes from sleep
     *
     * @param callback Function to call after wake up
     * @return Builder& Reference to this builder for method chaining
     */
    Builder& withWakeUpCallback(power::WakeUpCallback callback) {
        auto protocol_config = config_.getProtocolConfig();
        if (protocol_config.getProtocolType() ==
            protocols::ProtocolType::kLoraMesh) {
            auto lora_config = protocol_config.getLoRaMeshConfig();
            lora_config.setWakeUpCallback(std::move(callback));
            protocol_config.setLoRaMeshConfig(lora_config);
            config_.setProtocolConfig(protocol_config);
        }
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