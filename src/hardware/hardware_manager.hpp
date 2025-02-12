// src/hardware/hardware_manager.hpp
#pragma once

#include <memory>

#include "build_options.hpp"
#include "hal.hpp"
#include "types/configurations/pin_configuration.hpp"
#include "types/configurations/radio_configuration.hpp"
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
        const PinConfig& pin_config = PinConfig::createDefault(),
        const RadioConfig& radio_config = RadioConfig::createDefaultSx1276());

    /**
     * @brief Destroy the Hardware Manager object
     */
    ~HardwareManager() = default;

    /**
     * @brief Initialize the HAL and hardware resources
     * 
     * @return bool True if initialization was successful
     */
    bool initialize();

    /**
     * @brief Get pointer to HAL interface
     * 
     * @return IHal* Pointer to HAL interface
     */
    hal::IHal* getHal() { return hal_.get(); }

    /**
     * @brief Check if HAL is initialized
     * 
     * @return bool True if HAL is initialized
     */
    bool isInitialized() const { return initialized_; }

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
     * @return bool True if update was successful
     */
    bool updatePinConfig(const PinConfig& pin_config);

    /**
     * @brief Update the radio configuration
     * 
     * @param radio_config New radio configuration
     * @return bool True if update was successful
     */
    bool updateRadioConfig(const RadioConfig& radio_config);

    // Delete copy constructor and assignment operator
    HardwareManager(const HardwareManager&) = delete;
    HardwareManager& operator=(const HardwareManager&) = delete;

    // Allow moving
    HardwareManager(HardwareManager&&) = default;
    HardwareManager& operator=(HardwareManager&&) = default;

   private:
    /**
     * @brief Initialize platform-specific hardware
     * 
     * @return bool True if platform initialization was successful
     */
    bool initializePlatform();

    /**
     * @brief Initialize hal modules
     * 
     * @return bool True if hal modules were initialized successfully
     */
    bool initializeHalModules();

    /**
     * @brief Initialize radio module
     * 
     * @return bool True if radio module was initialized successfully
     */
    bool initializeRadioModule();

    /**
     * @brief Validate current configuration
     * 
     * @return bool True if both pin and radio configurations are valid
     */
    bool validateConfiguration() const;

    std::unique_ptr<hal::IHal> hal_;  ///< Pointer to HAL interface
    std::unique_ptr<radio::IRadio>
        radio_;  ///< Pointer to radio module interface

    bool initialized_ = false;  ///< Initialization status
    PinConfig pin_config_;      ///< Current pin configuration
    RadioConfig radio_config_;  ///< Current radio configuration

#ifdef ARDUINO
    // Arduino-specific members
    static constexpr int kCsPin = 10;    ///< Default SPI CS pin
    static constexpr int kDio0Pin = 2;   ///< Default DIO0 pin
    static constexpr int kResetPin = 9;  ///< Default reset pin
#else
    // Native platform specific members
    static constexpr int kSpiChannel = 0;      ///< Default SPI channel
    static constexpr int kSpiSpeed = 8000000;  ///< Default SPI speed
#endif
};

}  // namespace hardware
}  // namespace loramesher