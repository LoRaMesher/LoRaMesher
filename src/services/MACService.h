#ifndef _LORAMESHER_WIFI_SERVICE_H
#define _LORAMESHER_WIFI_SERVICE_H

#include <Arduino.h>

// WiFi libraries
// #include <WiFi.h>

#include "ArduinoLog.h"

class MACService {
public:
    /**
     * @brief Initialize the WiFi Service
     *
     */
    static void init();

    /**
     * @brief Get the Local Address
     *
     * @return uint16_t Local Address
     */
    static uint16_t getLocalAddress();

private:

    /**
     * @brief Local Address
     *
     */
    static uint16_t localAddress;

};

#endif