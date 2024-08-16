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
     * @brief Received link quality only available for nodes at 1 hop
     *
     */
    uint8_t received_link_quality = 0;

    /**
     * @brief Transmitted link quality only available for nodes at 1 hop
     *
     */
    uint8_t transmitted_link_quality = 0;

    /**
     * @brief Has received a hello packet
     *
     */
    bool hasReceivedHelloPacket = true;

    /**
     * @brief Construct a new Route Node object
     *
     * @param address_ Address
     * @param metric_ Metric
     * @param via_ Via
     * @param hop_count_ Hop count
     * @param received_link_quality_ Received link quality
     * @param transmitted link quality_ Transmitted link quality
     */
    RouteNode(
        uint16_t address_, uint8_t metric_, uint8_t role_, uint16_t via_, uint8_t hop_count_,
        uint8_t received_link_quality_, uint8_t transmitted_link_quality_):
        networkNode(address_, metric_, role_, hop_count_), via(via_),
        received_link_quality(received_link_quality_), transmitted_link_quality(transmitted_link_quality_) {
    };
};

#endif