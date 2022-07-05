#include "PacketService.h"




Packet<uint8_t>* PacketService::createEmptyPacket(size_t packetSize) {
    if (packetSize > MAXPACKETSIZE) {
        Log.warningln(F("Trying to create a packet greater than MAXPACKETSIZE"));
        packetSize = MAXPACKETSIZE;
    }

    Packet<uint8_t>* p = (Packet<uint8_t>*) malloc(packetSize);

    Log.traceln(F("Packet created with %d bytes"), packetSize);

    return p;

}

AppPacket<uint8_t>* PacketService::convertPacket(DataPacket* p) {
    uint32_t payloadSize = p->payloadSize - (sizeof(DataPacket) - sizeof(PacketHeader));

    AppPacket<uint8_t>* uPacket = createAppPacket(p->dst, p->src, p->payload, payloadSize);
    return uPacket;
}

AppPacket<uint8_t>* PacketService::createAppPacket(uint16_t dst, uint16_t src, uint8_t* payload, uint32_t payloadSize) {
    int packetLength = sizeof(AppPacket<uint8_t>) + payloadSize;

    AppPacket<uint8_t>* p = (AppPacket<uint8_t>*) malloc(packetLength);

    if (p) {
        //Copy the payload into the packet
        memcpy((void*) ((unsigned long) p->payload), payload, payloadSize);
    } else {
        Log.errorln(F("User Packet not allocated"));
        return nullptr;
    }

    p->dst = dst;
    p->src = src;
    p->payloadSize = payloadSize;

    return p;
}

uint8_t PacketService::getMaximumPayloadLengthControlPacket(uint8_t type) {
    return MAXPACKETSIZE - sizeof(ControlPacket);
}

bool PacketService::isDataPacket(uint8_t type) {
    return (HELLO_P & type) != HELLO_P;
}

bool PacketService::isControlPacket(uint8_t type) {
    return !((HELLO_P & type) == HELLO_P || (DATA_P & type) == DATA_P);
}

RoutePacket* PacketService::createRoutingPacket(uint16_t localAddress, NetworkNode* nodes, size_t numOfNodes) {
    size_t routingSizeInBytes = numOfNodes * sizeof(NetworkNode);

    RoutePacket* routePacket = createPacket<RoutePacket>((uint8_t*) nodes, routingSizeInBytes);
    routePacket->dst = BROADCAST_ADDR;
    routePacket->src = localAddress;
    routePacket->type = HELLO_P;
    routePacket->payloadSize = routingSizeInBytes;

    return routePacket;
}

DataPacket* PacketService::dataPacket(Packet<uint8_t>* p) {
    return (DataPacket*) (p);
}

ControlPacket* PacketService::createControlPacket(uint16_t dst, uint16_t src, uint8_t type, uint8_t* payload, uint8_t payloadSize) {
    ControlPacket* packet = createPacket<ControlPacket>(payload, payloadSize);
    packet->dst = dst;
    packet->src = src;
    packet->type = type;
    packet->payloadSize = payloadSize + sizeof(ControlPacket) - sizeof(PacketHeader);

    return packet;
}

ControlPacket* PacketService::createEmptyControlPacket(uint16_t dst, uint16_t src, uint8_t type, uint8_t seq_id, uint16_t num_packets) {
    ControlPacket* packet = createPacket<ControlPacket>((uint8_t*) 0, 0);
    packet->dst = dst;
    packet->src = src;
    packet->type = type;
    packet->seq_id = seq_id;
    packet->number = num_packets;
    packet->payloadSize = sizeof(ControlPacket) - sizeof(PacketHeader);

    return packet;
}

DataPacket* PacketService::createDataPacket(uint16_t dst, uint16_t src, uint8_t type, uint8_t* payload, uint8_t payloadSize) {
    DataPacket* packet = createPacket<DataPacket>(payload, payloadSize);
    packet->dst = dst;
    packet->src = src;
    packet->type = type;
    packet->payloadSize = payloadSize + sizeof(DataPacket) - sizeof(PacketHeader);

    return packet;
}