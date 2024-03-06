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
     * @brief Metric, how many hops to reach the previous address
     *
     */
    uint8_t metric = 0;

    /**
     * @brief Role of the Node
     *
     */
    uint8_t role = 0;

    NetworkNode() {};

    NetworkNode(uint16_t address_, uint8_t metric_, uint8_t role_): address(address_), metric(metric_), role(role_) {};
};

#pragma pack()

#endif