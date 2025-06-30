/**
 * @file src/types/hardware/i_hardware_manager.hpp
 * @brief Interface hardware abstraction layer for radio communication
 */
#pragma once

#include "types/error_codes/result.hpp"
#include "types/radio/radio.hpp"
#include "types/radio/radio_state.hpp"

namespace loramesher {

namespace hardware {

/**
 * @brief Interface for hardware manager providing platform abstraction for LoRa radio control
 * 
 * This interface provides the primary abstraction layer between the LoRaMesher protocol 
 * and platform-specific hardware implementations. It enables cross-platform compatibility
 * by abstracting radio control, timing, and event handling.
 * 
 * ## Implementation Guidelines
 * 
 * ### Thread Safety Requirements
 * - All methods MUST be thread-safe as they may be called from multiple RTOS tasks
 * - Use platform-appropriate synchronization primitives (mutexes, semaphores)
 * - Ensure atomic access to radio state and configuration
 * 
 * ### State Management Pattern
 * Implementations should follow this state machine:
 * ```
 * [UNINITIALIZED] -> Initialize() -> [INITIALIZED] -> Start() -> [RUNNING]
 *                                                              <-> setState()
 *                                    [INITIALIZED] <- Stop() <- [RUNNING]
 * ```
 * 
 * ### Platform Implementations
 * - **ESP32**: Use Arduino framework with RadioLib integration
 * - **Desktop/Testing**: Mock implementation with virtual radio simulation
 * - **Custom Platform**: Implement radio driver interface for your hardware
 * 
 * ### Error Handling Pattern
 * - Return Result::Success for successful operations
 * - Return appropriate error codes for failures (hardware errors, invalid state, etc.)
 * - Log errors appropriately for debugging
 * - Never throw exceptions from interface methods
 * 
 * ### Event Callback Requirements
 * - Callbacks MUST be called from ISR-safe context or deferred to task context
 * - Event objects are moved to callback (unique_ptr ownership transfer)
 * - Callbacks should not block for extended periods
 * - Consider event queue for high-frequency events
 * 
 * ### Performance Considerations
 * - `getTimeOnAir()` should be optimized for frequent calls (consider caching)
 * - `SendMessage()` should be non-blocking or have configurable timeout
 * - State changes should be efficient (<1ms for typical operations)
 * 
 * @see HardwareManager for ESP32 implementation example
 * @see MockHardwareManager for testing implementation
 */
class IHardwareManager {
   public:
    /**
      * @brief Callback type for radio events
      */
    using EventCallback =
        std::function<void(std::unique_ptr<radio::RadioEvent>)>;

    /**
      * @brief Virtual destructor
      */
    virtual ~IHardwareManager() = default;

    /**
      * @brief Initialize the hardware and radio modules
      * 
      * Performs platform-specific hardware initialization including:
      * - SPI bus configuration (if applicable)
      * - GPIO pin setup for radio control
      * - Radio module reset and basic configuration
      * - Internal state initialization
      * 
      * Implementation Notes:
      * - Must be called before any other methods
      * - Should validate all hardware connections
      * - Initialize internal mutexes/synchronization primitives
      * - Set radio to a known state (typically IDLE)
      * 
      * @return Result::Success if initialization successful
      * @return Result::HardwareError if radio module not found/responsive
      * @return Result::InvalidState if already initialized
      * 
      * @note This method should be idempotent - safe to call multiple times
      */
    virtual Result Initialize() = 0;

    /**
      * @brief Start active radio operation and enable receive mode
      * 
      * Transitions the hardware from initialized state to active operation:
      * - Configure radio for receive mode
      * - Enable interrupt handling for radio events
      * - Start any platform-specific timer services
      * - Begin listening for incoming messages
      * 
      * Implementation Notes:
      * - Must call Initialize() successfully first
      * - Enable radio interrupts and configure callback routing
      * - Set appropriate receive timeout (if applicable)
      * - Initialize receive buffers
      * 
      * @return Result::Success if start successful
      * @return Result::InvalidState if not initialized or already started
      * @return Result::HardwareError if radio configuration failed
      */
    virtual Result Start() = 0;

    /**
      * @brief Stop radio operation and disable hardware
      * 
      * Gracefully shuts down radio operation:
      * - Disable radio interrupts
      * - Cancel any pending transmissions
      * - Clear receive buffers
      * - Set radio to sleep/idle mode for power savings
      * 
      * Implementation Notes:
      * - Should be safe to call from any state
      * - Clean up all pending operations
      * - Disable interrupt handlers to prevent callbacks
      * - Preserve configuration for potential restart
      * 
      * @return Result::Success if stop successful
      * @return Result::HardwareError if radio state change failed
      */
    virtual Result Stop() = 0;

