#ifndef _LORAMESHER_PACKET_SERVICE_H
#define _LORAMESHER_PACKET_SERVICE_H

#include "entities/packets/Packet.h"
#include "entities/packets/ControlPacket.h"
#include "entities/packets/DataPacket.h"
#include "entities/packets/AppPacket.h"
#include "entities/packets/RoutePacket.h"

class PacketService {
public:

    /**
     * @brief Reinterpret cast from a Packet<uint8_t>* to a DataPacket*
     *
     * @param p packet to be reinterpret
     * @return DataPacket*
     */
    static DataPacket* dataPacket(Packet<uint8_t>* p);

    /**
     * @brief Create a Control Packet
     *
     * @param dst Destination address
     * @param src Source address
     * @param type Type of packet
     * @param payload Pointer to the payload
     * @param payloadSize Payload size
     * @return ControlPacket*
     */
    static ControlPacket* createControlPacket(uint16_t dst, uint16_t src, uint8_t type, uint8_t* payload, uint8_t payloadSize);

    /**
     * @brief Create a Empty Control Packet
     *
     * @param dst Destination address
     * @param src Source address
     * @param type Type of packet
     * @param seq_id Sequence id of the packet
     * @param num_packets Number of the packet
     * @return ControlPacket*
     */
    static ControlPacket* createEmptyControlPacket(uint16_t dst, uint16_t src, uint8_t type, uint8_t seq_id, uint16_t num_packets);

    /**
     * @brief Create a Data Packet
     *
     * @param dst Destination address
     * @param src Source address
     * @param type Type of packet
     * @param payload Pointer to the payload
     * @param payloadSize Payload size
     * @return DataPacket*
     */
    static DataPacket* createDataPacket(uint16_t dst, uint16_t src, uint8_t type, uint8_t* payload, uint8_t payloadSize);

    /**
     * @brief Create an Empty Packet
     *
     * @param packetSize Packet size in bytes
     * @return Packet<uint8_t>*
     */
    static Packet<uint8_t>* createEmptyPacket(size_t packetSize);

    /**
     * @brief Copy a packet
     *
     * @tparam T type of packet
     * @param p packet
     * @param packetLength all packet length
     * @return Packet<uint8_t>*
     */
    template<class T>
    static Packet<uint8_t>* copyPacket(T* p, size_t packetLength) {
        Packet<uint8_t>* cpPacket = (Packet<uint8_t>*) malloc(packetLength);

        if (cpPacket) {
            memcpy(cpPacket, p, packetLength);
        } else {
            Log.errorln(F("Copy Packet not allocated"));
            return nullptr;
        }

        return cpPacket;
    }

    /**
     * @brief Create a Routing Packet object
     *
     * @param localAddress localAddress of the node
     * @param nodes list of NetworkNodes
     * @param numOfNodes Number of nodes
     * @return RoutePacket*
     */
    static RoutePacket* createRoutingPacket(uint16_t localAddress, NetworkNode* nodes, size_t numOfNodes);

    /**
     * @brief Create a Application Packet
     *
     * @param dst destination address
     * @param src source address
     * @param payload payload array
     * @param payloadSize payload size in bytes
     * @return AppPacket<uint8_t>*
     */
    static AppPacket<uint8_t>* createAppPacket(uint16_t dst, uint16_t src, uint8_t* payload, uint32_t payloadSize);

    /**
     * @brief given a DataPacket it will be converted to a AppPacket
     *
     * @param p packet of type DataPacket
     * @return AppPacket<uint8_t>*
     */
    static AppPacket<uint8_t>* convertPacket(DataPacket* p);

    /**
     * @brief Get the Packet Payload Length
     *
     * @param p Data packet
     * @return size_t Size of the actual payload
     */
    static size_t getPacketPayloadLength(DataPacket* p) { return p->payloadSize + sizeof(DataPacket) - sizeof(PacketHeader); }

    /**
     * @brief Get the Packet Payload Length object
     *
     * @param p
     * @return size_t
     */
    static size_t getPacketPayloadLength(ControlPacket* p) { return p->payloadSize - (sizeof(ControlPacket) - sizeof(PacketHeader)); }

    /**
     * @brief Get the Maximum Payload Length with a MAXPACKETSIZE defined for ControlPacket
     *
     * @param type
     * @return uint8_t
     */
    static uint8_t getMaximumPayloadLengthControlPacket(uint8_t type);

    /**
     * @brief Given a type returns if needs a data packet
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    static bool isDataPacket(uint8_t type);

    /**
     * @brief Given a type returns if needs a control packet
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    static bool isControlPacket(uint8_t type);

#ifndef LM_GOD_MODE
private:
#endif

    /**
     * @brief Create a T*
     *
     * @param payload Payload
     * @param payloadSize Length of the payload in bytes
     * @return T*
     */
    template<class T>
    static T* createPacket(uint8_t* payload, uint8_t payloadSize) {
        //Packet length = size of the packet + size of the payload
        int packetLength = sizeof(T) + payloadSize;

        if (packetLength > MAXPACKETSIZE) {
            Log.warningln(F("Trying to create a packet greater than MAXPACKETSIZE"));
            return nullptr;
        }

        T* p = (T*) malloc(packetLength);

        if (p) {
            //Copy the payload into the packet
            memcpy((void*) ((unsigned long) p + (sizeof(T))), payload, payloadSize);
        } else {
            Log.errorln(F("packet not allocated"));
            return nullptr;
        }

        Log.traceln(F("Packet created with %d bytes"), packetLength);

        return p;
    };
};

#endif