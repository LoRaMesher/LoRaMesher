/**
 * @file superframe_service.cpp
 * @brief Implementation of superframe management service with automatic update task
 */

#include "superframe_service.hpp"
#include <algorithm>
#include <cstdlib>

#include "os/os_port.hpp"

namespace {
using namespace loramesher::types::protocols::lora_mesh;
}  // namespace

namespace loramesher {
namespace protocols {
namespace lora_mesh {

SuperframeService::SuperframeService(uint16_t total_slots,
                                     uint32_t slot_duration_ms)
    : total_slots_(total_slots),
      slot_duration_ms_(slot_duration_ms),
      is_running_(false),
      is_synchronized_(false),
      auto_advance_(true),
      last_slot_(0),
      service_start_time_(0),
      update_interval_ms_(DEFAULT_UPDATE_INTERVAL_MS),
      update_task_handle_(nullptr),
      notification_queue_(nullptr),
      superframes_completed_(0),
      total_timing_error_ms_(0),
      timing_samples_(0),
      last_sync_time_(0),
      sync_drift_accumulator_(0) {
    // Create notification queue for update task
    notification_queue_ = GetRTOS().CreateQueue(
        NOTIFICATION_QUEUE_SIZE, sizeof(SuperframeNotificationType));
    if (!notification_queue_) {
        LOG_ERROR("Failed to create superframe notification queue");
    }
}

SuperframeService::~SuperframeService() {
    // Make sure we stop the service and task
    LOG_DEBUG("SuperframeService destructor called");

    // First stop the superframe to set is_running_ = false
    // This will cause the UpdateTaskFunction loop to exit
    if (is_running_) {
        is_running_ = false;
        is_synchronized_ = false;

        // Wake up the task if it's blocked in ReceiveFromQueue by sending a notification
        // This ensures the task sees is_running_ = false quickly
        if (notification_queue_) {
            SuperframeNotificationType stop_notification =
                SuperframeNotificationType::STARTED;
            GetRTOS().SendToQueue(notification_queue_, &stop_notification, 0);
        }
    }

    // Now safely delete the task (it should exit its loop due to is_running_ = false)
    DeleteUpdateTask();

    // Clean up notification queue after task is safely deleted
    if (notification_queue_) {
        GetRTOS().DeleteQueue(notification_queue_);
        notification_queue_ = nullptr;
    }

    LOG_DEBUG("SuperframeService destructor completed");
}

Result SuperframeService::StartSuperframe() {
    if (is_running_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Superframe already running");
    }

    // Initialize timing
    uint32_t current_time = GetRTOS().getTickCount();
    // We cannot update this superframe since after the syncronization,
    // we are setting this to start, and we do not want to update our
    // superframe_start_time_.
    if (update_start_time_in_new_superframe) {
        superframe_start_time_ = current_time;
        is_synchronized_ = true;  // Consider synchronized when starting
        last_slot_ = 0xFFFF;
    }

    service_start_time_ = current_time;
    last_sync_time_ = current_time;

    is_running_ = true;

    // Create the update task
    if (update_task_handle_ != nullptr) {
        GetRTOS().ResumeTask(update_task_handle_);
    } else {
        bool task_created = CreateUpdateTask();
        if (!task_created) {
            LOG_ERROR("Failed to create superframe update task");
            return Result(LoraMesherErrorCode::kMemoryError,
                          "Failed to create superframe update task");
        }
    }

    LOG_INFO("Superframe service started - %d slots, %dms per slot",
             total_slots_, slot_duration_ms_);

    // Notify update task that superframe has started
    // NotifyUpdateTask(SuperframeNotificationType::STARTED);

    return Result::Success();
}

Result SuperframeService::StopSuperframe() {
    if (!is_running_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Superframe not running");
    }

    is_running_ = false;
    is_synchronized_ = false;

    // Stop the update task
    if (update_task_handle_) {
        // bool suspended = GetRTOS().SuspendTask(update_task_handle_);
        // if (!suspended) {
        //     LOG_ERROR(
        //         "Failed to suspend superframe update task, deleting task");
        //     // Delete the task if suspension failed
        //     GetRTOS().DeleteTask(update_task_handle_);
        //     update_task_handle_ = nullptr;
        // }

        // Delete the task. Suspending the task does not work correctly.
        // Maybe is something about the virtual time mode
        // Hours spend: 5h
        GetRTOS().DeleteTask(update_task_handle_);
        update_task_handle_ = nullptr;
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
    // We need this because, if it is not a manager this node should not update the superframe_start_time_ unless
    // is from a syncronization node.
    uint32_t current_time = GetRTOS().getTickCount();
    if (update_start_time_in_new_superframe) {
        superframe_start_time_ = current_time;
    } else {
        uint32_t superframe_end_time = GetSuperframeEndTime();
        if (current_time >= superframe_end_time) {
            superframe_start_time_ =
                superframe_start_time_ + GetSuperframeDuration();
        } else {
            LOG_WARNING(
                "New superframe requested before current one ended, "
                "keeping previous start time");
        }
    }

    last_slot_ = 0;

    // Update timing statistics
    UpdateTimingStats();

    LOG_DEBUG("Started superframe #%d", superframes_completed_);

    // Notify callback if set
    if (superframe_callback_) {
        superframe_callback_(0, true);  // Slot 0, new superframe
    }

    // Notify update task that a new frame has started
    NotifyUpdateTask(SuperframeNotificationType::NEW_FRAME);

    return Result::Success();
}

Result SuperframeService::DoNotUpdateStartTimeOnNewSuperframe() {
    update_start_time_in_new_superframe = false;
    return Result::Success();
}

bool SuperframeService::IsSynchronized() const {
    if (!is_running_) {
        return false;
    }

    // Check if we haven't lost sync due to timing drift
    // uint32_t current_time = GetRTOS().getTickCount();
    // uint32_t time_since_sync = current_time - last_sync_time_;

    // Consider synchronized if within reasonable drift limits
    // TODO: Make this configurable
    // const uint32_t MAX_SYNC_DRIFT_MS = 5000;  // 5 seconds
    // TODO: This works, however, we need to set the time_since_sync in other places.
    return is_synchronized_;  //&& (time_since_sync < MAX_SYNC_DRIFT_MS);
}

void SuperframeService::SetSynchronized(bool synchronized) {
    is_synchronized_ = synchronized;
    if (synchronized) {
        // Reset drift accumulator when synchronized
        sync_drift_accumulator_ = 0;
        last_sync_time_ = GetRTOS().getTickCount();
    }
    LOG_INFO("Superframe synchronization state set to %s",
             synchronized ? "true" : "false");
}

Result SuperframeService::UpdateSuperframeConfig(uint16_t total_slots,
                                                 uint32_t slot_duration_ms,
                                                 bool update_superframe) {
    // Create new superframe with updated parameters
    if (total_slots == 0) {
        return Result(LoraMesherErrorCode::kInvalidArgument,
                      "Total slots must be greater than 0");
    }

    total_slots_ = total_slots;

    if (slot_duration_ms != 0) {
        slot_duration_ms_ = slot_duration_ms;
    }

    // Notify update task of configuration changes (timing recalculation needed)
    NotifyUpdateTask(SuperframeNotificationType::CONFIG_CHANGED);

    if (update_superframe) {
        Result result = HandleNewSuperframe();
        if (!result.IsSuccess()) {
            LOG_ERROR("Failed to handle new superframe: %s",
                      result.GetErrorMessage().c_str());
            return result;
        }
    }

    return Result::Success();
}

Result SuperframeService::StartSuperframeDiscovery() {
    if (!is_running_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Superframe not running");
    }

    LOG_DEBUG("Starting superframe discovery");

    return UpdateSuperframeConfig(DEFAULT_DISCOVERY_SLOT_COUNT,
                                  DEFAULT_SLOT_DURATION_MS);
}

uint32_t SuperframeService::GetSuperframeDuration() const {
    return total_slots_ * slot_duration_ms_;
}

uint32_t SuperframeService::GetSuperframeEndTime() const {
    // Calculate end time based on start time and duration
    return superframe_start_time_ + GetSuperframeDuration();
}

uint32_t SuperframeService::GetDiscoveryTimeout() {
    if (!is_running_) {
        return 0;
    }

    // Discovery timeout is the duration of discovery slots
    // TODO: Calculate this correctly.
    return total_slots_ * slot_duration_ms_ * 3;
}

uint16_t SuperframeService::GetCurrentSlot() const {
    if (!is_running_) {
        return 0;
    }

    uint32_t current_time = GetRTOS().getTickCount();

    if (auto_advance_) {
        if (current_time < superframe_start_time_) {
            // LOG_DEBUG(
            //     "[TIMING_SLOT] Node 0x%04X: current_time (%u) < "
            //     "superframe_start_time_ (%u), returning slot 0",
            //     node_address_, current_time, superframe_start_time_);
            return 0;  // Before superframe started
        }

        uint32_t elapsed = current_time - superframe_start_time_;
        uint32_t slot_index = (elapsed / slot_duration_ms_) % total_slots_;

        // Log detailed slot calculation periodically (every 10th call to avoid spam)
        // static uint32_t call_counter = 0;
        // call_counter++;
        // if (call_counter % 100000 == 0) {
        //     LOG_DEBUG(
        //         "[TIMING_SLOT] Node 0x%04X slot calculation: current_time=%u, "
        //         "start_time=%u, elapsed=%u, slot_duration=%u, slot_index=%u",
        //         node_address_, current_time, superframe_start_time_, elapsed,
        //         slot_duration_ms_, slot_index);
        // }

        return static_cast<uint16_t>(slot_index);
    } else {
        // Calculate slot without wrapping around to new superframe
        if (current_time < superframe_start_time_) {
            return 0;
        }

        uint32_t elapsed_time = current_time - superframe_start_time_;
        uint32_t total_superframe_time = total_slots_ * slot_duration_ms_;

        if (elapsed_time >= total_superframe_time) {
            // Instead of wrapping, stay at the highest slot
            return total_slots_ - 1;
        }

        // Normal calculation within the superframe
        return (elapsed_time / slot_duration_ms_);
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

uint32_t SuperframeService::GetTimeRemainingInSlot() {
    if (!is_running_) {
        return 0;
    }

    uint32_t current_time = GetRTOS().getTickCount();
    uint16_t current_slot = GetCurrentSlot();
    uint32_t current_slot_end_time = GetSlotEndTime(current_slot);

    if (current_time >= current_slot_end_time) {
        return 0;
    }

    return current_slot_end_time - current_time;
}

uint32_t SuperframeService::GetTimeInSlot() const {
    if (!is_running_) {
        return 0;
    }

    uint32_t current_time = GetRTOS().getTickCount();
    uint16_t current_slot = GetCurrentSlot();
    uint32_t slot_start_time =
        superframe_start_time_ + (current_slot * slot_duration_ms_);

    if (current_time < slot_start_time) {
        return 0;
    }

    return current_time - slot_start_time;
}

Result SuperframeService::SynchronizeWith(uint32_t external_slot_start_time,
                                          uint16_t external_slot) {
    // Validate external_slot parameter
    if (external_slot >= total_slots_) {
        return Result(LoraMesherErrorCode::kInvalidArgument,
                      "External slot index exceeds total slots");
    }

    // Calculate when the external superframe started
    uint32_t slot_duration = slot_duration_ms_;
    uint32_t elapsed_time = external_slot * slot_duration;
    uint32_t current_time = GetRTOS().getTickCount();

    // Enhanced diagnostic logging for timing analysis - ALWAYS LOG
    LOG_INFO("[TIMING_SYNC] Node 0x%04X synchronization START:", node_address_);
    LOG_INFO("[TIMING_SYNC]   external_slot_start_time: %u ms",
             external_slot_start_time);
    LOG_INFO("[TIMING_SYNC]   external_slot: %u", external_slot);
    LOG_DEBUG("[TIMING_SYNC]   slot_duration: %u ms", slot_duration);
    LOG_DEBUG("[TIMING_SYNC]   elapsed_time: %llu ms", elapsed_time);
    LOG_DEBUG("[TIMING_SYNC]   current_time: %u ms", current_time);
    LOG_DEBUG("[TIMING_SYNC]   old_superframe_start: %u ms",
              superframe_start_time_);

    // Check for potential underflow
    if (elapsed_time > external_slot_start_time) {
        LOG_ERROR(
            "[TIMING_SYNC] Underflow detected: elapsed_time (%u) > "
            "external_slot_start_time (%u)",
            elapsed_time, external_slot_start_time);
        // Instead of failing, use the external slot start time as reference
        // This handles cases where slot calculation produces invalid timing
        LOG_INFO(
            "[TIMING_SYNC] Using external_slot_start_time as superframe "
            "reference");
        superframe_start_time_ = external_slot_start_time;
        is_synchronized_ = true;
        last_sync_time_ = current_time;
        last_slot_ = external_slot - 1;
        return Result::Success();
    }

    uint32_t superframe_duration = slot_duration * total_slots_;

    // Additional validation: ensure calculated time is reasonable
    // Superframe start should be in the past but not too far in the future
    if (external_slot_start_time > current_time + superframe_duration) {
        LOG_ERROR(
            "[TIMING_SYNC] Invalid calculated_superframe_start (%u) > "
            "reasonable future time (%u)",
            external_slot_start_time, current_time + superframe_duration);
        // Fall back to using current time as base
        LOG_INFO(
            "[TIMING_SYNC] Using current_time as superframe reference "
            "fallback");
        superframe_start_time_ = current_time;
        is_synchronized_ = true;
        last_sync_time_ = current_time;
        last_slot_ = external_slot - 1;
        return Result::Success();
    }

    // Adjust our superframe to match
    uint32_t old_start = superframe_start_time_;
    superframe_start_time_ = external_slot_start_time;

    LOG_INFO("[TIMING_SYNC]   calculated_superframe_start: %u ms",
             external_slot_start_time);
    LOG_INFO(
        "[TIMING_SYNC] Previous superframe start time: %dms, new start "
        "time: "
        "%dms",
        old_start, external_slot_start_time);

    // TODO: Old_start should be stored and used here. This won't work now because
    // Other nodes that are not network manager, would update this after the superframe_start_time_ has been updated
    // After a full superframe has compleated.
    // Calculate drift
    int32_t drift = static_cast<int32_t>(external_slot_start_time - old_start);
    sync_drift_accumulator_ += std::abs(drift);

    last_slot_ = external_slot - 1;

    is_synchronized_ = true;
    last_sync_time_ = current_time;

    LOG_INFO("Synchronized superframe with external timing (drift: %dms)",
             drift);

    // Notify update task of synchronization update (immediate recalculation needed)
    NotifyUpdateTask(SuperframeNotificationType::SYNC_UPDATED);

    return Result::Success();
}

void SuperframeService::SetSuperframeCallback(SuperframeCallback callback) {
    superframe_callback_ = callback;
}

SuperframeService::SuperframeStats SuperframeService::GetSuperframeStats()
    const {
    SuperframeStats stats = {};

    stats.superframes_completed = superframes_completed_;
    stats.current_slot = GetCurrentSlot();
    stats.time_in_current_slot_ms = GetTimeInSlot();

    if (is_running_) {
        uint32_t current_time = GetRTOS().getTickCount();
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

uint32_t SuperframeService::GetSlotStartTime(uint16_t slot_number) {
    return superframe_start_time_ + (slot_number * slot_duration_ms_);
}

uint32_t SuperframeService::GetSlotEndTime(uint16_t slot_number) {
    return GetSlotStartTime(slot_number) + slot_duration_ms_;
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

        // Detect new superframe
        if (current_slot == 0) {
            new_superframe = true;
            // Only handle new superframe if auto-advance is enabled
            if (auto_advance_) {
                HandleNewSuperframe();
            } else {
                // TODO: Stop until set to advance.
            }
        }

        // Call callback for slot transition
        if (superframe_callback_ && !new_superframe) {
            superframe_callback_(current_slot, new_superframe);
        }

        last_slot_ = current_slot;
    }

    // Update synchronization status
    UpdateSynchronizationStatus();

    return Result::Success();
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
    if (update_task_handle_) {
        return true;  // Already running
    }

    bool task_created = GetRTOS().CreateTask(
        UpdateTaskFunction, "SuperframeUpdate", TASK_STACK_SIZE, this,
        TASK_PRIORITY, &update_task_handle_);

    if (task_created) {
        LOG_DEBUG("Superframe update task created");
    } else {
        LOG_ERROR("Failed to create superframe update task");
    }

    return task_created;
}

void SuperframeService::DeleteUpdateTask() {
    if (update_task_handle_ != nullptr) {
        LOG_DEBUG("Deleting superframe update task handle: %p",
                  update_task_handle_);
        GetRTOS().DeleteTask(update_task_handle_);
        update_task_handle_ = nullptr;
        LOG_DEBUG("Superframe update task handle set to nullptr");
    } else {
        LOG_DEBUG("Superframe update task handle already null");
    }

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

    // Set node address for this task's logging context
    if (service->node_address_ != 0) {
        char address_str[8];
        snprintf(address_str, sizeof(address_str), "0x%04X",
                 service->node_address_);
        rtos.SetCurrentTaskNodeAddress(address_str);
    }

    // Task loop with queue-based efficient waiting
    SuperframeNotificationType notification;
    while (!rtos.ShouldStopOrPause() && service->is_running_) {
        // Double-check that the notification queue is still valid
        if (!service->notification_queue_) {
            LOG_DEBUG("UpdateTask: notification queue deleted, exiting");
            break;
        }

        // Calculate timeout until next meaningful event (slot transition or superframe end)
        uint32_t timeout_ms = service->CalculateNextEventTimeout();

        LOG_DEBUG("Next event timeout: %d ms", timeout_ms);

        // Wait for notification or timeout
        auto queue_result = rtos.ReceiveFromQueue(service->notification_queue_,
                                                  &notification, timeout_ms);

        if (queue_result == os::QueueResult::kOk) {
            // Received notification - handle based on notification type
            // LOG_DEBUG("UpdateTask received notification: %d",
            //           static_cast<int>(notification));

            switch (notification) {
                case SuperframeNotificationType::CONFIG_CHANGED:
                case SuperframeNotificationType::SYNC_UPDATED:
                    // These notifications only require timeout recalculation, not state updates
                    // LOG_DEBUG(
                    //     "Notification requires timeout recalculation only");
                    // Timeout will be recalculated in next loop iteration
                    break;

                case SuperframeNotificationType::STARTED:
                case SuperframeNotificationType::NEW_FRAME:
                    // New frame may require state update to handle slot transitions
                    // LOG_DEBUG(
                    //     "New frame notification - checking state transitions");
                    // service->UpdateSuperframeState();
                    break;
            }
        } else if (queue_result == os::QueueResult::kTimeout) {
            // Natural timeout - time for next slot/superframe transition
            LOG_DEBUG(
                "UpdateTask timeout - checking for slot/frame transitions");
            // Always update state on natural timeout (slot/frame boundary reached)
            service->UpdateSuperframeState();
        }
        // For other queue results (error, empty), continue with normal processing

        GetRTOS().YieldTask();
    }

    LOG_DEBUG("SuperframeService UpdateTaskFunction exiting naturally");
}

bool SuperframeService::CheckForNewSuperframe() {
    if (!is_running_) {
        return false;
    }

    uint32_t current_time = GetRTOS().getTickCount();
    uint32_t elapsed_time = current_time - superframe_start_time_;
    uint32_t superframe_duration = GetSuperframeDuration();
    if (elapsed_time >= superframe_duration) {
        return true;
    }

    return false;
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

    uint32_t current_time = GetRTOS().getTickCount();
    uint32_t expected_time = superframe_start_time_;

    // Calculate timing error
    uint32_t timing_error =
        abs(static_cast<int32_t>(current_time - expected_time));

    total_timing_error_ms_ += timing_error;
    timing_samples_++;
}

uint32_t SuperframeService::CalculateNextEventTimeout() const {
    if (!is_running_) {
        return MAX_DELAY;  // Not running, sleep indefinitely
    }

    uint32_t current_time = GetRTOS().getTickCount();

    // Calculate time until next slot boundary
    uint16_t current_slot = GetCurrentSlot();
    uint32_t current_slot_start =
        superframe_start_time_ + (current_slot * slot_duration_ms_);
    uint32_t next_slot_time = current_slot_start + slot_duration_ms_;

    // Calculate time until superframe end
    uint32_t superframe_end_time = GetSuperframeEndTime();

    // Choose the earlier of next slot or superframe end
    uint32_t next_event_time = std::min(next_slot_time, superframe_end_time);

    // Log all this data
    // LOG_DEBUG("Current time: %d ms", current_time);
    // LOG_DEBUG("Current slot: %d", current_slot);
    // LOG_DEBUG("Current slot start time: %d ms", current_slot_start);
    // LOG_DEBUG("Next slot time: %d ms", next_slot_time);
    // LOG_DEBUG("Next event time: %d ms", next_event_time);
    // LOG_DEBUG("Superframe end time: %d ms", superframe_end_time);

    // If we're past the event time, handle differently based on synchronization state
    if (current_time >= next_event_time) {
        // If we're past the superframe end and not updating start time in new superframe
        // (non-manager nodes), return a longer delay to avoid busy waiting
        if (current_time >= superframe_end_time &&
            !update_start_time_in_new_superframe) {
            // Wait for the next expected synchronization signal or use a reasonable timeout
            const uint32_t SYNC_WAIT_TIMEOUT_MS =
                1000;  // 1 second wait for sync
            return SYNC_WAIT_TIMEOUT_MS;
        }
        return 1;  // 1ms minimum to avoid busy waiting for other cases
    }

    uint32_t timeout = next_event_time - current_time;

    // Cap timeout to reasonable maximum and minimum to ensure periodic updates
    const uint32_t MAX_TIMEOUT_MS = 5000;  // 5 seconds maximum sleep
    const uint32_t MIN_TIMEOUT_MS = 20;    // 20ms minimum sleep
    return std::max(std::min(timeout, MAX_TIMEOUT_MS), MIN_TIMEOUT_MS);
}

void SuperframeService::NotifyUpdateTask(
    SuperframeNotificationType notification_type) {
    if (!notification_queue_) {
        LOG_DEBUG("Notification queue not initialized, skipping notification");
        return;
    }

    auto& rtos = GetRTOS();
    auto result = rtos.SendToQueue(notification_queue_, &notification_type,
                                   0);  // Non-blocking send

    if (result != os::QueueResult::kOk) {
        LOG_WARNING(
            "Failed to send notification to update task queue (type: %d)",
            static_cast<int>(notification_type));
    } else {
        LOG_DEBUG("Sent notification to update task (type: %d)",
                  static_cast<int>(notification_type));
    }
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher