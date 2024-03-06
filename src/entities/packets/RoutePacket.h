#ifndef _LORAMESHER_ROUTE_PACKET_H
#define _LORAMESHER_ROUTE_PACKET_H

#include "PacketHeader.h"
#include "entities/routingTable/NetworkNode.h"

#pragma pack(1)
class RoutePacket final: public PacketHeader {
public:

    /**
     * @brief Node Role
     *
     */
    uint8_t nodeRole = 0;

    /**
     * @brief Network nodes
     *
     */
    NetworkNode networkNodes[];

    /**
     * @brief Get the Number of Network Nodes
     *
     * @return size_t Number of Network Nodes inside the packet
     */
    size_t getNetworkNodesSize() { return (this->packetSize - sizeof(RoutePacket)) / sizeof(NetworkNode); }
};

#pragma pack()

#endif