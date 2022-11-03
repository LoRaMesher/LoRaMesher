#include "RoutingTableService.h"

size_t RoutingTableService::routingTableSize() {
    return routingTableList->getLength();
}

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

RouteNode* RoutingTableService::getBestNodeByRole(uint8_t role) {
    RouteNode* bestNode = nullptr;

    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if ((node->networkNode.role & role) == role &&
                (bestNode == nullptr || node->networkNode.metric > bestNode->networkNode.metric)) {
                bestNode = node;
            }

        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();
    return bestNode;
}

bool RoutingTableService::hasAddressRoutingTable(uint16_t address) {
    RouteNode* node = findNode(address);
    return node != nullptr;
}

uint16_t RoutingTableService::getNextHop(uint16_t dst) {
    RouteNode* node = findNode(dst);

    if (node == nullptr)
        return 0;

    return node->via;
}

uint8_t RoutingTableService::getNumberOfHops(uint16_t address) {
    RouteNode* node = findNode(address);

    if (node == nullptr)
        return 0;

    return node->networkNode.metric;
}

void RoutingTableService::processRoute(RoutePacket* p, int8_t receivedSNR) {
    size_t numNodes = p->getNetworkNodesSize();
    Log.verboseln(F("Route packet from %X with size %d"), p->src, numNodes);

    NetworkNode* receivedNode = new NetworkNode(p->src, 1, p->nodeRole);
    processRoute(p->src, receivedNode);
    delete receivedNode;

    resetReceiveSNRRoutePacket(p->src, receivedSNR);

    for (size_t i = 0; i < numNodes; i++) {
        NetworkNode* node = &p->networkNodes[i];
        node->metric++;
        processRoute(p->src, node);
    }

    printRoutingTable();
}

void RoutingTableService::resetReceiveSNRRoutePacket(uint16_t src, int8_t receivedSNR) {
    RouteNode* rNode = findNode(src);
    if (rNode == nullptr)
        return;

    Log.verboseln("Reset Receive SNR from %X: %d", src, receivedSNR);

    rNode->receivedSNR = receivedSNR;
}

void RoutingTableService::processRoute(uint16_t via, NetworkNode* node) {
    if (node->address != WiFiService::getLocalAddress()) {

        RouteNode* rNode = findNode(node->address);
        //If nullptr the node is not inside the routing table, then add it
        if (rNode == nullptr) {
            addNodeToRoutingTable(node, via);
            return;
        }

        //Update the metric and restart timeout if needed
        if (node->metric < rNode->networkNode.metric) {
            rNode->networkNode.metric = node->metric;
            rNode->via = via;
            resetTimeoutRoutingNode(rNode);
            Log.verboseln(F("Found better route for %X via %X metric %d"), node->address, via, node->metric);
        }
        else if (node->metric == rNode->networkNode.metric) {
            //Reset the timeout, only when the metric is the same as the actual route.
            resetTimeoutRoutingNode(rNode);
        }

        //Update the Role all the cases
        rNode->networkNode.role = node->role;
    }
}

void RoutingTableService::addNodeToRoutingTable(NetworkNode* node, uint16_t via) {
    if (routingTableList->getLength() >= RTMAXSIZE) {
        Log.warningln(F("Routing table max size reached, not adding route and deleting it"));
        return;
    }
    RouteNode* rNode = new RouteNode(node->address, node->metric, node->role, via);

    //Reset the timeout of the node
    resetTimeoutRoutingNode(rNode);

    routingTableList->setInUse();

    routingTableList->Append(rNode);

    routingTableList->releaseInUse();

    Log.verboseln(F("New route added: %X via %X metric %d, role %b"), node->address, via, node->metric, node->role);
}

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

void RoutingTableService::resetTimeoutRoutingNode(RouteNode* node) {
    node->timeout = millis() + DEFAULT_TIMEOUT * 1000;
}

void RoutingTableService::printRoutingTable() {
    Log.verboseln(F("Current routing table:"));

    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        size_t position = 0;

        do {
            RouteNode* node = routingTableList->getCurrent();

            Log.verboseln(F("%d - %X via %X metric %d Role %d"), position,
                node->networkNode.address,
                node->via,
                node->networkNode.metric,
                node->networkNode.role);

            position++;
        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();
}

void RoutingTableService::manageTimeoutRoutingTable() {
    Log.verboseln(F("Checking routes timeout"));

    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if (node->timeout < millis()) {
                Log.warningln(F("Route timeout %X via %X"), node->networkNode.address, node->via);

                delete node;
                routingTableList->DeleteCurrent();
            }

        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();

    printRoutingTable();
}

LM_LinkedList<RouteNode>* RoutingTableService::routingTableList = new LM_LinkedList<RouteNode>();