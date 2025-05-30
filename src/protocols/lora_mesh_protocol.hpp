/**
 * @file src/protocols/lora_mesh_protocol.hpp
 * @brief Definition of LoRaMesh protocol for mesh networking
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "hardware/hardware_manager.hpp"
#include "os/rtos.hpp"
#include "types/configurations/protocol_configuration.hpp"
#include "types/messages/loramesher/join_response_header.hpp"
#include "types/protocols/lora_mesh/network_node_route.hpp"
#include "types/protocols/lora_mesh/slot_allocation.hpp"
#include "types/protocols/lora_mesh/superframe.hpp"
#include "types/protocols/protocol.hpp"

namespace loramesher {
namespace protocols {

/**
 * @brief Callback type for route update notifications
 * 
 * @param route_updated Whether the route was updated
 * @param destination The destination address
 * @param next_hop The next hop address
 * @param hop_count The hop count to destination
 */
using RouteUpdateCallback =
    std::function<void(bool route_updated, AddressType destination,
                       AddressType next_hop, uint8_t hop_count)>;

/**
 * @brief Implementation of the LoRaMesh protocol for mesh networking
 * 
 * This protocol provides multi-hop routing capabilities using a synchronization
 * mechanism between nodes, with scheduled transmission/reception/sleep cycles.
 */
class LoRaMeshProtocol : public Protocol {
   public:
    /**
     * @brief Constructor for LoRaMeshProtocol
     */
    LoRaMeshProtocol();

    /**
     * @brief Destructor for LoRaMeshProtocol
     * 
     * Ensures all tasks are properly cleaned up.
     */
    ~LoRaMeshProtocol() override;

    /**
     * @brief Initialize the protocol
     * 
     * Creates and starts the required tasks.
     * 
     * @param hardware Shared pointer to the hardware manager
     * @param node_address The address of this node
     * @return Result Success if initialization was successful, error details otherwise
     */
    Result Init(std::shared_ptr<hardware::IHardwareManager> hardware,
                AddressType node_address) override;

    /**
     * @brief Configure the protocol with specific settings
     * 
     * @param config The LoRaMesh-specific configuration
     * @return Result Success if configuration was successful, error details otherwise
     */
    Result Configure(const LoRaMeshProtocolConfig& config);

    /**
     * @brief Start the protocol operation
     *
     * @return Result Success if started successfully, error details otherwise
     */
    Result Start() override;

    /**
     * @brief Stop the protocol operation
     *
     * @return Result Success if stopped successfully, error details otherwise
     */
    Result Stop() override;

    /**
     * @brief Send a message using the LoRaMesh protocol
     * 
     * This is a generic message sending method inherited from Protocol.
     * 
     * @param message The message to send
     * @return Result Success if message was sent successfully, error details otherwise
     */
    Result SendMessage(const BaseMessage& message) override;

    /**
     * @brief Set a callback for route updates
     * 
     * @param callback Function to call when routes are updated
     */
    void SetRouteUpdateCallback(RouteUpdateCallback callback);

    /**
     * @brief Enum for protocol state
     */
    enum class ProtocolState {
        INITIALIZING,      ///< Protocol is initializing
        DISCOVERY,         ///< Looking for existing network
        JOINING,           ///< Attempting to join network
        NORMAL_OPERATION,  ///< Normal network operation
        NETWORK_MANAGER,   ///< Acting as network manager
        FAULT_RECOVERY     ///< Attempting to recover from fault
    };

    ProtocolState GetState() const { return state_; }

    bool IsSynchronized() const { return is_synchronized_; }

    AddressType GetNetworkManager() const { return network_manager_; }

    uint16_t GetCurrentSlot() const { return current_slot_; }

    uint32_t GetSlotDurationMs() const {
        return current_superframe_.slot_duration_ms;
    }

