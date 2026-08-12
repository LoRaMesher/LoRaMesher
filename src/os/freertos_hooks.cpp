/**
 * @file freertos_hooks.cpp
 * @brief FreeRTOS application hooks (stack-overflow detection).
 *
 * Compiled only on Arduino-ESP32 builds. The hook function is declared
 * weakly inside the precompiled FreeRTOS library; defining a strong
 * symbol here overrides the default no-op when
 * configCHECK_FOR_STACK_OVERFLOW is non-zero in sdkconfig.
 */

#include "config/system_config.hpp"

#if defined(LORAMESHER_BUILD_ARDUINO) && \
    (defined(ESP32) || defined(ARDUINO_ARCH_ESP32))

#include <stdlib.h>

#include "os/freertos_includes.hpp"

extern "C" {
// ets_printf is in ROM, has no locking, and is safe to call from any
// context (ISR, panic, or with a corrupted task stack).
int ets_printf(const char* fmt, ...);
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask,
                                              char* pcTaskName) {
    (void)xTask;
    ets_printf("\n\n*** STACK OVERFLOW in task: %s ***\n\n",
               pcTaskName ? pcTaskName : "<unknown>");
    abort();
}

#endif  // LORAMESHER_BUILD_ARDUINO && ESP32
