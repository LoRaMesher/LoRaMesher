#pragma once

#include "entities/state/LM_State.h"
#include "entities/packets/ControlPacket.h"
#include "entities/packets/Packet.h"
#include "services/PacketService.h"
#include "utilities/LinkedQueue.hpp"

#include "BuildOptions.h"

class SimulatorService {
public:
    SimulatorService();
    ~SimulatorService();

    void addState(size_t receivedQueueSize, size_t sentQueueSize, size_t receivedUserQueueSize,
        size_t routingTableSize, size_t q_WRPSize, size_t q_WSPSize, LM_StateType type, Packet<uint8_t>* packet);

    void startSimulation();

    void stopSimulation();

    void clearStates();

    LM_LinkedList<LM_State>* statesList = new LM_LinkedList<LM_State>();

private:
    bool isSimulating = false;

    size_t numberStates = 0;
};