#ifndef _LORAMESHER_WIFI_SERVICE_H
#define _LORAMESHER_WIFI_SERVICE_H

#include <Arduino.h>

// WiFi libraries
#include <WiFi.h>

#include "ArduinoLog.h"

class WiFiService {
public:
    WiFiService();

    uint16_t getLocalAddress();

private:

    /**
     * @brief Local Address
     *
     */
    uint16_t localAddress = 0;

};

extern WiFiService WifiServ;

#endif