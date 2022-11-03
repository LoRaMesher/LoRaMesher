#include "PacketService.h"

Packet<uint8_t>* PacketService::createEmptyPacket(size_t packetSize) {
    if (packetSize > MAXPACKETSIZE) {
        Log.warningln(F("Trying to create a packet greater than MAXPACKETSIZE"));
        packetSize = MAXPACKETSIZE;
    }

    Packet<uint8_t>* p = static_cast<Packet<uint8_t>*>(malloc(packetSize));

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

    AppPacket<uint8_t>* p = static_cast<AppPacket<uint8_t>*>(malloc(packetLength));

    if (p) {
        //Copy the payload into the packet
        memcpy(p->payload, payload, payloadSize);
    }
    else {
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
    return (type & DATA_P) == DATA_P;
}

bool PacketService::isOnlyDataPacket(uint8_t type) {
    return type == DATA_P;
}

bool PacketService::isControlPacket(uint8_t type) {
    return !(isHelloPacket(type) || isOnlyDataPacket(type));
}

bool PacketService::isHelloPacket(uint8_t type) {
    return (type & HELLO_P) == HELLO_P;
}

bool PacketService::isNeedAckPacket(uint8_t type) {
    return (type & NEED_ACK_P) == NEED_ACK_P;
}

bool PacketService::isAckPacket(uint8_t type) {
    return (type & ACK_P) == ACK_P;
}

bool PacketService::isLostPacket(uint8_t type) {
    return (type & LOST_P) == LOST_P;
}

bool PacketService::isSyncPacket(uint8_t type) {
    return (type & SYNC_P) == SYNC_P;
}

bool PacketService::isXLPacket(uint8_t type) {
    return (type & XL_DATA_P) == XL_DATA_P;
}

bool PacketService::isDataControlPacket(uint8_t type) {
    return (isHelloPacket(type) || isAckPacket(type) || isLostPacket(type) || isLostPacket(type));
}

RoutePacket* PacketService::createRoutingPacket(uint16_t localAddress, NetworkNode* nodes, size_t numOfNodes, uint8_t nodeRole) {
    size_t routingSizeInBytes = numOfNodes * sizeof(NetworkNode);

    RoutePacket* routePacket = createPacket<RoutePacket>(reinterpret_cast<uint8_t*>(nodes), routingSizeInBytes);
    routePacket->dst = BROADCAST_ADDR;
    routePacket->src = localAddress;
    routePacket->type = HELLO_P;
    routePacket->payloadSize = routingSizeInBytes + sizeof(RoutePacket) - sizeof(PacketHeader);
    routePacket->nodeRole = nodeRole;

    return routePacket;
}

DataPacket* PacketService::dataPacket(Packet<uint8_t>* p) {
    return reinterpret_cast<DataPacket*>(p);
}

ControlPacket* PacketService::controlPacket(Packet<uint8_t>* p) {
    return reinterpret_cast<ControlPacket*>(p);
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
    ControlPacket* packet = createPacket<ControlPacket>(0, 0);
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

size_t PacketService::getPacketPayloadLength(Packet<uint8_t>* p) {
    if (isControlPacket(p->type))
        return getPacketPayloadLength(controlPacket(p));

    if (isDataPacket(p->type))
        return getPacketPayloadLength(dataPacket(p));

    return p->payloadSize;
}

size_t PacketService::getPacketHeaderLength(Packet<uint8_t>* p) {
    if (isControlPacket(p->type))
        return sizeof(ControlPacket);

    if (isDataPacket(p->type))
        return sizeof(DataPacket);

    return sizeof(PacketHeader);
}

size_t PacketService::getControlLength(Packet<uint8_t>* p) {
    if (isDataControlPacket(p->type))
        return p->getPacketLength();

    return getPacketHeaderLength(p);
}

size_t PacketService::getPacketPayloadLengthWithoutControl(Packet<uint8_t>* p) {
    if (isDataControlPacket(p->type))
        return 0;
    return getPacketPayloadLength(p);
}