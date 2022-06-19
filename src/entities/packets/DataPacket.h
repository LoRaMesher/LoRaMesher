#ifndef _LORAMESHER_DATA_PACKET_H
#define _LORAMESHER_DATA_PACKET_H

#include <Arduino.h>

#pragma pack(1)
template <class T>
class DataPacket {
public:
    uint16_t via;
    T payload[];
};
#pragma pop()

#endif