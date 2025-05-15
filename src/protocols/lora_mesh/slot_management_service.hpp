/**
 * @file slot_management_service.hpp
 * @brief Interface for TDMA slot management
 */

#pragma once

#include <vector>
#include "types/error_codes/result.hpp"
#include "types/messages/base_message.hpp"

namespace loramesher {
namespace protocols {

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
 * @brief Interface for slot management service
 * 
 * Handles TDMA slot allocation and scheduling
 */
class ISlotManagementService {
   public:
    virtual ~ISlotManagementService() = default;

    /**
     * @brief Process a slot request message
     * 
     * @param message The slot request message
     * @return Result Success if processed correctly, error details otherwise
     */
    virtual Result ProcessSlotRequest(const BaseMessage& message) = 0;

    /**
     * @brief Process a slot allocation message
     * 
     * @param message The slot allocation message
     * @return Result Success if processed correctly, error details otherwise
     */
    virtual Result ProcessSlotAllocation(const BaseMessage& message) = 0;

    /**
     * @brief Send a slot request
     * 
     * @param num_slots Number of slots needed
     * @return Result Success if request sent, error details otherwise
     */
    virtual Result SendSlotRequest(uint8_t num_slots) = 0;

    /**
     * @brief Initialize the slot table
     * 
     * @param is_network_manager Whether this node is the network manager
     * @return Result Success if initialization successful, error details otherwise
     */
    virtual Result InitializeSlotTable(bool is_network_manager) = 0;

    /**
     * @brief Handle a slot transition
     * 
     * @param slot_number New slot number
     * @return Result Success if transition handled, error details otherwise
     */
    virtual Result HandleSlotTransition(uint16_t slot_number) = 0;

    /**
     * @brief Update slot allocation for the network
     * 
     * @return Result Success if update successful, error details otherwise
     */
    virtual Result UpdateSlotAllocation() = 0;

    /**
     * @brief Broadcast slot allocation to all nodes
     * 
     * @return Result Success if broadcast successful, error details otherwise
     */
    virtual Result BroadcastSlotAllocation() = 0;

    /**
     * @brief Get the current slot number
     * 
     * @return uint16_t Current slot number
     */
    virtual uint16_t GetCurrentSlot() const = 0;

    /**
     * @brief Get the current slot table
     * 
     * @return const std::vector<SlotAllocation>& Reference to slot table
     */
    virtual const std::vector<SlotAllocation>& GetSlotTable() const = 0;
};

}  // namespace protocols
}  // namespace loramesher