    /**
     * @brief Get the discovery timeout period used by the protocol
     * 
     * @return Discovery timeout in milliseconds
     */
    static const uint32_t GetDiscoveryTimeout() {
        return DEFAULT_SUPERFRAMES_TO_CREATE_NEW_NETWORK *
               DEFAULT_SLOTS_PER_SUPERFRAME * DEFAULT_SLOT_DURATION_MS;
    }

#ifdef DEBUG
    void SetTimeFunction(std::function<uint32_t()> time_function) {
        get_time_function_ = time_function;
    }
#endif

   protected:
    /**
    * @brief Set up a new network
    */
    void SetupNewNetwork();

   private:
    // Task handlers
    os::TaskHandle_t radio_task_handle_;  ///< Handle for radio events task
    os::TaskHandle_t
        slot_manager_task_handle_;  ///< Handle for slot management task

    // Queue handlers
    os::QueueHandle_t protocol_event_queue_;  ///< Queue for radio events

    // Protocol state
    ProtocolState state_;          ///< Current protocol state
    bool stop_tasks_;              ///< Flag to stop all tasks
    uint16_t current_slot_;        ///< Current slot in superframe
    bool is_synchronized_;         ///< Whether node is synchronized
    AddressType network_manager_;  ///< Current network manager
    uint32_t last_sync_time_;      ///< Last time sync was received
    types::protocols::lora_mesh::Superframe
        current_superframe_;  ///< Current superframe structure

    // Protocol data structures
    std::vector<types::protocols::lora_mesh::NetworkNodeRoute>
        routing_table_;  ///< Routing table
    std::vector<types::protocols::lora_mesh::SlotAllocation>
        slot_table_;  ///< Slot allocation table
    std::unordered_map<types::protocols::lora_mesh::SlotAllocation::SlotType,
                       std::vector<std::unique_ptr<BaseMessage>>>
        message_queue_;  ///< Queue for incoming messages

    // Configuration
    LoRaMeshProtocolConfig config_;  ///< Protocol configuration
#ifdef DEBUG
    std::function<uint32_t()>
        get_time_function_;  ///< Function to get current time (for testing)
#endif

    // Callbacks
    RouteUpdateCallback route_update_callback_;  ///< Route update callback

    /**
      * @brief Process a received radio event according to LoRaMesh protocol
      * 
      * @param event The radio event to be processed
      * @return Result Success if message was processed successfully, error details otherwise
      */
    Result ProcessReceivedRadioEvent(std::unique_ptr<radio::RadioEvent> event);

    /**
     * @brief Static task function for slot management
     * 
     * @param parameters Pointer to the LoRaMeshProtocol instance
     */
    static void SlotManagerTaskFunction(void* parameters);

    /**
     * @brief Static task function for protocol logic
     * 
     * @param parameters Pointer to the LoRaMeshProtocol instance
     */
    static void ProtocolLogicTaskFunction(void* parameters);

    /**
     * @brief Logic for discovery phase
     * 
     * @return Result Success if logic executed successfully, error details otherwise
     */
    Result LogicDiscovery();

    /**
     * @brief Logic for normal operation phase
     * 
     * @return Result Success if logic executed successfully, error details otherwise
     */
    Result LogicNormalOperation();

    /**
     * @brief Logic for network manager phase
     * 
     * @return Result Success if logic executed successfully, error details otherwise
     */
    Result LogicNetworkManager();

    /**
     * @brief Logic for fault recovery phase
     * 
     * @return Result Success if logic executed successfully, error details otherwise
     */
    Result LogicFaultRecovery();

    /**
     * @brief Logic for joining phase
     * 
     * @return Result Success if logic executed successfully, error details otherwise
     */
    Result LogicJoining();

    /**
     * @brief Send a routing table update
     * 
     * @return Result Success if update was sent, error details otherwise
     */
    Result SendRoutingTableUpdate();

    /**
     * @brief Send a slot request
     * 
     * @param num_slots Number of slots needed
     * @return Result Success if request was sent, error details otherwise
     */
    Result SendSlotRequest(uint8_t num_slots);

    /**
     * @brief Send a discovery message
     * 
     * @return Result Success if discovery was sent, error details otherwise
     */
    Result SendDiscoveryMessage();

