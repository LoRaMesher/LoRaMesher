/**
 * @file src/os/rtos.hpp
 * @brief Common RTOS interface
 */
#pragma once

#include <cstdint>
#include <functional>

#include "config/system_config.hpp"

namespace loramesher {
namespace os {

#ifdef LORAMESHER_BUILD_ARDUINO
#define ISR_ATTR void ICACHE_RAM_ATTR
#define MAX_DELAY portMAX_DELAY
using TaskHandle_t = TaskHandle_t;
using QueueHandle_t = QueueHandle_t;
using TaskFunction_t = TaskFunction_t;
#else
#define ISR_ATTR void
#define MAX_DELAY 0xFFFFFFFF
using TaskHandle_t = void*;
using QueueHandle_t = void*;
using TaskFunction_t = std::function<void(void*)>;
#endif

/**
 * @brief Queue operations result
 */
enum class QueueResult {
    kOk,       ///< Operation successful
    kTimeout,  ///< Operation timed out
    kFull,     ///< Queue is full
    kEmpty,    ///< Queue is empty
    kError     ///< Generic error
};

/**
 * @brief Task state enumeration
 */
enum class TaskState {
    kRunning,    ///< Task is running
    kReady,      ///< Task is ready to run
    kBlocked,    ///< Task is blocked
    kSuspended,  ///< Task is suspended
    kDeleted,    ///< Task is deleted
    kUnknown     ///< Task state is unknown
};

/**
 * @brief Task statistics structure
 */
struct TaskStats {
    std::string name;         ///< Task name
    TaskState state;          ///< Task state
    uint32_t stackWatermark;  ///< Minimum stack watermark
    uint32_t runtime;         ///< Task runtime in milliseconds
};

/**
 * @class RTOS
 * @brief Abstract RTOS interface
 */
class RTOS {
   public:
    /**
     * @brief Create a new task
     */
    virtual bool CreateTask(TaskFunction_t taskFunction, const char* name,
                            uint32_t stackSize, void* parameters,
                            uint32_t priority, TaskHandle_t* taskHandle) = 0;

    /**
     * @brief Delete a task
     * @param taskHandle Handle of task to delete, nullptr for current task
     */
    virtual void DeleteTask(TaskHandle_t taskHandle) = 0;

    /**
     * @brief Suspend a task
     * @param taskHandle Handle of task to suspend, nullptr for current task
     */
    virtual void SuspendTask(TaskHandle_t taskHandle) = 0;

    /**
     * @brief Resume a task
     * @param taskHandle Handle of task to resume
     */
    virtual void ResumeTask(TaskHandle_t taskHandle) = 0;

    /**
     * @brief Create a queue
     * @param length Maximum number of items queue can hold
     * @param itemSize Size of each item in bytes
     * @return Queue handle or nullptr if creation failed
     */
    virtual QueueHandle_t CreateQueue(uint32_t length, uint32_t itemSize) = 0;

    /**
     * @brief Delete a queue
     * @param queue Handle of queue to delete
     */
    virtual void DeleteQueue(QueueHandle_t queue) = 0;

    /**
     * @brief Send item to queue
     * @param queue Queue handle
     * @param item Pointer to item to send
     * @param timeout Timeout in milliseconds, 0 for no wait
     * @return Operation result
     */
    virtual QueueResult SendToQueue(QueueHandle_t queue, const void* item,
                                    uint32_t timeout) = 0;

    /**
     * @brief Send item to queue from ISR
     * @param queue Queue handle
     * @param item Pointer to item to send
     * @param timeout Timeout in milliseconds, 0 for no wait
     * @return Operation result
     */
    virtual QueueResult SendToQueueISR(QueueHandle_t queue,
                                       const void* item) = 0;

    /**
     * @brief Receive item from queue
     * @param queue Queue handle
     * @param buffer Buffer to store received item
     * @param timeout Timeout in milliseconds, 0 for no wait
     * @return Operation result
     */
    virtual QueueResult ReceiveFromQueue(QueueHandle_t queue, void* buffer,
                                         uint32_t timeout) = 0;

    /**
     * @brief Get items available in queue
     * @param queue Queue handle
     * @return Number of items in queue
     */
    virtual uint32_t getQueueMessagesWaiting(QueueHandle_t queue) = 0;

    /**
     * @brief Delay task execution
     * @param ms Delay in milliseconds
     */
    virtual void delay(uint32_t ms) = 0;

    /**
     * @brief Get current tick count
     * @return Current tick count
     */
    virtual uint32_t getTickCount() = 0;

    /**
     * @brief Start the RTOS scheduler
     */
    virtual void StartScheduler() = 0;

    /**
     * @brief Get stack high watermark for a task
     * @param taskHandle Task handle
     * @return Stack watermark in bytes
     */
    virtual uint32_t getTaskStackWatermark(TaskHandle_t taskHandle) = 0;

    /**
     * @brief Get current state of a task
     * @param taskHandle Task handle
     * @return Current task state
     */
    virtual TaskState getTaskState(TaskHandle_t taskHandle) = 0;

    /**
     * @brief Get statistics for all tasks
     * @return Vector of task statistics
     */
    virtual std::vector<TaskStats> getSystemTaskStats() = 0;

    /**
     * @brief Get singleton instance
     */
    static RTOS& instance();

    /**
     * @brief Convert task state to string representation
     * 
     * @param state Task state to convert
     * @return const char* String representation of the state
     */
    static const char* getTaskStateString(TaskState state) {
        switch (state) {
            case TaskState::kRunning:
                return "Running";
            case TaskState::kReady:
                return "Ready";
            case TaskState::kBlocked:
                return "Blocked";
            case TaskState::kSuspended:
                return "Suspended";
            case TaskState::kDeleted:
                return "Deleted";
            default:
                return "Unknown";
        }
    }

    /**
     * @brief Register an ISR callback
     * @param callback Static function pointer to the ISR
     * @param pin Pin number to attach the ISR to (default: 0)
     * @param mode Interrupt mode (default: 0)
     * @return Handle to the registered ISR
     */
    virtual void* RegisterISR(void (*callback)(), uint8_t pin = 0,
                              int mode = 0) = 0;

    /**
     * @brief Template method to register class member ISR
     * @tparam T Class type
     * @param instance Pointer to class instance
     * @param member Pointer to member function
     * @return Handle to the registered ISR
     */
    template <typename T>
    void* registerClassISR(T* instance, void (T::*member)()) {
        // TODO: Implement class member ISR registration
        // // Store the instance and member function
        // static T* storedInstance = instance;
        // static void (T::*storedMember)() = member;

        // // Create a static wrapper function
        // static auto wrapper = []() ISR_ATTR {
        //     (storedInstance->*storedMember)();
        // };

        // return RegisterISR(wrapper);
        return nullptr;
    }

   protected:
    RTOS() = default;
    virtual ~RTOS() = default;
};

}  // namespace os
}  // namespace loramesher