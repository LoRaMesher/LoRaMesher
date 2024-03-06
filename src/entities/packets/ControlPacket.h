#ifndef _LORAMESHER_CONTROL_PACKET_H
#define _LORAMESHER_CONTROL_PACKET_H

#include "RouteDataPacket.h"

#include "BuildOptions.h"

#pragma pack(1)
class ControlPacket final: public RouteDataPacket {
public:
    uint8_t seq_id = 0;
    uint16_t number = 0;
    uint8_t payload[];

    /**
     * @brief Get the Packet Length
     *
     * @tparam T
     * @param p Packet of Type T
     * @return size_t Packet size in bytes
     */
    size_t getPacketLength() { return this->packetSize; }

    /**
     * @brief Delete function for Packets
     *
     * @param p Packet to be deleted
     */
    void operator delete(void* p) {
        ESP_LOGV(LM_TAG, "Deleting Control packet");
        vPortFree(p);
    }
};
#pragma pack()

#endif