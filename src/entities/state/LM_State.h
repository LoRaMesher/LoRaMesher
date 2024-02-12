#pragma once

#include "entities/packets/ControlPacket.h"

#pragma pack(1)
enum LM_StateType {
    STATE_TYPE_RECEIVED,
    STATE_TYPE_SENT,
    STATE_TYPE_MANAGER
};

class LM_State {
public:
    size_t id = 0;
    LM_StateType type = STATE_TYPE_RECEIVED;

    size_t receivedQueueSize = 0;
    size_t sentQueueSize = 0;
    size_t receivedUserQueueSize = 0;
    size_t q_WRPSize = 0;
    size_t q_WSPSize = 0;
    size_t routingTableSize = 0;
    size_t secondsSinceStart = 0;
    size_t freeMemoryAllocation = 0;
    // TODO: Clone all the routing table?

    ControlPacket packetHeader;
};
#pragma pack()