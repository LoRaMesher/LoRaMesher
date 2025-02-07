#include "arduino_hal.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

namespace loramesher {
namespace hal {

uint32_t LoraMesherArduinoHal::millis() {
    return esp_timer_get_time() / 1000ULL;
}

void LoraMesherArduinoHal::delay(uint32_t ms) {
    delay(ms);
}

}  // namespace hal
}  // namespace loramesher

#endif