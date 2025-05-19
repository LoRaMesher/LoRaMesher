/**
 * @file superframe_service.cpp
 * @brief Implementation of superframe management service with automatic update task
 */

#include "superframe_service.hpp"
#include <algorithm>

namespace {
using namespace loramesher::types::protocols::lora_mesh;
}  // namespace

namespace loramesher {
namespace protocols {
namespace lora_mesh {

SuperframeService::SuperframeService(
    std::shared_ptr<ITimeProvider> time_provider,
    const Superframe& initial_superframe)
    : time_provider_(time_provider),
      current_superframe_(initial_superframe),
      is_running_(false),
      is_synchronized_(false),
      auto_advance_(true),
      last_slot_(0),
      service_start_time_(0),
      task_enabled_(true),  // Task enabled by default
      task_running_(false),
      update_interval_ms_(DEFAULT_UPDATE_INTERVAL_MS),
      update_task_handle_(nullptr),
      superframes_completed_(0),
      total_timing_error_ms_(0),
      timing_samples_(0),
      last_sync_time_(0),
      sync_drift_accumulator_(0) {

    if (!time_provider_) {
        // Create default time provider if none provided
        time_provider_ = std::make_shared<TimeProvider>();
    }

    // Validate initial superframe
    Result validation = current_superframe_.Validate();
    if (!validation.IsSuccess()) {
        LOG_WARNING("Initial superframe invalid: %s",
                    validation.GetErrorMessage().c_str());
        // Use default superframe if invalid
        current_superframe_ = Superframe();
    }
}

SuperframeService::~SuperframeService() {
    // Make sure we stop the service and task
    StopSuperframe();

    if (task_running_) {
        DeleteUpdateTask();
    }
}

Result SuperframeService::StartSuperframe() {
    if (is_running_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Superframe already running");
    }

    // Initialize timing
    uint32_t current_time = time_provider_->GetCurrentTime();
    current_superframe_.superframe_start_time = current_time;
    service_start_time_ = current_time;
    last_sync_time_ = current_time;

    is_running_ = true;
    is_synchronized_ = true;  // Consider synchronized when starting
    last_slot_ = 0;

    // Reset statistics
    superframes_completed_ = 0;
    total_timing_error_ms_ = 0;
    timing_samples_ = 0;
    sync_drift_accumulator_ = 0;

    // Create the update task if enabled
    if (task_enabled_ && !task_running_) {
        bool task_created = CreateUpdateTask();
        if (!task_created) {
            LOG_ERROR("Failed to create superframe update task");
            return Result(LoraMesherErrorCode::kMemoryError,
                          "Failed to create superframe update task");
        }
    }

    LOG_INFO("Superframe service started - %d slots, %dms per slot",
             current_superframe_.total_slots,
             current_superframe_.slot_duration_ms);

    return Result::Success();
}

Result SuperframeService::StopSuperframe() {
    if (!is_running_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Superframe not running");
    }

    is_running_ = false;
    is_synchronized_ = false;

    // Stop the update task if running
    if (task_running_) {
        DeleteUpdateTask();
    }

    LOG_INFO("Superframe service stopped after %d completed superframes",
             superframes_completed_);

    return Result::Success();
}

Result SuperframeService::HandleNewSuperframe() {
    if (!is_running_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Superframe not running");
    }

    superframes_completed_++;

    // Update superframe start time
    uint32_t current_time = time_provider_->GetCurrentTime();
    current_superframe_.superframe_start_time = current_time;
    last_slot_ = 0;

    // Update timing statistics
    UpdateTimingStats();

    // Notify callback if set
    if (superframe_callback_) {
        superframe_callback_(0, true);  // Slot 0, new superframe
    }

    LOG_DEBUG("Started superframe #%d", superframes_completed_);

    return Result::Success();
}

bool SuperframeService::IsSynchronized() const {
    if (!is_running_) {
        return false;
    }

    // Check if we haven't lost sync due to timing drift
    uint32_t current_time = time_provider_->GetCurrentTime();
    uint32_t time_since_sync = current_time - last_sync_time_;

    // Consider synchronized if within reasonable drift limits
    // TODO: Make this configurable
    const uint32_t MAX_SYNC_DRIFT_MS = 5000;  // 5 seconds
    return is_synchronized_ && (time_since_sync < MAX_SYNC_DRIFT_MS);
}

const Superframe& SuperframeService::GetSuperframeConfig() const {
    return current_superframe_;
}

Result SuperframeService::UpdateSuperframeConfig(uint16_t total_slots,
                                                 uint16_t data_slots,
                                                 uint16_t discovery_slots,
                                                 uint16_t control_slots,
                                                 uint32_t slot_duration_ms) {
    // Create new superframe with updated parameters
    Superframe new_superframe(total_slots, data_slots, discovery_slots,
                              control_slots, slot_duration_ms,
                              current_superframe_.superframe_start_time);

    return UpdateSuperframeConfig(new_superframe);
}

uint16_t SuperframeService::GetCurrentSlot() const {
    if (!is_running_) {
        return 0;
    }

    uint32_t current_time = time_provider_->GetCurrentTime();

    if (auto_advance_) {
        // Normal calculation - allows slot to wrap to 0
        return current_superframe_.GetCurrentSlot(current_time);
    } else {
        // Calculate slot without wrapping around to new superframe
        if (current_time < current_superframe_.superframe_start_time) {
            return 0;
        }

        uint32_t elapsed_time =
            current_time - current_superframe_.superframe_start_time;
        uint32_t total_superframe_time = current_superframe_.total_slots *
                                         current_superframe_.slot_duration_ms;

        if (elapsed_time >= total_superframe_time) {
            // Instead of wrapping, stay at the highest slot
            return current_superframe_.total_slots - 1;
        }

        // Normal calculation within the superframe
        return (elapsed_time / current_superframe_.slot_duration_ms);
    }
}

SlotAllocation::SlotType SuperframeService::GetCurrentSlotType(
    const std::vector<SlotAllocation>& slot_table) const {

    uint16_t current_slot = GetCurrentSlot();

    // Find the slot type in the allocation table
    for (const auto& allocation : slot_table) {
        if (allocation.slot_number == current_slot) {
            return allocation.type;
        }
    }

    // Default to sleep if not found
    return SlotAllocation::SlotType::SLEEP;
}

bool SuperframeService::IsInSlotType(
    SlotAllocation::SlotType slot_type,
    const std::vector<SlotAllocation>& slot_table) const {
    return GetCurrentSlotType(slot_table) == slot_type;
}

uint32_t SuperframeService::GetTimeRemainingInSlot() const {
    if (!is_running_) {
        return 0;
    }

    uint32_t current_time = time_provider_->GetCurrentTime();
    uint16_t current_slot = GetCurrentSlot();
    uint32_t slot_end_time = current_superframe_.GetSlotEndTime(current_slot);

    if (current_time >= slot_end_time) {
        return 0;
    }

    return slot_end_time - current_time;
}

uint32_t SuperframeService::GetTimeInSlot() const {
    if (!is_running_) {
        return 0;
    }

    uint32_t current_time = time_provider_->GetCurrentTime();
    uint16_t current_slot = GetCurrentSlot();
    uint32_t slot_start_time =
        current_superframe_.GetSlotStartTime(current_slot);

    if (current_time < slot_start_time) {
        return 0;
    }

    return current_time - slot_start_time;
}

Result SuperframeService::AdvanceToNextSuperframe() {
    if (!is_running_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Superframe not running");
    }

    // Force advance to next superframe
    current_superframe_.AdvanceToNextSuperframe(
        time_provider_->GetCurrentTime());

    return HandleNewSuperframe();
}

Result SuperframeService::SynchronizeWith(uint32_t external_slot_start_time,
                                          uint16_t external_slot) {
    if (!is_running_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Superframe not running");
    }

    // Calculate when the external superframe started
    uint32_t slot_duration = current_superframe_.slot_duration_ms;
    uint32_t calculated_superframe_start =
        external_slot_start_time - (external_slot * slot_duration);

    // Adjust our superframe to match
    uint32_t old_start = current_superframe_.superframe_start_time;
    current_superframe_.superframe_start_time = calculated_superframe_start;

    LOG_DEBUG("Previous superframe start time: %dms, new start time: %dms",
              old_start, calculated_superframe_start);

    // Calculate drift
    int32_t drift =
        static_cast<int32_t>(calculated_superframe_start - old_start);
    sync_drift_accumulator_ += abs(drift);

    is_synchronized_ = true;
    last_sync_time_ = time_provider_->GetCurrentTime();

    LOG_INFO("Synchronized superframe with external timing (drift: %dms)",
             drift);

    return Result::Success();
}

void SuperframeService::SetSuperframeCallback(SuperframeCallback callback) {
    superframe_callback_ = callback;
}

Result SuperframeService::UpdateSuperframeConfig(
    const Superframe& new_superframe) {
    // Validate new superframe
    Result validation = new_superframe.Validate();
    if (!validation.IsSuccess()) {
        return validation;
    }

    // If running, the change takes effect at the next superframe boundary
    if (is_running_) {
        LOG_INFO("Superframe config will change at next superframe boundary");
    }

    // Update configuration (preserve current start time if running)
    current_superframe_ = new_superframe;
    if (is_running_) {
        // Don't change the start time if already running
        // The new config will apply at the next natural superframe transition
    }

    LOG_INFO(
        "Updated superframe config: %d slots (%d data, %d discovery, %d "
        "control), %dms/slot",
        current_superframe_.total_slots, current_superframe_.data_slots,
        current_superframe_.discovery_slots, current_superframe_.control_slots,
        current_superframe_.slot_duration_ms);

    return Result::Success();
}

SuperframeService::SuperframeStats SuperframeService::GetSuperframeStats()
    const {
    SuperframeStats stats = {};

    stats.superframes_completed = superframes_completed_;
    stats.current_slot = GetCurrentSlot();
    stats.time_in_current_slot_ms = GetTimeInSlot();

    if (is_running_) {
        uint32_t current_time = time_provider_->GetCurrentTime();
        stats.total_runtime_ms = current_time - service_start_time_;
        stats.sync_drift_ms =
            sync_drift_accumulator_ / std::max(1u, superframes_completed_);
    }

    if (timing_samples_ > 0) {
        stats.avg_slot_accuracy_ms = total_timing_error_ms_ / timing_samples_;
    }

    return stats;
}

bool SuperframeService::NeedsResynchronization(
    uint32_t drift_threshold_ms) const {
    if (!is_running_ || !is_synchronized_) {
        return true;
    }

    // Check accumulated drift
    uint32_t avg_drift =
        (superframes_completed_ > 0)
            ? (sync_drift_accumulator_ / superframes_completed_)
            : 0;

    return avg_drift > drift_threshold_ms;
}

uint32_t SuperframeService::GetSlotStartTime(uint16_t slot_number) const {
    return current_superframe_.GetSlotStartTime(slot_number);
}

uint32_t SuperframeService::GetSlotEndTime(uint16_t slot_number) const {
    return current_superframe_.GetSlotEndTime(slot_number);
}

Result SuperframeService::UpdateSuperframeState() {
    if (!is_running_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Superframe not running");
    }

    uint16_t current_slot = GetCurrentSlot();

    // Check for slot transition
    if (current_slot != last_slot_) {
        // Check if we've wrapped around (new superframe)
        bool new_superframe = false;

        // Detect new superframe by checking if we wrapped from high to low slot
        if (current_slot < last_slot_) {
            new_superframe = true;
            // Only handle new superframe if auto-advance is enabled
            if (auto_advance_) {
                HandleNewSuperframe();
            } else {
                LOG_DEBUG("New superframe detected but auto-advance disabled");
                // Reset the superframe start time to the current time
            }
        }

        // Call callback for slot transition
        if (superframe_callback_) {
            superframe_callback_(current_slot, new_superframe);
        }

        last_slot_ = current_slot;

        LOG_DEBUG("Slot transition: slot %d%s", current_slot,
                  new_superframe ? " (new superframe)" : "");
    }

    // Check if auto-advance is enabled and we need to handle a new superframe
    if (auto_advance_ && CheckForNewSuperframe()) {
        HandleNewSuperframe();
    }

    // Update synchronization status
    UpdateSynchronizationStatus();

    return Result::Success();
}

void SuperframeService::SetTaskEnabled(bool enable) {
    task_enabled_ = enable;

    // If enabling and not already running, create task
    if (task_enabled_ && !task_running_ && is_running_) {
        CreateUpdateTask();
    }

    // If disabling and running, delete task
    if (!task_enabled_ && task_running_) {
        DeleteUpdateTask();
    }
}

void SuperframeService::SetUpdateInterval(uint32_t interval_ms) {
    // Ensure reasonable interval
    if (interval_ms < 10) {
        interval_ms = 10;  // Minimum 10ms to avoid excessive CPU usage
    } else if (interval_ms > 1000) {
        interval_ms = 1000;  // Maximum 1s to ensure reasonable responsiveness
    }

    update_interval_ms_ = interval_ms;
}

bool SuperframeService::CreateUpdateTask() {
    if (task_running_) {
        return true;  // Already running
    }

    bool task_created = GetRTOS().CreateTask(
        UpdateTaskFunction, "SuperframeUpdate", TASK_STACK_SIZE, this,
        TASK_PRIORITY, &update_task_handle_);

    if (task_created) {
        task_running_ = true;
        LOG_DEBUG("Superframe update task created");
    } else {
        LOG_ERROR("Failed to create superframe update task");
    }

    return task_created;
}

void SuperframeService::DeleteUpdateTask() {
    if (!task_running_) {
        return;  // Not running
    }

    GetRTOS().DeleteTask(update_task_handle_);
    update_task_handle_ = nullptr;
    task_running_ = false;

    LOG_DEBUG("Superframe update task deleted");
}

void SuperframeService::UpdateTaskFunction(void* param) {
    // Get service instance
    SuperframeService* service = static_cast<SuperframeService*>(param);

    if (!service) {
        return;
    }

    // RTOS instance
    auto& rtos = GetRTOS();

    // Task loop
    while (service->is_running_ && service->task_enabled_ &&
           !rtos.ShouldStopOrPause()) {

        // Update superframe state
        service->UpdateSuperframeState();

        // Sleep for update interval
        rtos.delay(service->update_interval_ms_);
    }

    // Task cleanup
    service->task_running_ = false;
    rtos.DeleteTask(nullptr);  // Delete self
}

bool SuperframeService::CheckForNewSuperframe() {
    if (!is_running_) {
        return false;
    }

    uint32_t current_time = time_provider_->GetCurrentTime();
    return current_superframe_.IsNewSuperframe(current_time);
}

void SuperframeService::UpdateSynchronizationStatus() {
    // TODO: This could be enhanced to check timing accuracy
    // and automatically adjust synchronization status
}

void SuperframeService::UpdateTimingStats() {
    // Track timing accuracy for statistics
    if (!is_running_) {
        return;
    }

    uint32_t current_time = time_provider_->GetCurrentTime();
    uint32_t expected_time = current_superframe_.superframe_start_time;

    // Calculate timing error
    uint32_t timing_error =
        abs(static_cast<int32_t>(current_time - expected_time));

    total_timing_error_ms_ += timing_error;
    timing_samples_++;
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher