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

bool HardwareManager::Initialize() {
    if (initialized_) {
        return true;
    }

    // Initialize platform-specific hardware
    if (!InitializePlatform()) {
        return false;
    }

    // Initialize radio
    if (!InitializeHalModules()) {
        return false;
    }

    initialized_ = true;
    return true;
}

bool HardwareManager::setPinConfig(const PinConfig& pin_config) {
    if (!pin_config.IsValid()) {
        return false;
    }

    pin_config_ = pin_config;
    return true;
}

bool HardwareManager::updateRadioConfig(const RadioConfig& radio_config) {
    if (!radio_config.IsValid()) {
        return false;
    }

    radio_config_ = radio_config;
    return true;
}

bool HardwareManager::InitializePlatform() {
#ifdef LORAMESHER_BUILD_ARDUINO
    // Initialize Arduino hardware
    // pinMode(kCsPin, OUTPUT);
    // pinMode(kDio0Pin, INPUT);
    // pinMode(kResetPin, OUTPUT);

    // digitalWrite(kCsPin, HIGH);
    // digitalWrite(kResetPin, HIGH);

    // // Initialize SPI
    // SPI.Begin();
    // SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

#elif defined(LORAMESHER_BUILD_NATIVE)
// TODO: Implement native platform initialization
#endif

    return true;
}

bool HardwareManager::InitializeHalModules() {
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

bool HardwareManager::InitializeRadioModule() {
// Create radio module
#ifdef LORAMESHER_BUILD_ARDUINO
    // radio_ = CreateRadio(kCsPin, kDio0Pin, kResetPin, SPI);
    // if (!radio_module_) {
    //     return false;
    // }

    // // Configure radio module
    // if (!radio_module_->Configure(radio_config_)) {
    //     return false;
    // }
#endif

    return true;
}

bool HardwareManager::ValidateConfiguration() const {
    return pin_config_.IsValid() && radio_config_.IsValid();
}

}  // namespace hardware
}  // namespace loramesher