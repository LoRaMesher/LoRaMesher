#ifndef _LORAMESHER_PACKET_SERVICE_H
#define _LORAMESHER_PACKET_SERVICE_H

#include "entities/packets/Packet.h"
#include "entities/packets/ControlPacket.h"
#include "entities/packets/DataPacket.h"
#include "entities/packets/AppPacket.h"
#include "entities/packets/RoutePacket.h"
#include "services/RoleService.h"
#include "BuildOptions.h"
#include "PacketFactory.h"

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
     * @brief Reinterpret cast from a Packet<uint8_t>* to a ControlPacket*
     *
     * @param p packet to be reinterpret
     * @return ControlPacket*
     */
    static ControlPacket* controlPacket(Packet<uint8_t>* p);

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
        Packet<uint8_t>* cpPacket = static_cast<Packet<uint8_t>*>(pvPortMalloc(packetLength));

        if (cpPacket) {
            memcpy(reinterpret_cast<void*>(cpPacket), reinterpret_cast<void*>(p), packetLength);
        }
        else {
            ESP_LOGE(LM_TAG, "Copy Packet not allocated");
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
     * @param nodeRole Role of the node
     * @return RoutePacket*
     */
    static RoutePacket* createRoutingPacket(uint16_t localAddress, NetworkNode* nodes, size_t numOfNodes, uint8_t nodeRole);

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
     * @brief Get the Packet Payload Length in bytes
     *
     * @param p
     * @return size_t
     */
    static size_t getPacketPayloadLength(Packet<uint8_t>* p);

    /**
     * @brief Get the Packet Payload Length
     *
     * @param p Data packet
     * @return size_t Size of the actual payload
     */
    static size_t getPacketPayloadLength(DataPacket* p) { return p->packetSize - sizeof(DataPacket); }

    /**
     * @brief Get the Packet Payload Length object
     *
     * @param p
     * @return size_t
     */
    static size_t getPacketPayloadLength(ControlPacket* p) { return p->packetSize - sizeof(ControlPacket); }

    /**
     * @brief Get the Packet Payload Length Without the Control packets payloads
     *
     * @param p Packet
     * @return size_t Size of payload without the payload of the Control packets
     */
    static size_t getPacketPayloadLengthWithoutControl(Packet<uint8_t>* p);

    /**
     * @brief Get the Packet Header Length in bytes
     *
     * @param p
     * @return size_t
     */
    static size_t getHeaderLength(Packet<uint8_t>* p);

    /**
     * @brief Get the Header Length
     *
     * @param type type of the packet
     * @return uin16_t header length
     */
    static uint8_t getHeaderLength(uint8_t type);

    /**
     * @brief Get the Control Length in bytes. Used to calculate the Overhead of all the packets.
     * In this function includes the payload of the Routing Packets and other control packets, like sync, acks and lost.
     *
     * @param p
     * @return size_t
     */
    static size_t getControlLength(Packet<uint8_t>* p);

    /**
     * @brief Get the Maximum Payload Length with a MAXPACKETSIZE
     *
     * @param type
     * @return uint8_t
     */
    static uint8_t getMaximumPayloadLength(uint8_t type);

    /**
     * @brief Given a type returns if is a data packet
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    static bool isDataPacket(uint8_t type);

    /**
     * @brief Given a type returns if is only a data packet
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    static bool isOnlyDataPacket(uint8_t type);

    /**
     * @brief Given a type returns if is a control packet
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    static bool isControlPacket(uint8_t type);

    /**
     * @brief Given a type returns if is a hello packet
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    static bool isHelloPacket(uint8_t type);

    /**
     * @brief Given a type returns if is a NeedAck packet
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    static bool isNeedAckPacket(uint8_t type);

    /**
     * @brief Given a type returns if is an Ack packet
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    static bool isAckPacket(uint8_t type);

    /**
     * @brief Given a type returns if is a Lost packet
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    static bool isLostPacket(uint8_t type);

    /**
     * @brief Given a type returns if is a Sync packet
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    static bool isSyncPacket(uint8_t type);

    /**
     * @brief Given a type returns if is a XL packet
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    static bool isXLPacket(uint8_t type);

    /**
     * @brief Given a type returns if is a Data Control Packet, It will include HELLO_P, ACKs, LOST_P and SYN_P
     *
     * @param type type of the packet
     * @return true True if needed
     * @return false If not
     */
    static bool isDataControlPacket(uint8_t type);

    /**
     * @brief Get the Packet Header
     *
     * @param p Get the packet headers without the payload to identify the packet and the payload size
     * @return ControlPacket*
     */
    static ControlPacket* getPacketHeader(Packet<uint8_t>* p);
};

#endif