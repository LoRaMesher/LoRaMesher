#ifndef _LORAMESHER_ROUTING_TABLE_SERVICE_H
#define _LORAMESHER_ROUTING_TABLE_SERVICE_H

#include "BuildOptions.h"

#include "utilities/LinkedQueue.hpp"

#include "entities/routingTable/RouteNode.h"
#include "entities/routingTable/NetworkNode.h"
#include "entities/packets/RoutePacket.h"
#include "entities/routingTable/HelloPacketNode.h"
#include "entities/packets/HelloPacket.h"

#include "services/WiFiService.h"
#include "services/RoleService.h"
#include "services/PacketService.h"
#include <entities/packets/Packet.h>

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
	 * @brief Routing table Id, used to identify which routing table is being used and if it is updated
	 *
	 */
	static uint8_t routingTableId;

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
	 * @param block_routing_table block the routing table
	 * @return RouteNode* pointer to the RouteNode or nullptr
	 */
	static RouteNode* findNode(uint16_t address, bool block_routing_table = true);

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
	  * @return true If the routing table has been updated
	  */
	static bool processRoute(RoutePacket* p, int8_t receivedSNR);

	/**
	 * @brief Process the Hello Packet
	 *
	 * @param p Hello Packet
	 * @param receivedSNR Received SNR
	 * @param out_send_packet A pointer to the packet to be sent if necessary
	 * @return true If the route has been updated
	 */
	static bool processHelloPacket(HelloPacket* p, int8_t receivedSNR, Packet<uint8_t>** out_send_packet);

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
	 * @return true If the routing table has been updated
	 */
	static bool manageTimeoutRoutingTable();

	/**
	 * @brief Checks all the routing entries if the node has received a hello packet. If not, reduce the metric.
	 *
	 * @return true If the routing table has been updated
	 */
	static bool checkReceivedHelloPacket();

	/**
	 * @brief Penalize the node that has not received a hello packet. It will reduce the received link quality.
	 *
	 * @param address Address to be penalized
	 * @return true If the node has been penalized
	 */
	static bool penalizeNodeReceivedLinkQuality(uint16_t address);

	/**
	 * @brief Get the number of nodes that are at One Hop
	 *
	 * @return size_t Number of nodes at one hop
	 */
	static size_t oneHopSize();

	/**
	 * @brief Get all a Hello Packet, this will contain all the nodes at one hop
	 *
	 *
	 * @param helloPacketNode Hello Packet Node
	 * @param size Size of the Hello Packet Node
	 * @return true If the Hello Packet Node is returned
	 */
	static bool getAllHelloPacketsNode(HelloPacketNode** helloPacketNode, size_t* size);

	/**
	 * @brief Clear all the Hello Packets Node
	 *
	 * @param helloPacketNode Hello Packet Node
	 * @return true If the Hello Packet Node is cleared
	 * @return false If the Hello Packet Node is not cleared
	 */
	static bool clearAllHelloPacketsNode(HelloPacketNode* helloPacketNode);

private:

	/**
	 * @brief process the network node, adds the node in the routing table if can
	 *
	 * @param via via address
	 * @param node NetworkNode
	 * @param receivedSNR Received SNR
	 * @return true If the route has been updated
	 */
	static bool processRoute(uint16_t via, NetworkNode* node, int8_t receivedSNR);

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
	  * @param viaNode Route Node to next hop to reach the network node address
	  */
	static void addNodeToRoutingTable(NetworkNode* node, RouteNode* viaNode);

	/**
	 * @brief Get the Maximum Metric Of Routing Table. To prevent that some new entries are not added to the routing table.
	 *
	 * @return uint8_t Returns the maximum metric of the routing table
	 */
	static uint8_t calculateMaximumMetricOfRoutingTable();

	/**
	 * @brief Find the hello packet node
	 *
	 * @param helloPacketNode Hello Packet Node
	 * @param address Address to be found
	 * @return HelloPacketNode* pointer to the Hello Packet Node or nullptr
	 */
	static HelloPacketNode* findHelloPacketNode(HelloPacket* helloPacketNode, uint16_t address);

	/**
	 * @brief get the transmitted link quality
	 *
	 * @param helloPacketNode Hello Packet Node
	 * @return uint8_t Transmitted link quality
	 */
	static uint8_t get_transmitted_link_quality(HelloPacket* helloPacketNode);

	/**
	 * @brief Update the Node
	 *
	 * @param rNode Node to be updated
	 * @param hops Hops
	 * @param rlq Received Link Quality, normally from the via node or the node that sent the packet
	 * @param tlq Transmitted Link Quality, normally from the via node or the node that sent the packet
	 * @return true If the node has been updated
	 * @return false If the node has not been updated
	 */
	static bool updateNode(RouteNode* rNode, uint8_t hops, uint8_t rlq, uint8_t tlq);

	/**
	 * @brief Update the metric of the Route Node
	 *
	 * @param rNode Route Node
	 * @param hops Hops
	 * @param rlq Received Link Quality
	 * @param tlq Transmitted Link Quality
	 * @return true If the metric has been updated
	 */
	static bool updateMetric(RouteNode* rNode, uint8_t hops, uint8_t rlq, uint8_t tlq);

	/**
	 * @brief Calculate the metric
	 *
	 * @param previous_metric Previous Metric
	 * @param hops Hops
	 * @param rlq Received Link Quality
	 * @param tlq Transmitted Link Quality
	 * @return uint8_t Metric
	 */
	static uint8_t calculateMetric(uint8_t previous_metric, uint8_t hops, uint8_t rlq, uint8_t tlq);

	/**
	 * @brief Given a node, update the metric of all the nodes that are the next hop of the rNode
	 *
	 * @param rNode Route Node
	 * @return true If the metric has been updated
	 */
	static bool updateMetricOfNextHop(RouteNode* rNode);

	/**
	 * @brief Generates a new Random Routing Table Id
	 *
	 */
	static void generateNewRTId();

	/**
	 * @brief Get the Farthest Hop Count
	 *
	 * @return uint8_t Farthest Hop Count
	 */
	static uint8_t getFarthestHopCount();

	/**
	 * @brief Check if the Route Packet needs to delete an entry of the routing table.
	 * It will find the nodes that are not in the routing table and delete them.
	 *
	 * @param p Route Packet
	 * @return true If the route has deleted an entry
	 * @return false If the route has not deleted an entry
	 */
	static bool checkIfRoutePacketNeedToDeleteEntry(RoutePacket* p);

	/**
	 * @brief Reset the timeout of the nodes that are the next hop of the node
	 *
	 * @param node Route Node
	 */
	static void resetTimeoutOfNextHops(RouteNode* node);
};

#endif