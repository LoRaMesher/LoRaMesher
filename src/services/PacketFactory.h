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
     * @brief Create a packet of type T with the provided payload
     *
     * @tparam T The packet type to create
     * @param payload Pointer to the payload data (can be nullptr if payloadSize is 0)
     * @param payloadSize Size of the payload in bytes
     * @return T* Pointer to the newly created packet, or nullptr if allocation failed
     */
    template <typename T>
    static T* createPacket(uint8_t* payload, uint8_t payloadSize) {
        // Calculate the total packet size (header + payload)
        const size_t headerSize = sizeof(T);
        const size_t requestedPacketSize = headerSize + payloadSize;
        const size_t maxPacketSize = PacketFactory::getMaxPacketSize();

        // Determine actual packet size, respecting maximum limits
        size_t actualPacketSize = requestedPacketSize;
        if (actualPacketSize > maxPacketSize) {
            ESP_LOGW(LM_TAG, "Packet size (%u) exceeds maximum (%u bytes), truncating",
                requestedPacketSize, maxPacketSize);
            actualPacketSize = maxPacketSize;
        }

        ESP_LOGV(LM_TAG, "Creating packet with %u bytes", actualPacketSize);

        // Allocate memory for the packet
        T* packet = static_cast<T*>(pvPortMalloc(actualPacketSize));
        if (packet == nullptr) {
            ESP_LOGE(LM_TAG, "Failed to allocate packet memory");
            return nullptr;
        }

        // Calculate payload size that will fit in the packet
        const size_t availablePayloadSpace = (actualPacketSize > headerSize) ?
            (actualPacketSize - headerSize) : 0;
        const size_t copySize = std::min(static_cast<size_t>(payloadSize), availablePayloadSpace);

        // Copy payload if both payload pointer exists and there's data to copy
        if (payload != nullptr && copySize > 0) {
            // Log warning if payload was truncated
            if (payloadSize > copySize) {
                ESP_LOGW(LM_TAG, "Payload truncated from %u to %u bytes to fit max packet size",
                    payloadSize, copySize);
            }

            // Calculate destination address for payload
            void* payloadDest = reinterpret_cast<uint8_t*>(packet) + headerSize;

            // Copy the payload using standard memcpy
            memcpy(payloadDest, payload, copySize);
        }

        ESP_LOGI(LM_TAG, "Packet created with %u bytes", actualPacketSize);
        return packet;
    };

private:
    static size_t* maxPacketSize;

};

#endif // _LORAMESHER_PACKET_FACTORY_H