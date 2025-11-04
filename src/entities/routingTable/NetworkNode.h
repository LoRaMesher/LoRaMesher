#ifndef _LORAMESHER_NETWORK_NODE_H
#define _LORAMESHER_NETWORK_NODE_H

#pragma pack(1)

/**
 * @brief Network Node
 *
 */
class NetworkNode {
public:
    /**
     * @brief Address
     *
     */
    uint16_t address = 0;

    /**
     * @brief Reverse ETX - Link quality from this node TO us (measured by hello packet reception)
     * Scaled by 10 (e.g., value 10 = ETX 1.0, value 20 = ETX 2.0)
     */
    uint8_t reverseETX = 10;

    /**
     * @brief Forward ETX - Link quality from us TO this node (measured by ACK reception)
     * Scaled by 10 (e.g., value 10 = ETX 1.0, value 20 = ETX 2.0)
     */
    uint8_t forwardETX = 10;

    /**
     * @brief Role of the Node
     *
     */
    uint8_t role = 0;

    NetworkNode() {};

    NetworkNode(uint16_t address_, uint8_t reverseETX_, uint8_t forwardETX_, uint8_t role_):
        address(address_), reverseETX(reverseETX_), forwardETX(forwardETX_), role(role_) {};

    // Legacy constructor for backward compatibility during transition
    NetworkNode(uint16_t address_, uint8_t metric_, uint8_t role_):
        address(address_), reverseETX(metric_), forwardETX(metric_), role(role_) {};
};

#pragma pack()

#endif