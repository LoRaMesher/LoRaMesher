#ifndef _LORAMESHER_PACKET_FACTORY_H
#define _LORAMESHER_PACKET_FACTORY_H

#include "entities/packets/Packet.h"

class PacketFactory {
public:

    static void setMaxPacketSize(size_t setMaxPacketSize) {
        if (maxPacketSize == nullptr)
            maxPacketSize = new size_t(setMaxPacketSize);
        else
            *maxPacketSize = setMaxPacketSize;
    }

    static size_t getMaxPacketSize() {
        if (maxPacketSize == nullptr)
            return 0;
        return *maxPacketSize;
    }

    /**
     * @brief Create a T*
     *
     * @param payload Payload
     * @param payloadSize Length of the payload in bytes
     * @return T*
     */
    template<class T>
    static T* createPacket(uint8_t* payload, uint8_t payloadSize) {
        //Packet size = size of the header + size of the payload
        size_t packetSize = sizeof(T) + payloadSize;
        size_t maxPacketSize = PacketFactory::getMaxPacketSize();

        if (packetSize > maxPacketSize) {
            ESP_LOGW(LM_TAG, "Trying to create a packet greater than %d bytes", maxPacketSize);
            packetSize = maxPacketSize;
        }

        ESP_LOGV(LM_TAG, "Creating packet with %d bytes", packetSize);

        T* p = static_cast<T*>(pvPortMalloc(packetSize));

        if (p) {
            //Copy the payload into the packet
            memcpy(reinterpret_cast<void*>((unsigned long) p + (sizeof(T))), payload, payloadSize);
        }
        else {
            ESP_LOGE(LM_TAG, "packet not allocated");
            return nullptr;
        }

        ESP_LOGI(LM_TAG, "Packet created with %d bytes", packetSize);

        return p;
    };

private:
    static size_t* maxPacketSize;

};

#endif // _LORAMESHER_PACKET_FACTORY_H