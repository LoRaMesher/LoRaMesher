#ifndef _LORAMESHER_PACKETX_H
#define _LORAMESHER_PACKETX_H

#include "ArduinoLog.h"

#pragma pack(1)
template<class T>
class PacketX {
public:
    uint16_t dst;
    uint16_t src;
    uint8_t type;
    //TODO: REMOVE THIS ID
    uint8_t id;
    uint8_t payloadSize = 0;
    T payload[];

    // /**
    //  * @brief Delete function for Packets
    //  *
    //  * @param p Packet to be deleted
    //  */
    void operator delete(void* p) {
        Log.traceln(F("Deleting packet"));
        free(p);
    }

    uint32_t getExtraToPayload() { return 0; }
};
#pragma pop()

#endif
