#ifndef _LORAMESHER_DATA_PACKET_H
#define _LORAMESHER_DATA_PACKET_H

#include <Arduino.h>

#include "RouteDataPacket.h"

#pragma pack(1)
class DataPacket final : public RouteDataPacket {
public:
    uint8_t payload[];

    /**
     * @brief Delete function for Packets
     *
     * @param p Packet to be deleted
     */
    void operator delete(void* p) {
        Log.traceln(F("Deleting Data packet"));
        free(p);
    }
};
#pragma pack()

#endif