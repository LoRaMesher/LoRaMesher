#ifndef _LORAMESHER_ROUTE_DATA_PACKET_H
#define _LORAMESHER_ROUTE_DATA_PACKET_H

#include "PacketHeader.h"

#pragma pack(1)
class RouteDataPacket : public PacketHeader {
public:
    uint16_t via = 0;
};
#pragma pack()

#endif