    /**
      * @brief Register callback function for radio receive events
      * 
      * Sets the callback function that will be invoked when radio events occur.
      * Events include message reception, transmission completion, and errors.
      * 
      * Implementation Notes:
      * - Callback ownership: Interface takes ownership of callback function
      * - Thread safety: Callbacks may be called from ISR context - defer heavy work
      * - Event ownership: RadioEvent objects passed via unique_ptr (move semantics)
      * - Error handling: Callbacks should not throw exceptions
      * 
      * Threading Considerations:
      * - Callbacks must not block for extended periods
      * - Consider using event queue for complex processing
      * - Use appropriate synchronization if accessing shared data
      * 
      * @param callback Function to invoke for radio events (moved into interface)
      * @return Result::Success if callback registered successfully
      * @return Result::InvalidParameter if callback is null/invalid
      * 
      * @note Only one callback can be active at a time
      */
    virtual Result setActionReceive(EventCallback callback) = 0;

    /**
      * @brief Transmit a message via the radio
      * 
      * Serializes and transmits the provided message using the configured radio.
      * Handles message serialization, radio state management, and transmission.
      * 
      * Implementation Notes:
      * - Message serialization: Convert BaseMessage to byte array
      * - State management: Temporarily switch from RX to TX mode if needed
      * - Timing: Should be non-blocking or have reasonable timeout
      * - Error recovery: Return to receive mode after transmission
      * 
      * Transmission Flow:
      * 1. Serialize message to bytes
      * 2. Validate message size against radio limits
      * 3. Switch radio to transmit mode
      * 4. Send message data
      * 5. Wait for transmission completion
      * 6. Return to receive mode
      * 
      * @param message The message object to transmit
      * @return Result::Success if transmission initiated successfully
      * @return Result::MessageTooLarge if message exceeds radio payload limits
      * @return Result::InvalidState if radio not in operational state
      * @return Result::HardwareError if radio transmission failed
      * 
      * @note Method should handle radio state transitions automatically
      */
    virtual Result SendMessage(const BaseMessage& message) = 0;

    /**
     * @brief Calculate transmission time for given message length
     * 
     * Computes the time-on-air for a message of specified length using current
     * radio configuration (spreading factor, bandwidth, coding rate).
     * 
     * Implementation Notes:
     * - Use radio-specific calculation formulas (e.g., LoRa time-on-air formula)
     * - Cache results for commonly used lengths to improve performance
     * - Include protocol overhead (headers, CRC) in calculation
     * - Account for preamble and sync word transmission time
     * 
     * LoRa Time-on-Air Formula:
     * - Tpreamble = (Npreamble + 4.25) * Tsymbol
     * - Tpayload = Npayload * Tsymbol  
     * - Ttotal = Tpreamble + Tpayload
     * 
     * @param length Message payload length in bytes (excluding radio headers)
     * @return Time-on-air in milliseconds
     * 
     * @note This method should be optimized for frequent calls
     * @note Returned time should include all transmission overhead
     */
    virtual uint32_t getTimeOnAir(uint8_t length) = 0;

    /**
     * @brief Change the radio operational state
     * 
     * Controls the radio module state for power management and operational control.
     * Common states include RX (receive), TX (transmit), IDLE, and SLEEP.
     * 
     * Implementation Notes:
     * - State transitions should be atomic and thread-safe
     * - Validate state transition is legal for current radio state
     * - Update internal state tracking before returning success
     * - Handle timing requirements for state transitions
     * 
     * State Transition Requirements:
     * - IDLE <-> RX: Should be immediate (<1ms)
     * - IDLE <-> TX: Should be immediate (<1ms)  
     * - RX <-> TX: May require brief idle state
     * - Any -> SLEEP: Should disable radio and save power
     * - SLEEP -> Any: May require initialization delay
     * 
     * @param state Target radio state (RX, TX, IDLE, SLEEP)
     * @return Result::Success if state change successful
     * @return Result::InvalidParameter if state transition not supported
     * @return Result::HardwareError if radio state change failed
     * @return Result::InvalidState if current state doesn't allow transition
     */
    virtual Result setState(radio::RadioState state) = 0;
};

}  // namespace hardware

}  // namespace loramesher