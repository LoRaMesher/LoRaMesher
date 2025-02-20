// src/hardware/hal_factory.hpp
#pragma once

#include "hal.hpp"

#ifdef ARDUINO
#include "arduino/arduino_hal.hpp"
#else
#include "native/native_hal.hpp"
#endif

#include <memory>

namespace loramesher {
namespace hal {

/**
 * @brief Factory class for creating platform-specific HAL instances.
 */
class HalFactory {
   public:
    /**
     * @brief Create an appropriate HAL instance based on the current platform.
     * 
     * @return std::unique_ptr<IHal> A unique pointer to the created HAL instance.
     */
    static std::unique_ptr<IHal> createHal() {
#ifdef ARDUINO
        return std::unique_ptr<IHal>(new LoraMesherArduinoHal());
#else
        return std::unique_ptr<IHal>(new NativeHal());
#endif
    }
};

}  // namespace hal
}  // namespace loramesher