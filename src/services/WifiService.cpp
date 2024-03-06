#include "WiFiService.h"

#ifdef ARDUINO
#include "WiFi.h"
#else
#include "hal/efuse_hal.h"
#include "esp_mac.h"
#endif

void WiFiService::init() {
    uint8_t mac[6];
#ifdef ARDUINO
    WiFi.macAddress(mac);
#else
    efuse_hal_get_mac(mac);
#endif
    localAddress = (mac[4] << 8) | mac[5];
    ESP_LOGI(LM_TAG, "Local LoRa address (from WiFi MAC): %X", localAddress);
}

uint16_t WiFiService::getLocalAddress() {
    if (localAddress == 0)
        init();
    return localAddress;
}

uint16_t WiFiService::localAddress = 0;
