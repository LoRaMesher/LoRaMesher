#ifndef _LORAMESHER_CONTROL_PACKET_H
#define _LORAMESHER_CONTROL_PACKET_H

#include <Arduino.h>

#include "RouteDataPacket.h"

#pragma pack(1)
class ControlPacket final : public RouteDataPacket {
public:
    uint8_t seq_id;
    uint16_t number;
    uint8_t payload[];

    /**
     * @brief Get the Packet Length
     *
     * @tparam T
     * @param p Packet of Type T
     * @return size_t Packet size in bytes
     */
    size_t getPacketLength() { return sizeof(ControlPacket) + this->payloadSize; }

    /**
     * @brief Delete function for Packets
     *
     * @param p Packet to be deleted
     */
    void operator delete(void* p) {
        Log.traceln(F("Deleting Control packet"));
        free(p);
    }
};
#pragma pack()

#endif