    /**
     * @brief Send a control message
     * 
     * @return Result Success if control was sent, error details otherwise
     */
    Result SendControlMessage();

    /**
     * @brief Send a data message
     * 
     * @return Result Success if data was sent, error details otherwise
     */
    Result SendDataMessage();

    /**
     * @brief Find the best route to a destination
     * 
     * @param destination Destination address
     * @return AddressType Next hop address, or 0 if no route found
     */
    AddressType FindNextHop(AddressType destination) const;

    /**
     * @brief Handle slot transition
     * 
     * @param slot_number New slot number
     */
    void HandleSlotTransition(uint16_t slot_number);

    /**
     * @brief Get the current time in milliseconds
     * 
     * @return uint32_t Current time
     */
    uint32_t GetCurrentTime() const;

    /**
     * @brief Add a message to the queue
     * 
     * @param type Message type
     * @param message Unique pointer to the message
     */
    void AddMessageToMessageQueue(
        types::protocols::lora_mesh::SlotAllocation::SlotType type,
        std::unique_ptr<BaseMessage> message);

    /**
     * @brief Extract the first message of a specific type from the queue
     * 
     * @param type Message type to retrieve
     * @return std::unique_ptr<BaseMessage> The extracted message, or nullptr if none found
     */
    std::unique_ptr<BaseMessage> ExtractMessageQueueOfType(
        types::protocols::lora_mesh::SlotAllocation::SlotType type);

    /**
     * @brief Initialize the slot table for a network with routing awareness
     * 
     * @param is_network_manager Whether this node is the network manager
     * @return Result Success if initialization was successful, error details otherwise
     */
    Result InitializeSlotTable(bool is_network_manager);

    /**
     * @brief Allocate data slots based on routing table information
     * 
     * @param is_network_manager Whether this node is the network manager
     * @param available_data_slots Number of data slots available to allocate
     */
    void AllocateDataSlotsBasedOnRouting(bool is_network_manager,
                                         uint16_t available_data_slots);

    /**
     * @brief Find the next available slot starting from a given position
     * 
     * @param start_slot Starting slot number to search from
     * @return uint16_t Next available slot, or UINT16_MAX if none found
     */
    uint16_t FindNextAvailableSlot(uint16_t start_slot);

    /**
     * @brief Updates a routing entry based on received information
     * 
     * @param source Source of the routing update
     * @param destination Destination address in the route
     * @param hop_count Hop count reported by the source
     * @param link_quality Link quality reported by the source
     * @param allocated_slots Slots allocated to the destination
     * @return bool True if routing table was changed significantly
     */
    bool UpdateRoutingEntry(AddressType source, AddressType destination,
                            uint8_t hop_count, uint8_t link_quality,
                            uint8_t allocated_slots);

    /**
     * @brief Creates a routing table message for broadcast
     * 
     * @return std::unique_ptr<BaseMessage> The routing table message to send
     */
    std::unique_ptr<BaseMessage> CreateRoutingTableMessage();

    /**
     * @brief Calculate link quality to a neighbor based on signal strength, history, etc.
     * 
     * @param neighbor_address Address of the neighbor
     * @return uint8_t Link quality (0-100)
     */
    uint8_t CalculateLinkQuality(AddressType neighbor_address);

    /**
     * @brief Join an existing network
     * 
     * @param network_manager_address Address of the network manager
     * @return Result Success if join was successful, error details otherwise
     */
    Result JoinNetwork(AddressType network_manager_address);

    /**
     * @brief Update information about a network node
     * 
     * @param node_address Address of the node
     * @param battery_level Optional battery level update (0-100)
     * @return bool True if the node was newly added to our network
     */
    bool UpdateNetworkNodeInfo(AddressType node_address, uint8_t battery_level);

    /**
     * @brief Sends a join request to the network
     * 
     * @param manager_address Address of the network manager to join
     * @param requested_slots Number of data slots requested
     * @return Result Success if request was sent, error otherwise
     */
    Result SendJoinRequest(AddressType manager_address,
                           uint8_t requested_slots);

