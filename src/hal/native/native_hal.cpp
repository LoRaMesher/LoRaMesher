#include "native_hal.hpp"

#ifdef LORAMESHER_BUILD_NATIVE

namespace loramesher {
namespace hal {

uint32_t NativeHal::millis() {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch());
    return duration.count();
}

void NativeHal::delay(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // namespace hal
}  // namespace loramesher

#endif
