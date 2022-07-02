#ifndef _LORAMESHER_PACKET_H
#define _LORAMESHER_PACKET_H

#include <Arduino.h>

#include <ArduinoLog.h>

#include "BuildOptions.h"
#include "PacketHeader.h"


#pragma pack(1)
template <typename T>
class Packet final : public PacketHeader {
public:

    T payload[];

    /**
     * @brief Get the Packet Length
     *
     * @tparam T
     * @param p Packet of Type T
     * @return size_t Packet size in bytes
     */
    size_t getPacketLength() { return sizeof(Packet<T>) + this->payloadSize; }

    /**
     * @brief Get the Payload Length in number of T
     *
     * @return size_t
     */
    size_t getPayloadLength() { return this->payloadSize / sizeof(T); }


    T* getPayload() { return (T*) (&this + sizeof(Packet)); }

    /**
     * @brief Delete function for Packets
     *
     * @param p Packet to be deleted
     */
    void operator delete(void* p) {
        Log.traceln(F("Deleting packet"));
        free(p);
    }

};
#pragma pack()

#endif