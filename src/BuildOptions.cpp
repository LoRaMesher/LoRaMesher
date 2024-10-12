#include "BuildOptions.h"

const char* LM_TAG = "LoRaMesher";
const char* LM_VERSION = "0.0.8";

const char* getPacketType(uint8_t type) {
    switch (type) {
        case NEED_ACK_P:
            return "NEED_ACK_P";
        case DATA_P:
            return "DATA_P";
        case ROUTING_P:
            return "ROUTING_P";
        case ACK_P:
            return "ACK_P";
        case XL_DATA_P:
            return "XL_DATA_P";
        case LOST_P:
            return "LOST_P";
        case SYNC_P:
            return "SYNC_P";
        case HELLO_P:
            return "HELLO_P";
        case ROUTING_REQUEST_P:
            return "ROUTING_REQUEST_P";
        default:
            return "UNKNOWN";
    }
}

#ifdef ARDUINO

size_t getFreeHeap() {
    return ESP.getFreeHeap();
}

#else 

#include <esp_timer.h>

size_t getFreeHeap() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

unsigned long IRAM_ATTR millis() {
    return (unsigned long) (esp_timer_get_time() / 1000ULL);
}
long random(long howsmall, long howbig);
long random(long howbig) {
    if (howbig == 0) {
        return 0;
    }
    if (howbig < 0) {
        return (random(0, -howbig));
    }
    // if randomSeed was called, fall back to software PRNG
    uint32_t val = rand();
    return val % howbig;
}

long random(long howsmall, long howbig) {
    if (howsmall >= howbig) {
        return howsmall;
    }
    long diff = howbig - howsmall;
    return random(diff) + howsmall;
}
#endif
