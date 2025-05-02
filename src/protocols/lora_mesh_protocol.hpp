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
    /**
     * @brief Structure representing a routing table entry
     */
    struct RoutingEntry {
        AddressType destination;  ///< Destination node address
        AddressType next_hop;     ///< Next hop to reach destination
        uint8_t hop_count;        ///< Number of hops to destination
        uint8_t link_quality;     ///< Link quality metric (0-100%)
        uint32_t last_updated;    ///< Timestamp of last update
        bool is_active;           ///< Whether this route is active
    };

    /**
     * @brief Structure representing a node in the network
     */
    struct NetworkNode {
        AddressType address;      ///< Node address
        uint8_t battery_level;    ///< Battery level (0-100%)
        uint32_t last_seen;       ///< Timestamp when last heard from
        bool is_network_manager;  ///< Whether this node is a network manager
    };

    /**
     * @brief Structure representing a slot allocation
     */
    struct SlotAllocation {
        enum class SlotType {
            TX,            ///< Transmission slot
            RX,            ///< Reception slot
            SLEEP,         ///< Sleep slot
            DISCOVERY_RX,  ///< Discovery slot
            DISCOVERY_TX,  ///< Discovery slot
            CONTROL_RX,    ///< Control slot
            CONTROL_TX,    ///< Control slot
        };

        uint16_t slot_number;  ///< Slot number in the superframe
        SlotType type;         ///< Type of slot
    };

    /**
     * @brief Structure for a superframe
     */
    struct Superframe {
        uint16_t total_slots;      ///< Total number of slots
        uint16_t data_slots;       ///< Number of data slots
        uint16_t discovery_slots;  ///< Number of discovery slots
        uint16_t control_slots;    ///< Number of control slots (Routing tables)
        uint32_t slot_duration_ms;       ///< Duration of each slot in ms
        uint32_t superframe_start_time;  ///< Start time of current superframe
    };

    /**
     * @brief Enum for message types
     */
    enum class MessageType {
        DISCOVERY,        ///< Discovery message
        SYNC,             ///< Synchronization message
        ROUTING_TABLE,    ///< Routing table update message
        SLOT_REQUEST,     ///< Slot request message
        SLOT_ALLOCATION,  ///< Slot allocation message
        DATA,             ///< Data message
        CONTROL           ///< Control message (e.g., ACK)
    };

    // Task handlers
    os::TaskHandle_t radio_task_handle_;  ///< Handle for radio events task
    os::TaskHandle_t
        slot_manager_task_handle_;  ///< Handle for slot management task

    // Queue handlers
    os::QueueHandle_t protocol_event_queue_;  ///< Queue for radio events

    // Protocol state
    ProtocolState state_;            ///< Current protocol state
    bool stop_tasks_;                ///< Flag to stop all tasks
    uint16_t current_slot_;          ///< Current slot in superframe
    bool is_synchronized_;           ///< Whether node is synchronized
    AddressType network_manager_;    ///< Current network manager
    uint32_t last_sync_time_;        ///< Last time sync was received
    Superframe current_superframe_;  ///< Current superframe structure

    // Protocol data structures
    std::vector<RoutingEntry> routing_table_;  ///< Routing table
    std::vector<NetworkNode> network_nodes_;   ///< Known network nodes
    std::vector<SlotAllocation> slot_table_;   ///< Slot allocation table
    std::unordered_map<MessageType,
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
     * @brief Static task function for radio event handling
     * 
     * @param parameters Pointer to the LoRaMeshProtocol instance
     */
    static void RadioEventTaskFunction(void* parameters);

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
    void AddMessageToMessageQueue(MessageType type,
                                  std::unique_ptr<BaseMessage> message);

    /**
     * @brief Extract the first message of a specific type from the queue
     * 
     * @param type Message type to retrieve
     * @return std::unique_ptr<BaseMessage> The extracted message, or nullptr if none found
     */
    std::unique_ptr<BaseMessage> ExtractMessageQueueOfType(MessageType type);

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
};

}  // namespace protocols
}  // namespace loramesher