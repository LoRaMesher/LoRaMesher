/**
 * @file freertos_includes.hpp
 * @brief Platform-correct FreeRTOS header includes.
 *
 * ESP32 / ESP8266 Arduino cores bundle FreeRTOS and expose its headers under a
 * `freertos/` include prefix. Other Arduino cores (e.g. STM32duino) do not ship
 * FreeRTOS; it is provided by the separate STM32FreeRTOS library whose headers
 * sit at the include root. This header selects the correct set so the rest of
 * the OS layer can include FreeRTOS without knowing the platform.
 */
#pragma once

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32) || defined(ESP8266) || \
    defined(ARDUINO_ARCH_ESP8266)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#elif __has_include(<STM32FreeRTOS.h>)
#include <STM32FreeRTOS.h>
#else
#error \
    "LoRaMesher requires FreeRTOS. On non-ESP Arduino cores (e.g. STM32) install the STM32FreeRTOS library."
#endif

#endif  // LORAMESHER_BUILD_ARDUINO
