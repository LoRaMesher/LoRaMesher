#include "hardware_manager.hpp"

#include <stdexcept>

#include "arduino/arduino_hal.hpp"
#include "native/native_hal.hpp"

namespace loramesher {
namespace hardware {

HardwareManager::HardwareManager(const PinConfig& pin_config,
                                 const RadioConfig& radio_config) {
    pin_config_ = pin_config;
    radio_config_ = radio_config;
}

bool HardwareManager::initialize() {
    if (initialized_) {
        return true;
    }

    // Initialize platform-specific hardware
    if (!initializePlatform()) {
        return false;
    }

    // Initialize radio
    if (!initializeHalModules()) {
        return false;
    }

    initialized_ = true;
    return true;
}

bool HardwareManager::updatePinConfig(const PinConfig& pin_config) {
    if (!pin_config.isValid()) {
        return false;
    }

    pin_config_ = pin_config;
    return true;
}

bool HardwareManager::updateRadioConfig(const RadioConfig& radio_config) {
    if (!radio_config.isValid()) {
        return false;
    }

    radio_config_ = radio_config;
    return true;
}

bool HardwareManager::initializePlatform() {
#ifdef LORAMESHER_BUILD_ARDUINO
    // Initialize Arduino hardware
    // pinMode(kCsPin, OUTPUT);
    // pinMode(kDio0Pin, INPUT);
    // pinMode(kResetPin, OUTPUT);

    // digitalWrite(kCsPin, HIGH);
    // digitalWrite(kResetPin, HIGH);

    // // Initialize SPI
    // SPI.begin();
    // SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

#elif defined(LORAMESHER_BUILD_NATIVE)
// TODO: Implement native platform initialization
#endif

    return true;
}

bool HardwareManager::initializeHalModules() {
    try {
#ifdef LORAMESHER_BUILD_ARDUINO
        // Create hal instance for Arduino
        hal_ = std::make_unique<hal::LoraMesherArduinoHal>();
#else
        // Create hal instance for native platform
        hal_ = std::make_unique<hal::NativeHal>();
#endif

        return true;
    } catch (const std::exception& e) {
        // Log error or handle exception
        return false;
    }
}

bool HardwareManager::initializeRadioModule() {
// Create radio module
#ifdef LORAMESHER_BUILD_ARDUINO
    // radio_ = createRadio(kCsPin, kDio0Pin, kResetPin, SPI);
    // if (!radio_module_) {
    //     return false;
    // }

    // // Configure radio module
    // if (!radio_module_->configure(radio_config_)) {
    //     return false;
    // }
#endif

    return true;
}

bool HardwareManager::validateConfiguration() const {
    return pin_config_.isValid() && radio_config_.isValid();
}

}  // namespace hardware
}  // namespace loramesher