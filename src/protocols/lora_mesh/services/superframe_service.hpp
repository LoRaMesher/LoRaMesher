/**
 * @file superframe_service.hpp
 * @brief Superframe management service with automatic update task
 */

#pragma once

#include <functional>
#include <memory>
#include <vector>
#include "os/rtos.hpp"
#include "protocols/lora_mesh/interfaces/i_superframe_service.hpp"
#include "types/protocols/lora_mesh/slot_allocation.hpp"
#include "types/protocols/lora_mesh/superframe.hpp"
#include "utils/logger.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/**
  * @brief Callback type for superframe events
  */
using SuperframeCallback =
    std::function<void(uint16_t current_slot, bool new_superframe)>;

/**
  * @brief Implementation of superframe management service with automatic updates
  * 
  * Manages superframe timing, slot transitions, synchronization and provides
  * automatic updates via an integrated RTOS task.
  */
class SuperframeService : public ISuperframeService {
   public:
    /**
      * @brief Constructor
      * 
      * @param total_slots Total number of slots in the superframe
      * @param slot_duration_ms Duration of each slot in milliseconds
      */
    explicit SuperframeService(
        uint16_t total_slots = DEFAULT_DISCOVERY_SLOT_COUNT,
        uint32_t slot_duration_ms = DEFAULT_DISCOVERY_TIMEOUT_MS);

    /**
      * @brief Virtual destructor
      * 
      * Cleans up task resources if active
      */
    virtual ~SuperframeService();

    // ISuperframeService interface implementation
    Result StartSuperframe() override;
    Result StopSuperframe() override;
    Result HandleNewSuperframe() override;

    bool IsSynchronized() const override;

    void SetSynchronized(bool synchronized) override;

    /**
     * @brief Get the total number of slots in the superframe
     * 
     * @return uint16_t Total number of slots
     */
    uint16_t GetTotalSlots() const { return total_slots_; }

    /**
     * @brief Update superframe configuration
     * 
     * @param total_slots Total number of slots in the superframe
     * @param slot_duration_ms Duration of each slot in milliseconds, if 0 it won't be updated
     * @param update_superframe Whether to update the superframe immediately
     * @return Result Success if configuration updated successfully
     */
    Result UpdateSuperframeConfig(uint16_t total_slots,
                                  uint32_t slot_duration_ms = 0,
                                  bool update_superframe = true) override;

    /**
     * @brief Get the slot duration in ms
     * 
     * @return uint32_t Slot duration ms
     */
    uint32_t GetSlotDuration() const override { return slot_duration_ms_; }

    /**
     * @brief Update superframe configuration with new slot duration
     * @param slot_duration_ms New duration for each slot in milliseconds, if 0 it won't be updated
     * @param update_superframe Whether to update the superframe immediately
     * @return Result Success if configuration updated successfully
     */
    Result UpdateSlotDuration(uint32_t slot_duration_ms,
                              bool update_superframe = true) {
        return UpdateSuperframeConfig(total_slots_, slot_duration_ms,
                                      update_superframe);
    }

    /**
     * @brief Start superframe discovery process
     * @return Result Success if discovery started successfully
     */
    Result StartSuperframeDiscovery();

    /**
      * @brief Get current superframe duration
      * 
      * @return uint32_t Duration of the superframe in milliseconds
      */
    uint32_t GetSuperframeDuration();

    /**
     * @brief Get Superframe Discovery timeout
     * 
     * @return uint32_t Discovery timeout in milliseconds
     */
    uint32_t GetDiscoveryTimeout();

    /**
      * @brief Get current slot number
      * 
      * @return uint16_t Current slot number
      */
    uint16_t GetCurrentSlot() const;

    /**
      * @brief Update superframe state and check for transitions
      * 
      * This method checks for slot transitions and new superframes,
      * and triggers the appropriate callbacks. It's called automatically
      * by the update task when enabled, but can also be called manually.
      * 
      * @return Result Success if update completed successfully
      */
    Result UpdateSuperframeState();

    /**
      * @brief Get current slot type based on current time
      * 
      * @param slot_table Slot allocation table
      * @return SlotAllocation::SlotType Current slot type
      */
    types::protocols::lora_mesh::SlotAllocation::SlotType GetCurrentSlotType(
        const std::vector<types::protocols::lora_mesh::SlotAllocation>&
            slot_table) const;

    /**
      * @brief Check if we're in a specific slot type
      * 
      * @param slot_type Slot type to check
      * @param slot_table Slot allocation table
      * @return bool True if currently in specified slot type
      */
    bool IsInSlotType(
        types::protocols::lora_mesh::SlotAllocation::SlotType slot_type,
        const std::vector<types::protocols::lora_mesh::SlotAllocation>&
            slot_table) const;

    /**
      * @brief Get time remaining in current slot
      * 
      * @return uint32_t Time remaining in milliseconds
      */
    uint32_t GetTimeRemainingInSlot();

