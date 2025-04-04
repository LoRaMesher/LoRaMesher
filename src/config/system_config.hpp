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

// #define DEBUG  // Uncomment to enable debug functionality

#define LORAMESHER_LOG_LEVEL 4  // 0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR, 4=NO_LOG
//#define LOGGER_DISABLE_COLORS   // Disable color output
#define LOGGER_BUFFER_SIZE 128  // Adjust buffer size for your needs