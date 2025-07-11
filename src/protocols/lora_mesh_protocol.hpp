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
#include "lora_mesh/services/superframe_service.hpp"
#include "os/rtos.hpp"
#include "types/configurations/protocol_configuration.hpp"
#include "types/protocols/protocol.hpp"

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
     * @brief Set route update callback
     * 
     * @param callback Callback function
     */
    void SetRouteUpdateCallback(
        lora_mesh::INetworkService::RouteUpdateCallback callback);

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
     * @return const std::vector<SlotAllocation>& Slot table
     */
    const std::vector<types::protocols::lora_mesh::SlotAllocation>&
    GetSlotTable() const {
        return network_service_->GetSlotTable();
    }

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

    // Services
    std::shared_ptr<lora_mesh::MessageQueueService> message_queue_service_;
    std::shared_ptr<lora_mesh::SuperframeService> superframe_service_;
    std::shared_ptr<lora_mesh::NetworkService> network_service_;

    // Task management
    os::TaskHandle_t protocol_task_handle_;
    os::QueueHandle_t radio_event_queue_;

    // Configuration
    LoRaMeshProtocolConfig config_;
    ServiceConfiguration service_config_;

    // Constants
    static constexpr uint32_t PROTOCOL_TASK_STACK_SIZE = 4096;
    static constexpr uint32_t TASK_PRIORITY = 3;
    static constexpr size_t RADIO_QUEUE_SIZE = 10;
    static constexpr uint32_t QUEUE_WAIT_TIMEOUT_MS = 100;
    static constexpr uint32_t DEFAULT_HELLO_INTERVAL_MS = 60000;
};

}  // namespace protocols
}  // namespace loramesher