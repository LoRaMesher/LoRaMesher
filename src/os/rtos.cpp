#include "os/rtos.hpp"

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO
#include "os/rtos_freertos.hpp"

namespace loramesher {
namespace os {

RTOS& RTOS::instance() {
    static RTOSFreeRTOS instance;
    return instance;
}

}  // namespace os
}  // namespace loramesher

#else
#include "os/rtos_mock.hpp"

namespace loramesher {
namespace os {

RTOS& RTOS::instance() {
    static RTOSMock instance;
    return instance;
}

}  // namespace os
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO
