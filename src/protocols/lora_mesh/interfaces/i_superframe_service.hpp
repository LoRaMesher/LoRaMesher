/**
 * @file i_superframe_service.hpp
 * @brief Interface for superframe management service
 */

#pragma once

#include "types/error_codes/result.hpp"
#include "types/protocols/lora_mesh/superframe.hpp"

namespace loramesher {

namespace protocols {
namespace lora_mesh {

/**
 * @brief Interface for superframe management service
 * 
 * Defines the interface for managing superframe timing and synchronization
 */
class ISuperframeService {
   public:
    /**
     * @brief Virtual destructor
     */
    virtual ~ISuperframeService() = default;

    /**
     * @brief Start the superframe
     * 
     * @return Result Success if started successfully
     */
    virtual Result StartSuperframe() = 0;

    /**
     * @brief Stop the superframe
     * 
     * @return Result Success if stopped successfully
     */
    virtual Result StopSuperframe() = 0;

    /**
     * @brief Handle transition to a new superframe
     * 
     * @return Result Success if handled successfully
     */
    virtual Result HandleNewSuperframe() = 0;

    /**
     * @brief Check if superframe is synchronized
     * 
     * @return bool True if synchronized
     */
    virtual bool IsSynchronized() const = 0;

    /**
     * @brief Set whether the superframe is synchronized
     * 
     * @param synchronized True if synchronized, false otherwise
     */
    virtual void SetSynchronized(bool synchronized) = 0;

    /**
     * @brief Synchronize with external superframe timing
     * 
     * @param external_slot_start_time Time when external slot started
     * @param external_slot Slot number of external superframe
     * @return Result Success if synchronized successfully
     */
    virtual Result SynchronizeWith(uint32_t external_slot_start_time,
                                   uint16_t external_slot) = 0;

    /**
     * @brief Update superframe configuration
     * 
     * @param total_slots Total number of slots in superframe
     * @param slot_duration_ms Duration of each slot in milliseconds
     * @param update_superframe Whether to update the superframe immediately
     * @return Result Success if updated successfully
     */
    virtual Result UpdateSuperframeConfig(uint16_t total_slots,
                                          uint32_t slot_duration_ms = 0,
                                          bool update_superframe = true) = 0;

    virtual uint32_t GetSlotDuration() const = 0;

    static constexpr uint32_t DEFAULT_DISCOVERY_SLOT_COUNT = 10;
    static constexpr uint32_t DEFAULT_SLOT_DURATION_MS = 1000;
    static constexpr uint32_t DEFAULT_DISCOVERY_TIMEOUT_MS =
        DEFAULT_SLOT_DURATION_MS * DEFAULT_DISCOVERY_SLOT_COUNT * 3;
    static constexpr uint32_t DEFAULT_CONTROL_SLOT_COUNT = 10;
    static constexpr uint32_t DEFAULT_SLEEP_SLOT_COUNT = 10;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher