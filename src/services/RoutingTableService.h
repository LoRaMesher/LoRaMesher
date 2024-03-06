#ifndef _LORAMESHER_ROUTING_TABLE_SERVICE_H
#define _LORAMESHER_ROUTING_TABLE_SERVICE_H

#include "utilities/LinkedQueue.hpp"

#include "entities/routingTable/RouteNode.h"

#include "entities/routingTable/NetworkNode.h"

#include "entities/packets/RoutePacket.h"

#include "BuildOptions.h"

#include "services/WiFiService.h"

#include "services/RoleService.h"

/**
 * @brief Routing Table Service
 *
 */
class RoutingTableService {
public:

	/**
	 * @brief Routing table List
	 *
	 */
	static LM_LinkedList<RouteNode>* routingTableList;

	/**
	 * @brief Prints the actual routing table in the log
	 *
	 */
	static void printRoutingTable();

	/**
	 * @brief Get the All Network Nodes that are inside the routing table
	 *
	 * @return NetworkNode* All the nodes in a list.
	 */
	static NetworkNode* getAllNetworkNodes();

	/**
	 * @brief Find the node that contains the address
	 *
	 * @param address address to be found
	 * @return RouteNode* pointer to the RouteNode or nullptr
	 */
	static RouteNode* findNode(uint16_t address);

	/**
	 * @brief Get the best node that contains a role, the nearest
	 *
	 * @param role role to be found
	 * @return RouteNode* pointer to the RouteNode or nullptr
	 */
	static RouteNode* getBestNodeByRole(uint8_t role);

	/**
	 * @brief Returns if address is inside the routing table
	 *
	 * @param address Address you want to check if is inside the routing table
	 * @return true If the address is inside the routing table
	 * @return false If the address is not inside the routing table
	 */
	static bool hasAddressRoutingTable(uint16_t address);

	/**
	 * @brief Get the Next Hop address
	 *
	 * @param dst address of the next hop
	 * @return uint16_t address of the next hop
	 */
	static uint16_t getNextHop(uint16_t dst);

	/**
	 * @brief Get the Number Of Hops of the address inside the routing table
	 *
	 * @param address Address of the number of hops you want to know
	 * @return uint8_t Number of Hops or 0 if address not found in routing table.
	 */
	static uint8_t getNumberOfHops(uint16_t address);

	/**
	 * @brief Returns the routing table size
	 *
	 * @return size_t
	 */
	static size_t routingTableSize();

	/**
	 * @brief Process the network packet
	 *
	 * @param p Route Packet
	 */

	 /**
	  * @brief Process the network packet
	  *
	  * @param p Route Packet
	  * @param receivedSNR Received SNR
	  */
	static void processRoute(RoutePacket* p, int8_t receivedSNR);

	/**
	 * @brief Reset the SNR from the Route Node received
	 *
	 * @param src Source address
	 * @param receivedSNR Received SNR
	 */
	static void resetReceiveSNRRoutePacket(uint16_t src, int8_t receivedSNR);

	/**
	 * @brief Reset the SNR from the Route Node Sent
	 *
	 * @param src Source address
	 * @param sentSNR Sent SNR
	 */
	static void resetSentSNRRoutePacket(uint16_t src, int8_t sentSNR);

	/**
	 * @brief Checks all the routing entries for a route timeout and remove the entry.
	 *
	 */
	static void manageTimeoutRoutingTable();

private:

	/**
	 * @brief process the network node, adds the node in the routing table if can
	 *
	 * @param via via address
	 * @param node NetworkNode
	 */
	static void processRoute(uint16_t via, NetworkNode* node);

	/**
	 * @brief process the network node, adds the node in the routing table if can
	 *
	 * @param rNode route node
	 * @param via via address
	 * @param node NetworkNode
	 */
	static void processRoute(RouteNode* rNode, uint16_t via, NetworkNode* node);

	/**
	 * @brief Reset the timeout of the given node
	 *
	 * @param node node to be reset the timeout
	 */
	static void resetTimeoutRoutingNode(RouteNode* node);

	/**
	 * @brief Add node to the routing table
	 *
	 * @param node Network node that includes the address and the metric
	 * @param via Address to next hop to reach the network node address
	 */

	 /**
	  * @brief Add node to the routing table
	  *
	  * @param node Network node that includes the address and the metric
	  * @param via Address to next hop to reach the network node address
	  */
	static void addNodeToRoutingTable(NetworkNode* node, uint16_t via);

	/**
	 * @brief Get the Maximum Metric Of Routing Table. To prevent that some new entries are not added to the routing table.
	 *
	 * @return uint8_t Returns the maximum metric of the routing table
	 */
	static uint8_t calculateMaximumMetricOfRoutingTable();
};

#endif