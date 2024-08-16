#ifndef _LORAMESHER_HELLO_PACKET_H
#define _LORAMESHER_HELLO_PACKET_H

#include "PacketHeader.h"
#include "entities/routingTable/HelloPacketNode.h"

#pragma pack(1)
class HelloPacket final: public PacketHeader {
public:

    /**
     * @brief Hello Packet Nodes
     *
     */
    HelloPacketNode helloPacketNodes[];

    /**
     * @brief Get the Number of Network Nodes
     *
     * @return size_t Number of Network Nodes inside the packet
     */
    size_t getHelloPacketNodesSize() { return (this->packetSize - sizeof(HelloPacket)) / sizeof(HelloPacketNode); }
};

#pragma pack()

#endif // _LORAMESHER_HELLO_PACKET_H