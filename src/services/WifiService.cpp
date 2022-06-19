#include "WiFiService.h"

void WiFiService::init() {
    uint8_t WiFiMAC[6];

    WiFi.macAddress(WiFiMAC);
    localAddress = (WiFiMAC[4] << 8) | WiFiMAC[5];

    Log.noticeln(F("Local LoRa address (from WiFi MAC): %X"), localAddress);
}

uint16_t WiFiService::getLocalAddress() {
    return localAddress;
}

uint16_t WiFiService::localAddress;
