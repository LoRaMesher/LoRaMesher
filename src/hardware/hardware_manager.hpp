// src/hardware/hardware_manager.hpp
#pragma once

#include <memory>

#include "build_options.hpp"
#include "hal.hpp"

namespace loramesher {
namespace hal {

/**
 * @brief Hardware Abstraction Layer for LoraMesher
 * 
 * This class provides platform-specific initialization and management
 * of hardware resources required by LoraMesher. The HAL interface and
 * radio module are initialized based on the platform.
 */
class HardwareManager {
   public:
    /**
     * @brief Construct a new LoraMesher HAL object
     */
    HardwareManager() = default;

    /**
     * @brief Destroy the LoraMesher HAL object
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
     * @return IHardwareManager* Pointer to HAL interface
     */
    IHardwareManager* getHal() { return hal_.get(); }

    /**
     * @brief Check if HAL is initialized
     * 
     * @return bool True if HAL is initialized
     */
    bool isInitialized() const { return initialized_; }

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

    std::unique_ptr<IHardwareManager> hal_;  ///< Pointer to HAL interface
    bool initialized_ = false;               ///< Initialization status

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

}  // namespace hal
}  // namespace loramesher