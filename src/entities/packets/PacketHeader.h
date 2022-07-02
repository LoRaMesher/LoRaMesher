#ifndef _LORAMESHER_PACKET_HEADER_H
#define _LORAMESHER_PACKET_HEADER_H

#include <Arduino.h>

#include "ArduinoLog.h"

#pragma pack(1)
class PacketHeader {
public:
    uint16_t dst;
    uint16_t src;
    uint8_t type;
    //TODO: REMOVE THIS ID
    uint8_t id;
    uint8_t payloadSize = 0;

    /**
     * @brief Get the Payload Length in bytes
     *
     * @return size_t
     */
    size_t getPayloadLength() { return this->payloadSize; }

    /**
     * @brief Get the Packet Length in bytes
     *
     * @return size_t in bytes
     */
    size_t getPacketLength() { return sizeof(PacketHeader) + getPayloadLength(); }

    /**
     * @brief Delete function for Packets
     *
     * @param p Packet to be deleted
     */
    void operator delete(void* p) {
        Log.traceln(F("Deleting Header packet"));
        free(p);
    }
    
};
#pragma pack()

#endif
