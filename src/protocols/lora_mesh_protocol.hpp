/**
 * @file lora_mesh_protocol.hpp
 * @brief Refactored LoRaMesh protocol using service-oriented architecture
 */

#pragma once

#include <functional>
#include <memory>

#include "hardware/hardware_manager.hpp"
#include "lora_mesh/services/message_queue_service.hpp"
#include "lora_mesh/services/network_service.hpp"
#include "lora_mesh/services/subslot_scheduler.hpp"
#include "lora_mesh/services/superframe_service.hpp"
#include "os/rtos.hpp"
#include "types/configurations/protocol_configuration.hpp"
#include "types/protocols/protocol.hpp"
#include "utils/compat/span.hpp"

namespace loramesher {
namespace protocols {

/**
 * @brief Refactored LoRaMesh protocol implementation
 * 
 * Coordinates multiple services to provide mesh networking capabilities.
 * The protocol acts as a thin coordination layer while services handle
 * the actual protocol logic.
 */
class LoRaMeshProtocol : public Protocol {
   public:
    /**
     * @brief Service configuration containing all sub-configurations
     */
    struct ServiceConfiguration {
        lora_mesh::INetworkService::NetworkConfig network_config;
        size_t message_queue_size = 10;
        uint32_t superframe_update_interval_ms = 20;
    };

    /**
     * @brief Protocol event notification types for event-driven operation
     */
    enum class ProtocolNotificationType : uint8_t {
        RADIO_EVENT = 1,  ///< Radio event received, process radio queue
        STATE_TIMEOUT,    ///< State timeout occurred, check state transitions
        STATE_CHANGE,     ///< Protocol state changed, update behavior
        SHUTDOWN,         ///< Protocol shutdown requested
        SLOT_TRANSITION  ///< Superframe slot boundary reached; drain slot_transition_queue_
    };

    /**
     * @brief Payload carried by SLOT_TRANSITION notifications
     */
    struct SlotTransitionData {
        uint16_t slot;
        bool new_superframe;
        uint32_t
            arrival_time_ms;  ///< GetTimeInSlot() captured when the superframe callback fired
    };

    /**
     * @brief Constructor
     */
    LoRaMeshProtocol();

    /**
     * @brief Destructor
     */
    ~LoRaMeshProtocol() override;

    /**
     * @brief Initialize the protocol and all services
     * 
     * @param hardware Hardware manager interface
     * @param node_address Local node address
     * @return Result Success or error details
     */
    Result Init(std::shared_ptr<hardware::IHardwareManager> hardware,
                AddressType node_address) override;

    /**
     * @brief Configure the protocol with specific settings
     * 
     * @param config Protocol configuration
     * @return Result Success or error details
     */
    Result Configure(const LoRaMeshProtocolConfig& config);

    /**
     * @brief Start protocol operation
     * 
     * @return Result Success or error details
     */
    Result Start() override;

    /**
     * @brief Stop protocol operation
     * 
     * @return Result Success or error details
     */
    Result Stop() override;

    /**
     * @brief Send a message through the protocol
     *
     * @param message Message to send
     * @return Result Success or error details
     */
    Result SendMessage(const BaseMessage& message) override;

    /**
     * @brief Send user data to a destination through the mesh network
     *
     * Creates a DataMessage with proper next-hop routing and queues
     * it for transmission. The data will be routed through the mesh
     * network to reach the destination.
     *
     * @param destination Final destination address
     * @param data User data payload to send
     * @return Result Success or error (e.g., no route found)
     */
    Result SendData(AddressType destination, const std::vector<uint8_t>& data);

    /**
     * @brief Send a broadcast message to all nodes in the mesh
     *
     * @param data User data payload
     * @return Result Success or error
     */
    Result SendBroadcast(std::span<const uint8_t> data);

    /**
     * @brief Pause all protocol services
     * 
     * @return Result Success or error details
     */
    Result Pause();

    /**
     * @brief Resume all protocol services
     * 
     * @return Result Success or error details
     */
    Result Resume();

    /**
     * @brief Get current protocol state
     * 
     * @return ProtocolState Current state
     */
    lora_mesh::INetworkService::ProtocolState GetState() const;

    /**
     * @brief Check if protocol is synchronized
     * 
     * @return bool True if synchronized
     */
    bool IsSynchronized() const;

    /**
     * @brief Get network manager address
     * 
     * @return AddressType Network manager address
     */
    AddressType GetNetworkManager() const;

    /**
     * @brief Get current slot number
     * 
     * @return uint16_t Current slot
     */
    uint16_t GetCurrentSlot() const;

