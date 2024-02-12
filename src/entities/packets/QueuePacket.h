#ifndef _LORAMESHER_QUEUE_PACKET_H
#define _LORAMESHER_QUEUE_PACKET_H

#include "BuildOptions.h"

template <typename T>
class QueuePacket {
public:
    uint16_t number = 0;
    uint8_t priority = 0;
    float rssi = 0;
    float snr = 0;
    T* packet;
};

#endif
