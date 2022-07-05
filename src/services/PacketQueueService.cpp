#include "PacketQueueService.h"

void PacketQueueService::addOrdered(LM_LinkedList<QueuePacket<Packet<uint8_t>>>* list, QueuePacket<Packet<uint8_t>>* qp) {
    list->setInUse();
    if (list->moveToStart()) {
        do {
            QueuePacket<Packet<uint8_t>>* current = list->getCurrent();
            if (current->priority < qp->priority) {
                list->addCurrent(qp);
                list->releaseInUse();
                return;
            }
        } while (list->next());
    }

    list->Append(qp);

    list->releaseInUse();
}