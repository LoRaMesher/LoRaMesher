
#ifndef _LORAMESHER_HELLO_PACKET_NODE_H
#define _LORAMESHER_HELLO_PACKET_NODE_H

#pragma pack(1)

/**
 * @brief Hello Packet Node
 *
 */
class HelloPacketNode {
public:
    /**
     * @brief Address
     *
     */
    uint16_t address = 0;

    /**
     * @brief Received link quality
     *
     */
    uint8_t received_link_quality = 0;


    HelloPacketNode() {};

    HelloPacketNode(uint16_t address_, uint8_t received_link_quality_): address(address_), received_link_quality(received_link_quality_) {};
};

#pragma pack()

#endif // _LORAMESHER_HELLO_PACKET_NODE_H