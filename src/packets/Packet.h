#ifndef _LORAMESHER_PACKET_H
#define _LORAMESHER_PACKET_H

#include <ArduinoLog.h>

#include "BuildOptions.h"
#include "PacketEx.h"

#pragma pack(1)
template <typename T>
class Packet : public PacketX<T> {
public:

    uint16_t via() {
        return this->payload[sizeof(uint16_t)];
    }

    /**
     * @brief Get the Packet Length
     *
     * @tparam T
     * @param p Packet of Type T
     * @return size_t Packet size in bytes
     */
    size_t getPacketLength() { return sizeof(this) + this->payloadSize; }

    uint32_t getExtraToPayload() { return sizeof(uint16_t); }
};
#pragma pack()

#endif