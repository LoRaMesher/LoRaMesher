#ifndef _LORAMESHER_CONTROL_PACKET_H
#define _LORAMESHER_CONTROL_PACKET_H

#include <Arduino.h>

#pragma pack(1)
template <typename T>
class ControlPacket {
public:
    uint8_t seq_id;
    uint16_t number;
    T payload[];
};
#pragma pack(pop)

#endif