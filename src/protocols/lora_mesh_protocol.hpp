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
     * @brief Process a received radio event
     * 
     * @param event The radio event to be processed
     * @return Result Success if message was processed successfully, error details otherwise
     */
    Result ProcessReceivedRadioEvent(
        std::unique_ptr<radio::RadioEvent> event) override;

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
            TX,         ///< Transmission slot
            RX,         ///< Reception slot
            SLEEP,      ///< Sleep slot
            DISCOVERY,  ///< Discovery slot
            CONTROL     ///< Control slot
        };

        uint16_t slot_number;     ///< Slot number in the superframe
        SlotType type;            ///< Type of slot
        AddressType source;       ///< For RX slots, the expected source
        AddressType destination;  ///< For TX slots, the target destination
    };

    /**
     * @brief Structure for a superframe
     */
    struct Superframe {
        uint16_t total_slots;            ///< Total number of slots
        uint16_t data_slots;             ///< Number of data slots
        uint16_t discovery_slots;        ///< Number of discovery slots
        uint16_t control_slots;          ///< Number of control slots
        uint32_t slot_duration_ms;       ///< Duration of each slot in ms
        uint32_t superframe_start_time;  ///< Start time of current superframe
    };

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

    // Task handlers
    os::TaskHandle_t radio_task_handle_;  ///< Handle for radio events task
    os::TaskHandle_t
        slot_manager_task_handle_;  ///< Handle for slot management task
    os::TaskHandle_t protocol_task_handle_;  ///< Handle for protocol logic task

    // Queue handlers
    os::QueueHandle_t radio_event_queue_;     ///< Queue for radio events
    os::QueueHandle_t protocol_event_queue_;  ///< Queue for protocol events

    // Protocol state
    ProtocolState state_;          ///< Current protocol state
    bool stop_tasks_;              ///< Flag to stop all tasks
    uint16_t current_slot_;        ///< Current slot in superframe
    bool is_synchronized_;         ///< Whether node is synchronized
    AddressType network_manager_;  ///< Current network manager
    uint32_t last_sync_time_;      ///< Last time sync was received

    // Protocol data structures
    std::vector<RoutingEntry> routing_table_;  ///< Routing table
    std::vector<NetworkNode> network_nodes_;   ///< Known network nodes
    std::vector<SlotAllocation> slot_table_;   ///< Slot allocation table
    Superframe current_superframe_;            ///< Current superframe structure

    // Configuration
    LoRaMeshProtocolConfig config_;  ///< Protocol configuration

    // Callbacks
    RouteUpdateCallback route_update_callback_;  ///< Route update callback

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
     * @brief Process a routing update
     * 
     * @param source Source address of update
     * @param data Routing data
     * @return Result Success if update was processed, error details otherwise
     */
    Result ProcessRoutingUpdate(AddressType source,
                                const std::vector<uint8_t>& data);

    /**
     * @brief Process a slot allocation message
     * 
     * @param source Source address of message
     * @param data Slot allocation data
     * @return Result Success if allocation was processed, error details otherwise
     */
    Result ProcessSlotAllocation(AddressType source,
                                 const std::vector<uint8_t>& data);

    /**
     * @brief Process a discovery message
     * 
     * @param source Source address of message
     * @param data Discovery data
     * @return Result Success if discovery was processed, error details otherwise
     */
    Result ProcessDiscovery(AddressType source,
                            const std::vector<uint8_t>& data);

    /**
     * @brief Process a control message
     * 
     * @param source Source address of message
     * @param data Control data
     * @return Result Success if control message was processed, error details otherwise
     */
    Result ProcessControlMessage(AddressType source,
                                 const std::vector<uint8_t>& data);

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
     * @brief Send a synchronization message
     * 
     * @return Result Success if sync was sent, error details otherwise
     */
    Result SendSyncMessage();

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

    // Constants
    static constexpr uint32_t RADIO_TASK_STACK_SIZE = 2048;
    static constexpr uint32_t SLOT_MANAGER_TASK_STACK_SIZE = 2048;
    static constexpr uint32_t PROTOCOL_TASK_STACK_SIZE = 2048;
    static constexpr uint32_t TASK_PRIORITY = 3;
    static constexpr size_t RADIO_QUEUE_SIZE = 10;
    static constexpr size_t PROTOCOL_QUEUE_SIZE = 10;
    static constexpr uint32_t QUEUE_WAIT_TIMEOUT_MS = 100;
    static constexpr uint32_t DEFAULT_SLOT_DURATION_MS = 100;
    static constexpr uint16_t DEFAULT_SLOTS_PER_SUPERFRAME = 100;
    static constexpr uint8_t MAX_LINK_QUALITY = 100;
};

}  // namespace protocols
}  // namespace loramesher