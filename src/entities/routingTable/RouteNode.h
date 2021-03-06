#ifndef _LORAMESHER_ROUTE_NODE_H
#define _LORAMESHER_ROUTE_NODE_H

#include <Arduino.h>

#include "NetworkNode.h"

/**
 * @brief Route Node
 *
 */
class RouteNode {
public:
    /**
     * @brief Network node
     *
     */
    NetworkNode networkNode;

    /**
     * @brief Timeout of the route
     *
     */
    uint32_t timeout = 0;

    /**
     * @brief Next hop to send the message
     *
     */
    uint16_t via = 0;

    RouteNode(uint16_t address_, uint8_t metric_, uint16_t via_) : networkNode(address_, metric_), via(via_) {};
};

#endif