    /**
     * @brief Get the Slot duration in ms
     * 
     * @return uint32_t Slot duration in milliseconds
     */
    uint32_t GetSlotDuration() const;

    /**
     * @brief Set route update callback
     *
     * @param callback Callback function
     */
    void SetRouteUpdateCallback(
        lora_mesh::INetworkService::RouteUpdateCallback callback);

    /**
     * @brief Set data received callback
     *
     * @param callback Callback function
     */
    void SetDataReceivedCallback(
        lora_mesh::INetworkService::DataReceivedCallback callback);

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
     * @brief Get local node capabilities
     *
     * @return uint8_t Local node capabilities bitmap
     */
    uint8_t GetLocalNodeCapabilities() const;

    /**
     * @brief Get capabilities for a specific node
     *
     * @param node_address Address of the node to query
     * @return uint8_t Node capabilities bitmap (0 if node not found)
     */
    uint8_t GetNodeCapabilities(AddressType node_address) const;

    /**
     * @brief Get all network nodes with their routing information
     *
     * Note: Caller must be careful with concurrent access as this returns
     * a reference to the internal vector.
     *
     * @return const std::vector<NetworkNodeRoute>& Reference to all nodes
     */
    const std::vector<types::protocols::lora_mesh::NetworkNodeRoute>&
    GetNetworkNodes() const;

    std::vector<types::protocols::lora_mesh::NetworkNodeRoute>
    GetNetworkNodesCopy() const;

#ifdef DEBUG
    lora_mesh::NetworkService* GetNetworkServiceForTest() {
        return network_service_.get();
    }
#endif

    /**
     * @brief Get Service Configuration
     * 
     * @return const ServiceConfiguration& Current service configuration
     */
    const ServiceConfiguration& GetServiceConfiguration() const;

    /**
     * @brief Get the discovery timeout
     * 
     * @return uint32_t Discovery timeout in milliseconds
     */
    uint32_t GetDiscoveryTimeout();

    /**
     * @brief Get the join timeout
     * 
     * @return uint32_t Join timeout in milliseconds
     */
    uint32_t GetJoinTimeout();

    /**
     * @brief Get the Slot duration in ms
     * 
     * @return uint32_t Slot duration in milliseconds
     */
    uint32_t GetSlotDuration();

    /**
     * @brief Get current slot table
     *
     * @return Span over active slot allocations (valid for object lifetime)
     */
    std::span<const types::protocols::lora_mesh::SlotAllocation> GetSlotTable()
        const {
        return network_service_->GetSlotTable();
    }

    /**
     * @brief Default guard time for data slot timing queries (ms)
     *
     * Accounts for FreeRTOS task wake-up latency, application processing,
     * and protocol queue pickup before the TX slot begins.
     */
    static constexpr uint32_t kDefaultDataSlotGuardTimeMs = 200;

    /**
     * @brief Get milliseconds to sleep before calling Send() for the next TX data slot
     *
     * Returns how long to delay so that Send() + protocol processing completes
     * before the next TX data slot begins. Subtracts guard_time_ms from the
     * raw slot start time.
     *
     * - If currently in a TX slot or within guard_time_ms of one: skips to the
     *   same slot in the next superframe.
     * - Returns 0 if no TX slots are allocated or the protocol is not running.
     *
     * @param guard_time_ms Milliseconds subtracted from slot start (default 200ms)
     * @return uint32_t Milliseconds to sleep before calling Send()
     */
    uint32_t GetTimeUntilNextDataSlot(
        uint32_t guard_time_ms = kDefaultDataSlotGuardTimeMs) const;

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

   private:
    /**
     * @brief Main protocol task function
     * 
     * @param parameters Protocol instance pointer
     */
    static void ProtocolTaskFunction(void* parameters);

    /**
     * @brief Process radio events
     * 
     * Polls for radio events and forwards to network service
     */
    void ProcessRadioEvents();

    /**
     * @brief Handle slot transition callback from superframe service
     * 
     * @param current_slot Current slot number
     * @param new_superframe Whether this is a new superframe
     */
    void OnSlotTransition(uint16_t current_slot, bool new_superframe);

    /**
     * @brief Handle state change callback from network service
     * 
     * @param new_state New protocol state
     */
    void OnStateChange(lora_mesh::INetworkService::ProtocolState new_state);

