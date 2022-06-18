#include "RoutingTableService.h"

RouteNode* RoutingTableService::findNode(uint16_t address) {
    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if (node->networkNode.address == address) {
                routingTableList->releaseInUse();
                return node;
            }

        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();
    return nullptr;
}

void RoutingTableService::processRoute(RoutePacket* p) {
    Log.verboseln(F("Route packet from %X with size %d"), p->src, p->payloadSize);

    // printPacket(p, true);

    NetworkNode* receivedNode = new NetworkNode(p->src, 1);
    processRoute(p->src, receivedNode);
    delete receivedNode;

    for (size_t i = 0; i < p->getPayloadLength(); i++) {
        NetworkNode* node = &p->routeNodes[i];
        node->metric++;
        processRoute(p->src, node);
    }

    // printRoutingTable();
}

void RoutingTableService::processRoute(uint16_t via, NetworkNode* node) {
    // if (node->address != localAddress) {
    //     routableNode* rNode = findNode(node->address);

    //     //If nullptr the node is not inside the routing table, then add it
    //     if (rNode == nullptr) {
    //         addNodeToRoutingTable(node, via);
    //         return;
    //     }

    //     //Update the metric and restart timeout if needed
    //     if (node->metric < rNode->networkNode.metric) {
    //         rNode->networkNode.metric = node->metric;
    //         rNode->via = via;
    //         resetTimeoutRoutingNode(rNode);
    //         Log.verboseln(F("Found better route for %X via %X metric %d"), node->address, via, node->metric);
    //     } else if (node->metric == rNode->networkNode.metric) {
    //         //Reset the timeout, only when the metric is the same as the actual route.
    //         resetTimeoutRoutingNode(rNode);
    //     }
    // }
}

size_t RoutingTableService::routingTableSize() {
    return routingTableList->getLength();
}

/**
 * @brief Get the All Network Nodes that are inside the routing table
 *
 * @return NetworkNode* All the nodes in a list.
 */
NetworkNode* RoutingTableService::getAllNetworkNodes() {
    routingTableList->setInUse();

    int routingSize = routingTableSize();

    NetworkNode* payload = new NetworkNode[routingSize];

    if (routingTableList->moveToStart()) {
        for (int i = 0; i < routingSize; i++) {
            RouteNode* currentNode = routingTableList->getCurrent();
            payload[i] = currentNode->networkNode;

            if (!routingTableList->next())
                break;
        }
    }

    routingTableList->releaseInUse();

    return payload;
}

// RoutePacket* RoutingTableService::createRoutingPacket() {
//     routingTableList->setInUse();

//     int routingSize = routingTableSize();

//     NetworkNode* payload = new NetworkNode[routingSize];

//     if (routingTableList->moveToStart()) {
//         for (int i = 0; i < routingSize; i++) {
//             RouteNode* currentNode = routingTableList->getCurrent();
//             payload[i] = currentNode->networkNode;

//             if (!routingTableList->next())
//                 break;
//         }
//     }

//     routingTableList->releaseInUse();

//     size_t routingSizeInBytes = routingSize * sizeof(NetworkNode);

//     Packet<uint8_t>* networkPacket = pS.createPacket(BROADCAST_ADDR, localAddress, HELLO_P, (uint8_t*) payload, routingSizeInBytes);
//     delete[]payload;
//     return networkPacket;
// }