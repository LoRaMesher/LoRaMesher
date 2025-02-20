// src/config/system_config.hpp
#pragma once

#if defined(ARDUINO) && ARDUINO >= 100
// Arduino implementations
#include <Arduino.h>

#define LORAMESHER_BUILD_ARDUINO
#else
// Native implementation
#include <stdio.h>
#define LORAMESHER_BUILD_NATIVE
#define ESP_LOGI(tag, fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

#define DEBUG  // Uncomment to enable debug functionality