#ifndef _LORAMESHER_ROUTE_PACKET_H
#define _LORAMESHER_ROUTE_PACKET_H

#include <Arduino.h>

#include "PacketHeader.h"
#include "entities/routingTable/NetworkNode.h"

#pragma pack(1)
class RoutePacket final : public PacketHeader {
public:
    NetworkNode routeNodes[];

    size_t getPayloadLength() { return this->payloadSize / sizeof(NetworkNode); }
};
#pragma pack(pop)

#endif