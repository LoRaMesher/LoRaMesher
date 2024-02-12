#ifndef _LORAMESHER_PACKET_HEADER_H
#define _LORAMESHER_PACKET_HEADER_H

#include "BuildOptions.h"

#pragma pack(1)
class PacketHeader {
public:
    uint16_t dst = 0;
    uint16_t src = 0;
    uint8_t type = 0;
    //TODO: REMOVE THIS ID
    uint8_t id;
    uint8_t packetSize = 0;

    /**
     * @brief Delete function for Packets
     *
     * @param p Packet to be deleted
     */
    void operator delete(void* p) {
        ESP_LOGV(LM_TAG, "Deleting Header packet");
        vPortFree(p);
    }

};
#pragma pack()

#endif
