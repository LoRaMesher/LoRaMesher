#ifndef _LORAMESHER_PACKET_SERVICE_H
#define _LORAMESHER_PACKET_SERVICE_H

#include "Packet.h"
#include "ControlPacket.h"
#include "DataPacket.h"
#include "AppPacket.h"
#include "RoutePacket.h"

class PacketService {
public:

    /**
     * @brief Create a Packet<T>
     *
     * @tparam T
     * @param dst Destination
     * @param src Source, normally local address, use getLocalAddress()
     * @param type Type of packet
     * @param payload Payload of type T
     * @param payloadSize Length of the payload in bytes
     * @return Packet<uint8_t>*
     */
    template <typename T>
    Packet<uint8_t>* createPacket(uint16_t dst, uint16_t src, uint8_t type, T* payload, uint8_t payloadSize) {
        uint8_t extraSize = getExtraLengthToPayload(type);

        Packet<uint8_t>* p = createPacket((uint8_t*) payload, payloadSize, extraSize);
        p->dst = dst;
        p->type = type;
        p->src = src;

        return p;
    }

    /**
     * @brief Create an Empty Packet
     *
     * @param packetSize Packet size in bytes
     * @return Packet<uint8_t>*
     */
    Packet<uint8_t>* createEmptyPacket(size_t packetSize);

    /**
     * @brief Copy a packet
     *
     * @param p packet to be copied
     * @return Packet<uint8_t>*
     */
    Packet<uint8_t>* copyPacket(Packet<uint8_t>* p);

    /**
     * @brief Create a Routing Packet object
     *
     * @param localAddress localAddress of the node
     * @param nodes list of NetworkNodes
     * @param numOfNodes Number of nodes
     * @return Packet<uint8_t>*
     */
    Packet<uint8_t>* createRoutingPacket(uint16_t localAddress, NetworkNode* nodes, size_t numOfNodes);

    /**
     * @brief Create a Application Packet
     *
     * @param dst destination address
     * @param src source address
     * @param payload payload array
     * @param payloadSize payload size in bytes
     * @return AppPacket<uint8_t>*
     */
    AppPacket<uint8_t>* createAppPacket(uint16_t dst, uint16_t src, uint8_t* payload, uint32_t payloadSize);

    /**
     * @brief given a Packet<uint8_t> it will be converted to a AppPacket
     *
     * @param p packet of type packet<uint8_t>
     * @return AppPacket<uint8_t>*
     */
    AppPacket<uint8_t>* convertPacket(Packet<uint8_t>* p);

    /**
     * @brief Get the Packet Payload Length in bytes
     *
     * @tparam T Type of packets
     * @param p packet to know the payload length in bytes
     * @return size_t The payload length in bytes of the packet
     */
    template <typename T>
    size_t getPacketPayloadLength(Packet<T>* p) { return p->payloadSize - getExtraLengthToPayload(p->type); }

    /**
     * @brief Get the payload in number of elements
     *
     * @tparam T
     * @param p
     * @return size_t
     */
    template<typename T>
    size_t getPayloadLength(Packet<T>* p) {
        return (p->payloadSize - getExtraLengthToPayload(p->type)) / sizeof(T);
    }

    /**
     * @brief Get the Payload of the packet
     *
     * @tparam T type of the payload
     * @param packet Packet that you want to get the payload
     * @return T* pointer of the packet payload
     */
    template <typename T>
    T* getPayload(Packet<T>* packet) {
        return (T*) ((unsigned long) packet->payload + getExtraLengthToPayload(packet->type));
    }

    /**
     * @brief Get the Maximum Payload Length with a MAXPACKETSIZE defined
     *
     * @param type
     * @return uint8_t
     */
    uint8_t getMaximumPayloadLength(uint8_t type);

    /**
     * @brief Given a type returns if needs a data packet
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    bool hasDataPacket(uint8_t type);

    /**
     * @brief Given a type returns if needs a control packet
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    bool hasControlPacket(uint8_t type);

private:

    /**
     * @brief Get the number of bytes to the payload, between a Packet<T> and their real payload
     *
     * @param type type of the packet
     * @return size_t number of bytes
     */
    uint8_t getExtraLengthToPayload(uint8_t type);

    /**
     * @brief Create a Packet<uint8_t>
     *
     * @param payload Payload
     * @param payloadSize Length of the payload in bytes
     * @param extraSize Indicates the function that it need to allocate extra space before the payload
     * @return Packet<uint8_t>*
     */
    Packet<uint8_t>* createPacket(uint8_t* payload, uint8_t payloadSize, uint8_t extraSize);

};

extern PacketService pS;

#endif