    /**
      * @brief Get time since slot started
      * 
      * @return uint32_t Time elapsed in current slot in milliseconds
      */
    uint32_t GetTimeInSlot() const;

    /**
      * @brief Synchronize with external superframe timing
      * 
      * @param external_start_time Start time of the slot of the external superframe
      * @param external_slot Current slot of external superframe
      * @return Result Success if synchronized successfully
      */
    Result SynchronizeWith(uint32_t external_slot_start_time,
                           uint16_t external_slot) override;

    /**
      * @brief Set superframe callback
      * 
      * @param callback Function to call on superframe events
      */
    void SetSuperframeCallback(SuperframeCallback callback);

    /**
      * @brief Get superframe statistics
      * 
      * @return SuperframeStats Structure with timing statistics
      */
    struct SuperframeStats {
        uint32_t superframes_completed;
        uint32_t total_runtime_ms;
        uint32_t avg_slot_accuracy_ms;
        uint32_t sync_drift_ms;
        uint16_t current_slot;
        uint32_t time_in_current_slot_ms;
    };

    SuperframeStats GetSuperframeStats() const;

    /**
      * @brief Check if superframe needs resynchronization
      * 
      * @param drift_threshold_ms Maximum allowed drift in milliseconds
      * @return bool True if resync is needed
      */
    bool NeedsResynchronization(uint32_t drift_threshold_ms = 100) const;

    /**
      * @brief Get slot start time
      * 
      * @param slot_number Slot number to get start time for
      * @return uint32_t Start time of the slot
      */
    uint32_t GetSlotStartTime(uint16_t slot_number);

    /**
      * @brief Get slot end time
      * 
      * @param slot_number Slot number to get end time for
      * @return uint32_t End time of the slot
      */
    uint32_t GetSlotEndTime(uint16_t slot_number);

    /**
      * @brief Set update interval for task
      * 
      * @param interval_ms Interval in milliseconds between updates
      */
    void SetUpdateInterval(uint32_t interval_ms);

    /**
      * @brief Get current update interval
      * 
      * @return uint32_t Current update interval in milliseconds
      */
    uint32_t GetUpdateInterval() const { return update_interval_ms_; }

    /**
      * @brief Enable/disable automatic superframe advancement
      * 
      * @param enable True to enable automatic advancement
      */
    void SetAutoAdvance(bool enable) { auto_advance_ = enable; }

    /**
      * @brief Check if auto-advance is enabled
      * 
      * @return bool True if auto-advance is enabled
      */
    bool IsAutoAdvanceEnabled() const { return auto_advance_; }

    /**
      * @brief Set the node address for logging context
      * 
      * This ensures that the SuperframeService update task has the correct
      * node address for logging purposes.
      * 
      * @param node_address The node address to set
      */
    void SetNodeAddress(uint16_t node_address) { node_address_ = node_address; }

   private:
    /**
      * @brief Create RTOS task for automatic updates
      * 
      * @return bool True if task created successfully
      */
    bool CreateUpdateTask();

    /**
      * @brief Delete RTOS task
      */
    void DeleteUpdateTask();

    /**
      * @brief Static task function for update task
      * 
      * @param param Parameter pointing to service instance
      */
    static void UpdateTaskFunction(void* param);

    /**
      * @brief Check if we've entered a new superframe
      * 
      * @return bool True if new superframe detected
      */
    bool CheckForNewSuperframe();

    /**
      * @brief Update synchronization status
      */
    void UpdateSynchronizationStatus();

    /**
      * @brief Calculate timing accuracy
      */
    void UpdateTimingStats();

    /**
     * @brief Get the end time of the superframe
     * 
     * @return uint32_t End time of the superframe in milliseconds
     */
    uint32_t GetSuperframeEndTime();

    uint16_t total_slots_ = 0;  ///< Total number of slots in the superframe
    uint32_t slot_duration_ms_ = 0;  ///< Duration of each slot in milliseconds
    uint32_t superframe_start_time_ =
        0;                       ///< Start time of current superframe cycle
    uint16_t node_address_ = 0;  ///< Node address for logging context

    bool is_running_;
    bool is_synchronized_;
    bool auto_advance_;
    uint16_t last_slot_;
    uint32_t service_start_time_;
    SuperframeCallback superframe_callback_;

    // Task management
    uint32_t update_interval_ms_;
    os::TaskHandle_t update_task_handle_;

    // Statistics
    mutable uint32_t superframes_completed_;
    mutable uint32_t total_timing_error_ms_;
    mutable uint32_t timing_samples_;
    uint32_t last_sync_time_;
    uint32_t sync_drift_accumulator_;

    // Constants
    static constexpr uint32_t DEFAULT_UPDATE_INTERVAL_MS = 20;
    static constexpr uint32_t TASK_STACK_SIZE = 2048;
    static constexpr uint32_t TASK_PRIORITY = 3;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher