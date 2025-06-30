/**
 * @file os_port.hpp
 * @brief RTOS port selection
 */
#pragma once

#include "config/system_config.hpp"
#include "os/rtos.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO
#include "os/rtos_freertos.hpp"
#else
#include "os/rtos_mock.hpp"
#endif
