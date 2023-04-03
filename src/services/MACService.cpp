#include "MACService.h"

void MACService::init() {
    // uint8_t WiFiMAC[6];

    // WiFi.macAddress(WiFiMAC);
    // localAddress = (WiFiMAC[4] << 8) | WiFiMAC[5];

    // Log.noticeln(F("Local LoRa address (from WiFi MAC): %X"), localAddress);

    uint8_t _chipmacid[6];

    esp_efuse_mac_get_default(_chipmacid);

    localAddress = (_chipmacid[4] << 8) | _chipmacid[5];

    Log.noticeln(F("Chip MAC address: %X"), _chipmacid);
}

uint16_t MACService::getLocalAddress() {
    return localAddress;
}

uint16_t MACService::localAddress;
