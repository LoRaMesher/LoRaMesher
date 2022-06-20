#include "PacketQueueService.h"

void PacketQueueService::addOrdered(LM_LinkedList<QueuePacket<uint8_t>>* list, QueuePacket<uint8_t>* qp) {
    list->setInUse();

    if (list->moveToStart()) {
        do {
            Log.verboseln("WTF");
            QueuePacket<uint8_t>* current = list->Last();
            uint16_t prio = current->number;
            Log.verboseln("WTF2");
            Log.verboseln("%d", prio);
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