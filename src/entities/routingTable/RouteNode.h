#ifndef _LORAMESHER_ROUTE_NODE_H
#define _LORAMESHER_ROUTE_NODE_H

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

    /**
     * @brief SNR from received packets. Only available nodes at 1 hop.
     *
     */
    int8_t receivedSNR = 0;

    /**
     * @brief SNR from sent packets. Only available nodes at 1 hop.
     *
     */
    int8_t sentSNR = 0;

    /**
     * @brief SRTT, smoothed round-trip time (RFC 6298)
     *
     */
    unsigned long SRTT = 0;

    /**
     * @brief RTTVAR, round-trip time variation (RFC 6298)
     *
     */
    unsigned long RTTVAR = 0;

    /**
     * @brief Construct a new Route Node object
     *
     * @param address_ Address
     * @param metric_ Metric
     * @param via_ Via
     */
    RouteNode(uint16_t address_, uint8_t metric_, uint8_t role_, uint16_t via_): networkNode(address_, metric_, role_), via(via_) {};
};

#endif