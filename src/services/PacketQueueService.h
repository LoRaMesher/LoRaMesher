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
     * @brief Add the Queue packet into the list ordered
     *
     * @param list Linked list to add the QueuePacket
     * @param qp Queue packet to be added
     */
    static void addOrdered(LM_LinkedList<QueuePacket<uint8_t>>* list, QueuePacket<uint8_t>* qp);

    /**
     * @brief Create a Queue Packet element
     *
     * @tparam T type of the queue packet
     * @tparam I type of the packet
     * @param p packet
     * @param priority priority inside the queue
     * @param number Number of the sequence
     * @return QueuePacket<uint8_t>*
     */
    template <typename T>
    static QueuePacket<uint8_t>* createQueuePacket(T* p, uint8_t priority, uint16_t number = 0) {
        QueuePacket<uint8_t>* qp = new QueuePacket<uint8_t>();
        qp->priority = priority;
        qp->number = number;
        qp->packet = reinterpret_cast<uint8_t*>(p);
        return qp;
    }

    /**
     * @brief It will delete the packet queue and the packet inside it
     *
     * @tparam T Type of the packet queue
     * @param pq packet queue to be deleted
     */
    static void deleteQueuePacketAndPacket(QueuePacket<Packet<uint8_t>>* pq) {
        free(pq->packet);

        Log.traceln(F("Deleting packet queue"));
        delete pq;
    }

    template <typename T>
    static void deleteQueuePacketAndPacket(QueuePacket<T>* pq) {
        deleteQueuePacketAndPacket(reinterpret_cast<QueuePacket<Packet<uint8_t>>*>(pq));
    }

};

#endif
