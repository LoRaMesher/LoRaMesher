#ifndef _LORAMESHER_APPPACKET_H
#define _LORAMESHER_APPPACKET_H


/**
 * @brief Application packet, it is used to send the packet to the application layer
 *
 * @tparam T
 */
template <class T>
class AppPacket {
public:
    /**
     * @brief Destination address, normally it will be local address or BROADCAST_ADDR
     *
     */
    uint16_t dst;

    /**
     * @brief Source Address
     *
     */
    uint16_t src;

    /**
     * @brief Payload Size in bytes
     *
     */
    uint32_t payloadSize = 0;

    /**
     * @brief Payload Array
     *
     */
    T payload[];

    /**
     * @brief Get the payload length in number of T
     *
     * @tparam T Type to be converted
     * @param p Application packet
     * @return size_t size in number of T
     */
    size_t getPayloadLength() { return this->payloadSize / sizeof(T); }

    /**
     * @brief Delete function for AppPackets
     *
     * @param p AppPacket to be deleted
     */
    void operator delete(void* p) {
        Log.traceln(F("Deleting app packet"));
        free(p);
    }
};

#endif