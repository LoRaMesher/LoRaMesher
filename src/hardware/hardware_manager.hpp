// src/hardware/hardware_manager.hpp
#pragma once

#include <memory>

#include "config/system_config.hpp"
#include "hal.hpp"
#include "types/configurations/pin_configuration.hpp"
#include "types/configurations/radio_configuration.hpp"
#include "types/error_codes/result.hpp"
#include "types/radio/radio.hpp"

namespace loramesher {
namespace hardware {

/**
 * @brief Hardware Abstraction Layer for LoraMesher
 * 
 * This class provides platform-specific initialization and management
 * of hardware resources required by LoraMesher. It manages both pin
 * and radio configurations along with the HAL interface initialization.
 */
class HardwareManager {
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
     * @brief Get pointer to HAL interface
     * 
     * @return IHal* Pointer to HAL interface
     */
    hal::IHal* getHal() { return hal_.get(); }

    /**
     * @brief Send a message
     * 
     * @return Result send message result
     */
    Result SendMessage();

    /**
     * @brief Check if HAL is initialized
     * 
     * @return bool True if HAL is initialized
     */
    bool IsInitialized() const { return initialized_; }

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

    std::unique_ptr<hal::IHal> hal_;  ///< Pointer to HAL interface
    std::unique_ptr<radio::IRadio>
        radio_;  ///< Pointer to radio module interface

    bool initialized_ = false;  ///< Initialization status
    PinConfig pin_config_;      ///< Current pin configuration
    RadioConfig radio_config_;  ///< Current radio configuration
};

}  // namespace hardware
}  // namespace loramesher