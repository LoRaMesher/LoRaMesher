#include "SimulatorService.h"

SimulatorService::SimulatorService() {

}

SimulatorService::~SimulatorService() {
    clearStates();
    delete statesList;
}

void SimulatorService::addState(size_t receivedQueueSize, size_t sentQueueSize, size_t receivedUserQueueSize, size_t routingTableSize, size_t q_WRPSize, size_t q_WSPSize, LM_StateType type, Packet<uint8_t>* packet) {
    if (!isSimulating) {
        return;
    }
    uint32_t freeHeap = getFreeHeap();

    if (freeHeap < 10000) {
        ESP_LOGW(LM_TAG, "Not enough memory to simulate. Free heap: %d", (int)freeHeap);
        return;
    }

    LM_State* state = new LM_State();
    state->id = numberStates++;
    state->receivedQueueSize = receivedQueueSize;
    state->sentQueueSize = sentQueueSize;
    state->receivedUserQueueSize = receivedUserQueueSize;
    state->routingTableSize = routingTableSize;
    state->q_WRPSize = q_WRPSize;
    state->q_WSPSize = q_WSPSize;
    state->type = type;
    state->secondsSinceStart = millis() / 1000;
    state->freeMemoryAllocation = freeHeap;

    if (packet == nullptr) {
        state->packetHeader = ControlPacket();
        statesList->Append(state);
        return;
    }

    ControlPacket* ctrlPacket = PacketService::getPacketHeader(packet);
    memcpy(&state->packetHeader, ctrlPacket, sizeof(ControlPacket));
    delete ctrlPacket;

    statesList->Append(state);
}

void SimulatorService::startSimulation() {
    isSimulating = true;
}

void SimulatorService::stopSimulation() {
    isSimulating = false;
}

void SimulatorService::clearStates() {
    statesList->Clear();
}