    /**
     * @brief Process a join request from a node
     * 
     * @param message The join request message
     * @return Result Success if processed successfully, error otherwise
     */
    Result ProcessJoinRequest(const BaseMessage& message);

    /**
     * @brief Process a join response from the network manager
     * 
     * @param message The join response message
     * @return Result Success if processed successfully, error otherwise
     */
    Result ProcessJoinResponse(const BaseMessage& message);

    /**
     * @brief Process a slot request from a node
     * 
     * @param message The slot request message
     * @return Result Success if processed successfully, error otherwise
     */
    Result ProcessSlotRequest(const BaseMessage& message);

    /**
     * @brief Process a slot allocation message
     * 
     * @param message The slot allocation message
     * @return Result Success if processed successfully, error otherwise
     */
    Result ProcessSlotAllocation(const BaseMessage& message);

    /**
     * @brief Process a routing table message
     * 
     * @param message The routing table message
     * @return Result Success if processed successfully, error otherwise
     */
    Result ProcessRoutingTableMessage(const BaseMessage& message);

    /**
     * @brief Determines if a join request should be accepted
     * 
     * @param node_address Address of the requesting node
     * @param capabilities Node capabilities
     * @param requested_slots Number of requested slots
     * @return std::pair<bool, uint8_t> First element is true if accepted, 
     *         second element is the number of allocated slots
     */
    std::pair<bool, uint8_t> ShouldAcceptJoin(AddressType node_address,
                                              uint8_t capabilities,
                                              uint8_t requested_slots);

    /**
     * @brief Sends a join response to a node
     * 
     * @param dest Address of the node that requested to join
     * @param status Response status (accepted/rejected)
     * @param allocated_slots Number of slots allocated (if accepted)
     * @return Result Success if response was sent, error otherwise
     */
    Result SendJoinResponse(AddressType dest,
                            JoinResponseHeader::ResponseStatus status,
                            uint8_t allocated_slots);

    /**
     * @brief Update slot allocation for all nodes in the network
     * 
     * @return Result Success if update was successful, error otherwise
     */
    Result UpdateSlotAllocation();

    /**
     * @brief Broadcast slot allocation to all nodes in the network
     * 
     * @return Result Success if broadcast was successful, error otherwise
     */
    Result BroadcastSlotAllocation();

    /**
     * @brief Get the allocated data slots from the routing table
     * 
     * @return uint8_t Number of allocated data slots
     */
    uint8_t GetAlocatedDataSlotsFromRoutingTable();

#ifdef DEBUG
   public:
#endif

    // Constants
    static constexpr uint32_t RADIO_TASK_STACK_SIZE = 2048;
    static constexpr uint32_t SLOT_MANAGER_TASK_STACK_SIZE = 2048;
    static constexpr uint32_t PROTOCOL_TASK_STACK_SIZE = 2048;
    static constexpr uint32_t TASK_PRIORITY = 3;
    static constexpr size_t RADIO_QUEUE_SIZE = 10;
    static constexpr size_t PROTOCOL_QUEUE_SIZE = 10;
    static constexpr uint32_t QUEUE_WAIT_TIMEOUT_MS = 100;
    static constexpr uint32_t DEFAULT_SLOT_DURATION_MS = 1000;
    static constexpr uint32_t DEFAULT_SLOT_EXTRA_DURATION_MS = 100;
    static constexpr uint16_t DEFAULT_SLOTS_PER_SUPERFRAME = 100;
    static constexpr uint8_t DEFAULT_SUPERFRAMES_TO_CREATE_NEW_NETWORK = 3;
    static constexpr uint8_t MAX_LINK_QUALITY = 100;
    static constexpr AddressType BROADCAST_ADDRESS = 0xFFFF;
    static constexpr uint8_t MAX_HOP_COUNT = 10;
    static constexpr uint8_t MAX_DEVICES = 50;
    static constexpr uint8_t MAX_DATA_SLOTS = 100;
};

}  // namespace protocols
}  // namespace loramesher