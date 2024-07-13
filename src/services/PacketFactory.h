#ifndef _LORAMESHER_PACKET_FACTORY_H
#define _LORAMESHER_PACKET_FACTORY_H

#include "entities/packets/Packet.h"

class PacketFactory {
public:

    static void setMaxPacketSize(size_t maxPacketSize) {
        if (PacketFactory::maxPacketSize == nullptr)
            PacketFactory::maxPacketSize = new size_t(maxPacketSize);
        else
            *PacketFactory::maxPacketSize = maxPacketSize;
    }

    static size_t getMaxPacketSize() {
        return *PacketFactory::maxPacketSize;
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

        if (packetSize > getMaxPacketSize()) {
            ESP_LOGW(LM_TAG, "Trying to create a packet greater than %d bytes", PacketFactory::maxPacketSize);
            packetSize = PacketFactory::maxPacketSize;
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