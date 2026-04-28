#include "os/rtos.hpp"

#include <cassert>

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO
#include "os/rtos_freertos.hpp"

namespace loramesher {
namespace os {

namespace {
RTOS* g_rtos = nullptr;
}

void RTOS::Init() {
    static RTOSFreeRTOS impl;
    g_rtos = &impl;
}

RTOS& RTOS::instance() {
    assert(g_rtos != nullptr && "RTOS::Init() must be called before instance()");
    return *g_rtos;
}

}  // namespace os
}  // namespace loramesher

#else
#include "os/rtos_mock.hpp"

namespace loramesher {
namespace os {

// Thread-local storage definition for node address cache
thread_local char RTOSMock::thread_local_node_address_[8] = {};

namespace {
RTOS* g_rtos = nullptr;
}

void RTOS::Init() {
    static RTOSMock impl;
    g_rtos = &impl;
}

RTOS& RTOS::instance() {
    assert(g_rtos != nullptr && "RTOS::Init() must be called before instance()");
    return *g_rtos;
}

}  // namespace os
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO
