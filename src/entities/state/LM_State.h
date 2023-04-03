#pragma once

#include <Arduino.h>
#include "entities/packets/ControlPacket.h"

enum LM_StateType {
    STATE_TYPE_RECEIVED,
    STATE_TYPE_SENT,
    STATE_TYPE_MANAGER
};

#pragma pack(1)
class LM_State {
public:

    size_t receivedQueueSize = 0;
    size_t sentQueueSize = 0;
    size_t receivedUserQueueSize = 0;
    size_t q_WRPSize = 0;
    size_t q_WSPSize = 0;
    size_t routingTableSize = 0;
    // TODO: Clone all the routing table?

    LM_StateType type = STATE_TYPE_RECEIVED;
    ControlPacket packetHeader;
};
#pragma pack()