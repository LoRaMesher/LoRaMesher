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
 * @brief Interface for hardware manager
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
      * @brief Initialize the hardware
      * 
      * @return Result Success if initialization was successful
      */
    virtual Result Initialize() = 0;

    /**
      * @brief Start the hardware operation
      * 
      * @return Result Success if start was successful
      */
    virtual Result Start() = 0;

    /**
      * @brief Stop the hardware operation
      * 
      * @return Result Success if stop was successful
      */
    virtual Result Stop() = 0;

    /**
      * @brief Set callback for radio events
      * 
      * @param callback Function to call for each radio event
      * @return Result Success if callback was set successfully
      */
    virtual Result setActionReceive(EventCallback callback) = 0;

    /**
      * @brief Send a message
      * 
      * @param message The message to send
      * @return Result Success if message was sent successfully
      */
    virtual Result SendMessage(const BaseMessage& message) = 0;

    /**
     * @brief Get time on air for a given length and configuration
     * 
     * @param length Length of the message in bytes
     * @return uint32_t Time on air in milliseconds
     */
    virtual uint32_t getTimeOnAir(uint8_t length) = 0;

    /**
     * @brief Set the current radio state
     * @param state Desired radio state
     * @return Result Success if state was changed successfully
     */
    virtual Result setState(radio::RadioState state) = 0;
};

}  // namespace hardware

}  // namespace loramesher