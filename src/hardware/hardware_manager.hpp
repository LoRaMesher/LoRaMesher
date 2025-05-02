// src/hardware/hardware_manager.hpp
#pragma once

#include <memory>

#include "config/system_config.hpp"
#include "hal.hpp"
#include "types/configurations/pin_configuration.hpp"
#include "types/configurations/radio_configuration.hpp"
#include "types/error_codes/result.hpp"
#include "types/hardware/i_hardware_manager.hpp"
#include "types/radio/radio.hpp"
#include "types/radio/radio_event.hpp"

namespace loramesher {
namespace hardware {

/**
 * @brief Hardware Abstraction Layer for LoraMesher
 * 
 * This class provides platform-specific initialization and management
 * of hardware resources required by LoraMesher. It manages both pin
 * and radio configurations along with the HAL interface initialization.
 */
class HardwareManager : public IHardwareManager {
   public:
    /**
     * @brief Construct a new Hardware Manager object
     * 
     * @param pin_config Pin configuration for the radio module
     * @param radio_config Radio configuration parameters
     */
    explicit HardwareManager(
        const PinConfig& pin_config = PinConfig::CreateDefault(),
        const RadioConfig& radio_config = RadioConfig::CreateDefaultSx1276());

    /**
     * @brief Destroy the Hardware Manager object
     */
    ~HardwareManager() = default;

    /**
     * @brief Initialize the HAL and hardware resources
     * 
     * @return Result Success if initialization was successful, error code otherwise
     */
    Result Initialize();

    /**
     * @brief Start the hardware operation
     * 
     * @return Result Success if start was successful
     */
    Result Start();

    /**
     * @brief Stop the hardware operation
     * 
     * @return Result Success if stop was successful
     */
    Result Stop();

    /**
     * @brief Start receiving messages
     * 
     * @return Result Success if receiving was started successfully
     * 
     */
    Result StartReceive();

    /**
     * @brief Get pointer to HAL interface
     * 
     * @return IHal* Pointer to HAL interface
     */
    hal::IHal* getHal() { return hal_.get(); }

    /**
     * @brief Get pointer to radio module interface
     * 
     * @return IRadio* Pointer to radio module interface
     */
    radio::IRadio* getRadio() { return radio_.get(); }

    /**
     * @brief Set callback for radio events
     * 
     * @param callback Function to call for each radio event
     * @return Result Success if callback was set successfully
     */
    Result setActionReceive(EventCallback callback);

    /**
     * @brief Send a message
     * 
     * @param message The message to send
     * @return Result Success if message was sent
     */
    Result SendMessage(const BaseMessage& message);

    /**
     * @brief Get time on air for a given length and configuration
     * 
     * @param length Length of the message in bytes
     * @return uint32_t Time on air in milliseconds
     */
    uint32_t getTimeOnAir(uint8_t length);

    /**
     * @brief Check if HAL is initialized
     * 
     * @return bool True if HAL is initialized
     */
    bool IsInitialized() const { return is_initialized_; }

    /**
     * @brief Get the current pin configuration
     * 
     * @return const PinConfig& Reference to current pin configuration
     */
    const PinConfig& getPinConfig() const { return pin_config_; }

    /**
     * @brief Get the current radio configuration
     * 
     * @return const RadioConfig& Reference to current radio configuration
     */
    const RadioConfig& getRadioConfig() const { return radio_config_; }

    /**
     * @brief Update the pin configuration
     * 
     * @param pin_config New pin configuration
     * @return Result Success if update was successful, error code otherwise
     */
    Result setPinConfig(const PinConfig& pin_config);

    /**
     * @brief Set the current radio state
     * @param state Desired radio state
     * @return Result Success if state was changed successfully
     */
    Result setState(radio::RadioState state) override;

    /**
     * @brief Update the radio configuration
     * 
     * @param radio_config New radio configuration
     * @return Result Success if update was successful, error code otherwise
     */
    Result updateRadioConfig(const RadioConfig& radio_config);

    // Delete copy constructor and assignment operator
    HardwareManager(const HardwareManager&) = delete;
    HardwareManager& operator=(const HardwareManager&) = delete;

    // Allow moving
    HardwareManager(HardwareManager&&) = default;
    HardwareManager& operator=(HardwareManager&&) = default;

   private:
    /**
     * @brief Initialize hal modules
     * 
     * @return Result Success if hal modules were initialized successfully, error code otherwise
     */
    Result InitializeHalModules();

    /**
     * @brief Initialize radio module
     * 
     * @return Result Success if radio module was initialized successfully, error code otherwise
     */
    Result InitializeRadioModule();

    /**
     * @brief Validate current configuration
     * 
     * @return Result Success if both pin and radio configurations are valid, error code otherwise
     */
    Result ValidateConfiguration() const;

    // Hardware components
    std::unique_ptr<hal::IHal> hal_;  ///< Pointer to HAL interface
    std::unique_ptr<radio::IRadio>
        radio_;  ///< Pointer to radio module interface

    // Configuration
    PinConfig pin_config_;      ///< Current pin configuration
    RadioConfig radio_config_;  ///< Current radio configuration

    // State
    bool is_initialized_ = false;  ///< Whether hardware is initialized
    bool is_running_ = false;      ///< Whether hardware is running

    // Callback
    EventCallback event_callback_ = nullptr;  ///< Callback for radio events
};

}  // namespace hardware
}  // namespace loramesher