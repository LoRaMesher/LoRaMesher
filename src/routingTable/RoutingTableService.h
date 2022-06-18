#ifndef _LORAMESHER_ROUTING_TABLE_SERVICE_H
#define _LORAMESHER_ROUTING_TABLE_SERVICE_H

#include <Arduino.h>

#include "ArduinoLog.h"


#include "helpers/LinkedQueue.hpp"

#include "RouteNode.h"

#include "NetworkNode.h"

#include "packets/RoutePacket.h"

#include "BuildOpt.h"

class RoutingTableService {
public:
    /**
     * @brief Get the Instance object
     *
     * @return RoutingTableService&
     */
    static RoutingTableService& getInstance() {
        static RoutingTableService instance;
        return instance;
    }

    /**
     * @brief Routing table List
     *
     */
    LM_LinkedList<RouteNode>* routingTableList = new LM_LinkedList<RouteNode>();

    /**
     * @brief Returns the routing table size
     *
     * @return size_t
     */
    size_t routingTableSize();

    /**
     * @brief Get the All Network Nodes that are inside the routing table
     *
     * @return NetworkNode* All the nodes in a list.
     */
    NetworkNode* getAllNetworkNodes();

    /**
     * @brief Find the node that contains the address
     *
     * @param address address to be found
     * @return RouteNode* pointer to the routableNode or nullptr
     */
    RouteNode* findNode(uint16_t address);


    /**
     * @brief Process the network packet
     *
     * @param p Route Packet
     */
    void processRoute(RoutePacket* p);

private:

    RoutingTableService() {};

    /**
     * @brief process the network node, adds the node in the routing table if can
     *
     * @param via via address
     * @param node NetworkNode
     */
    void processRoute(uint16_t via, NetworkNode* node);

    /**
     * @brief Reset the timeout of the given node
     *
     * @param node node to be reset the timeout
     */
    void resetTimeoutRoutingNode(RouteNode* node);


    /**
     * @brief Add node to the routing table
     *
     * @param node Network node that includes the address and the metric
     * @param via Address to next hop to reach the network node address
     */
    void addNodeToRoutingTable(NetworkNode* node, uint16_t via);
};

#endif