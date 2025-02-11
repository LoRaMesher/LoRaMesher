#include <stdexcept>
#include "hardware_manager.hpp"

#include "arduino/arduino_hal.hpp"
#include "native/native_hal.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO
#include <SPI.h>
#elif defined(LORAMESHER_BUILD_NATIVE)
// TODO: Implement native platform initialization
// #include <wiringPi.h>
// #include <wiringPiSPI.h>
#endif

namespace loramesher {
namespace hal {

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

bool HardwareManager::initializePlatform() {
#ifdef LORAMESHER_BUILD_ARDUINO
    // Initialize Arduino hardware
    pinMode(kCsPin, OUTPUT);
    pinMode(kDio0Pin, INPUT);
    pinMode(kResetPin, OUTPUT);

    digitalWrite(kCsPin, HIGH);
    digitalWrite(kResetPin, HIGH);

    // Initialize SPI
    SPI.begin();
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

#elif defined(LORAMESHER_BUILD_NATIVE)
// TODO: Implement native platform initialization
#endif

    return true;
}

bool HardwareManager::initializeHalModules() {
    try {
#ifdef LORAMESHER_BUILD_ARDUINO
        // Create hal instance for Arduino
        hal_ = std::make_unique<ArduinoHal>();
#else
        // Create hal instance for native platform
        hal_ = std::make_unique<NativeHal>();
#endif

        return true;
    } catch (const std::exception& e) {
        // Log error or handle exception
        return false;
    }
}

}  // namespace hal
}  // namespace loramesher