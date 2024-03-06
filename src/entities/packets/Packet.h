#ifndef _LORAMESHER_PACKET_H
#define _LORAMESHER_PACKET_H

#include "BuildOptions.h"
#include "PacketHeader.h"

#pragma pack(1)
template <typename T>
class Packet final: public PacketHeader {
public:

    T payload[];

    /**
     * @brief Delete function for Packets
     *
     * @param p Packet to be deleted
     */
    void operator delete(void* p) {
        ESP_LOGV(LM_TAG, "Deleting  packet");
        vPortFree(p);
    }

};
#pragma pack()

#endif