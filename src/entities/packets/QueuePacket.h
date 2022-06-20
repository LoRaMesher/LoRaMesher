#ifndef _LORAMESHER_QUEUE_PACKET_H
#define _LORAMESHER_QUEUE_PACKET_H

#include <Arduino.h>

/**
 * @brief packetQueue template
 *
 * @tparam T
 */
template <typename T>
class QueuePacket {
public:
    uint16_t number;
    uint8_t priority;
    T* packet;
};

#endif
