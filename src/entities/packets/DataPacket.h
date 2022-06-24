#ifndef _LORAMESHER_DATA_PACKET_H
#define _LORAMESHER_DATA_PACKET_H

#include <Arduino.h>

#include "RouteDataPacket.h"

#pragma pack(1)
template <class T>
class DataPacket final : public RouteDataPacket {
public:
    T payload[];
};
#pragma pack()

#endif