#ifndef _LORAMESHER_ROUTING_TABLE_SERVICE_H
#define _LORAMESHER_ROUTING_TABLE_SERVICE_H

#include <Arduino.h>

#include "ArduinoLog.h"


#include "helpers/LinkedQueue.hpp"

#include "RouteNode.h"

#include "NetworkNode.h"

#include "packets/RoutePacket.h"

#include "BuildOptions.h"

#include "services/WiFiService.h"


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
     * @brief Prints the actual routing table in the log
     *
     */
    void printRoutingTable();

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
     * @return RouteNode* pointer to the RouteNode or nullptr
     */
    RouteNode* findNode(uint16_t address);

    /**
     * @brief Returns if address is inside the routing table
     *
     * @param address Address you want to check if is inside the routing table
     * @return true If the address is inside the routing table
     * @return false If the address is not inside the routing table
     */
    bool hasAddressRoutingTable(uint16_t address);

    /**
     * @brief Get the Next Hop address
     *
     * @param dst address of the next hop
     * @return uint16_t address of the next hop
     */
    uint16_t getNextHop(uint16_t dst);

    /**
     * @brief Get the Number Of Hops of the address inside the routing table
     *
     * @param address Address of the number of hops you want to know
     * @return uint8_t Number of Hops or 0 if address not found in routing table.
     */
    uint8_t getNumberOfHops(uint16_t address);

    /**
     * @brief Returns the routing table size
     *
     * @return size_t
     */
    size_t routingTableSize();

    /**
     * @brief Process the network packet
     *
     * @param p Route Packet
     */
    void processRoute(RoutePacket* p);


    /**
     * @brief Checks all the routing entries for a route timeout and remove the entry.
     *
     */
    void manageTimeoutRoutingTable();

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