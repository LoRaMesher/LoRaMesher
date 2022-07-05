#ifndef _LORAMESHER_PACKET_QUEUE_SERVICE_H
#define _LORAMESHER_PACKET_QUEUE_SERVICE_H

#include <Arduino.h>

#include "ArduinoLog.h"

#include "entities/packets/QueuePacket.h"

#include "services/PacketService.h"

#include "utilities/LinkedQueue.hpp"

class PacketQueueService {
public:

    /**
     * @brief Create a Queue Packet
     *
     * @tparam T Type of packet
     * @param p Packet
     * @param priority Priority of the packet
     * @param number Number of the Packet
     * @return QueuePacket<T>*
     */
    template<class T>
    static QueuePacket<T>* createQueuePacket(T* p, uint8_t priority, uint16_t number = 0) {
        QueuePacket<T>* qp = new QueuePacket<T>();
        qp->priority = priority;
        qp->number = number;
        qp->packet = p;
        return qp;
    }

    /**
     * @brief Find the element queue inside the specified list of queuePackets
     *
     * @tparam T Type of the queue
     * @param queue Queue where to find
     * @param num Number of the specific element
     * @return QueuePacket<T>* QueueElement inside the list
     */
    template<class T>
    static QueuePacket<T>* findPacketQueue(LM_LinkedList<QueuePacket<T>>* queue, uint8_t num) {
        queue->setInUse();

        if (queue->moveToStart()) {
            do {
                QueuePacket<T>* current = queue->getCurrent();

                if (current->number == num) {
                    queue->releaseInUse();
                    return current;
                }

            } while (queue->next());
        }

        queue->releaseInUse();

        return nullptr;
    }

    /**
     * @brief It will delete the packet queue and the packet inside it
     *
     * @tparam T Type of the packet queue
     * @param pq packet queue to be deleted
     */
    static void deleteQueuePacketAndPacket(QueuePacket<Packet<uint8_t>>* pq) {
        Log.traceln(F("Deleting packet"));
        free(pq->packet);

        Log.traceln(F("Deleting packet queue"));
        delete pq;
    }

    template <typename T>
    static void deleteQueuePacketAndPacket(QueuePacket<T>* pq) {
        deleteQueuePacketAndPacket((QueuePacket<Packet<uint8_t>>*)(pq));
    }

};

#endif