    /**
     * @brief Handle network topology change
     * 
     * @param route_updated Whether the route was updated, if false, the route is stale
     * @param destination Destination address of the route
     * @param next_hop Next hop address for the route
     * @param hop_count Number of hops to destination
     */
    void OnNetworkTopologyChange(bool route_updated, AddressType destination,
                                 AddressType next_hop, uint8_t hop_count);

    /**
     * @brief Process messages for current slot type
     * 
     * @param slot_type Type of current slot
     */
    void ProcessSlotMessages(
        types::protocols::lora_mesh::SlotAllocation::SlotType slot_type);

    /**
     * @brief Drain all remaining events from the radio event queue
     *
     * This method is called during cleanup to ensure all events are properly
     * deleted before the queue is destroyed, preventing memory leaks.
     */
    void DrainRadioEventQueue();

    /**
     * @brief Send notification to protocol task for event-driven processing
     *
     * @param notification_type Type of notification to send
     */
    void NotifyProtocolTask(ProtocolNotificationType notification_type);

    /**
     * @brief Create service configuration from protocol config
     * 
     * @param config Protocol configuration
     * @return ServiceConfiguration Service configuration
     */
    ServiceConfiguration CreateServiceConfig(
        const LoRaMeshProtocolConfig& config);

#ifdef DEBUG
    /**
     * @brief Create service configuration for test purposes
     * 
     * @param config Protocol configuration
     * @return ServiceConfiguration Service configuration
     */
    ServiceConfiguration CreateServiceConfigForTest(
        const LoRaMeshProtocolConfig& config);
#endif  // DEBUG

    /**
     * @brief Start the discovery process to find other nodes
     * 
     * @return Result Success or error details
     */
    Result StartDiscovery();

    /**
     * @brief Adds a routing table message into the queue service if it does not exist
     * 
     * @return Result Success or error details
     */
    Result AddRoutingMessageToQueueService();

    /// Returns true if a message can be transmitted before the slot ends.
    /// @param message_size  Bytes (for ToA calculation via hardware)
    /// @param additional_delay_ms  Pre-TX delay not yet elapsed (guard time, subslot wait)
    bool CanFitInSlot(uint8_t message_size,
                      uint32_t additional_delay_ms = 0) const;

    /// Extracts a queued message and sends it with guard-time + ToA check.
    Result TrySendGuardedMessage(
        types::protocols::lora_mesh::SlotAllocation::SlotType slot_type);

    /// Extracts a queued message, attempts subslotted TX. Falls back to
    /// immediate TX if the subslot offset pushes ToA past the slot boundary.
    Result TrySendSubslottedMessage(
        types::protocols::lora_mesh::SlotAllocation::SlotType slot_type,
        const lora_mesh::SubslotConfig& config, uint16_t identifier);

    // Services
    std::shared_ptr<lora_mesh::MessageQueueService> message_queue_service_;
    std::shared_ptr<lora_mesh::SuperframeService> superframe_service_;
    std::shared_ptr<lora_mesh::NetworkService> network_service_;

    // Task management
    os::TaskHandle_t protocol_task_handle_;
    os::QueueHandle_t radio_event_queue_;
    os::QueueHandle_t
        protocol_notification_queue_;  ///< Queue for protocol event notifications
    os::QueueHandle_t
        slot_transition_queue_;  ///< Queue carrying SlotTransitionData for SLOT_TRANSITION events

    // Configuration
    LoRaMeshProtocolConfig config_;
    ServiceConfiguration service_config_;

    // Power management callbacks
    power::PrepareSleepCallback prepare_sleep_callback_ = nullptr;
    power::WakeUpCallback wake_up_callback_ = nullptr;
    power::PowerState current_power_state_ = power::PowerState::ACTIVE;

    // Subslot scheduling state
    bool in_subslotted_slot_ =
        false;  ///< True during subslotted slots (radio stays in RX)
    uint32_t current_slot_arrival_time_ms_ =
        0;  ///< GetTimeInSlot() at slot boundary (from SlotTransitionData)

    // Constants
    static constexpr uint32_t PROTOCOL_TASK_STACK_SIZE = 8192;
    static constexpr uint32_t TASK_PRIORITY = 3;
    static constexpr size_t RADIO_QUEUE_SIZE = 10;
    static constexpr size_t PROTOCOL_NOTIFICATION_QUEUE_SIZE =
        16;  ///< Protocol notification queue size
    static constexpr uint32_t QUEUE_WAIT_TIMEOUT_MS = 100;
    static constexpr uint32_t DEFAULT_HELLO_INTERVAL_MS = 60000;
};

}  // namespace protocols
}  // namespace loramesher