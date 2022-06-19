#include "PacketService.h"

Packet<uint8_t>* PacketService::createPacket(uint8_t* payload, uint8_t payloadSize, uint8_t extraSize) {
    //Packet length = size of the packet + size of the payload + extraSize before payload
    int packetLength = sizeof(Packet<uint8_t>) + payloadSize + extraSize;

    if (packetLength > MAXPACKETSIZE) {
        Log.warningln(F("Trying to create a packet greater than MAXPACKETSIZE"));
        // return nullptr;
    }

    Packet<uint8_t>* p = (Packet<uint8_t>*) malloc(packetLength);

    if (p) {
        //Copy the payload into the packet
        //If has extraSize, we need to add the extraSize to point correctly to the p->payload
        memcpy((void*) ((unsigned long) p->payload + (extraSize)), payload, payloadSize);
    } else {
        Log.errorln(F("packet not allocated"));
        return nullptr;
    }

    p->payloadSize = payloadSize + extraSize;

    Log.traceln(F("Packet created with %d bytes"), packetLength);

    return p;
};


Packet<uint8_t>* PacketService::createEmptyPacket(size_t packetSize) {
    if (packetSize > MAXPACKETSIZE) {
        Log.warningln(F("Trying to create a packet greater than MAXPACKETSIZE"));
        packetSize = MAXPACKETSIZE;
    }

    Packet<uint8_t>* p = (Packet<uint8_t>*) malloc(packetSize);

    Log.traceln(F("Packet created with %d bytes"), packetSize);

    return p;

}

Packet<uint8_t>* PacketService::copyPacket(Packet<uint8_t>* p) {
    int packetLength = p->getPacketLength();

    Packet<uint8_t>* cpPacket = (Packet<uint8_t>*) malloc(packetLength);

    if (cpPacket) {
        memcpy(cpPacket, p, packetLength);
    } else {
        Log.errorln(F("Copy Packet not allocated"));
        return nullptr;
    }

    return cpPacket;
}

AppPacket<uint8_t>* PacketService::convertPacket(Packet<uint8_t>* p) {
    AppPacket<uint8_t>* uPacket = createAppPacket(p->dst, p->src, getPayload(p), getPayloadLength(p));
    delete p;

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

uint8_t PacketService::getMaximumPayloadLength(uint8_t type) {
    return MAXPACKETSIZE - getExtraLengthToPayload(type) - sizeof(Packet<uint8_t>);
}

uint8_t PacketService::getExtraLengthToPayload(uint8_t type) {
    //Offset to the payload
    uint8_t extraSize = 0;

    //Check if type contains DATA_P or XL_DATA_P, add via size to extra size
    if (hasDataPacket(type))
        extraSize += sizeof(DataPacket<uint8_t>);

    //Check if needs a control packet given a type. Add seq_id and number size to extra size
    if (hasControlPacket(type))
        extraSize += sizeof(ControlPacket<uint8_t>);

    return extraSize;
}

bool PacketService::hasDataPacket(uint8_t type) {
    return (HELLO_P & type) != HELLO_P;
}

bool PacketService::hasControlPacket(uint8_t type) {
    return !((HELLO_P & type) == HELLO_P || (DATA_P & type) == DATA_P);
}

Packet<uint8_t>* PacketService::createRoutingPacket(uint16_t localAddress, NetworkNode* nodes, size_t numOfNodes) {
    size_t routingSizeInBytes = numOfNodes * sizeof(NetworkNode);

    Packet<uint8_t>* networkPacket = createPacket(BROADCAST_ADDR, localAddress, HELLO_P, (uint8_t*) nodes, routingSizeInBytes);

    return networkPacket;
}
