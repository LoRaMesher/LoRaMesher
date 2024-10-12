#include "RoutingTableService.h"

size_t RoutingTableService::routingTableSize() {
    return routingTableList->getLength();
}

RouteNode* RoutingTableService::findNode(uint16_t address, bool block_routing_table) {

    if (block_routing_table)
        routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if (node->networkNode.address == address) {
                if (block_routing_table)
                    routingTableList->releaseInUse();
                return node;
            }

        } while (routingTableList->next());
    }

    if (block_routing_table)
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
                (bestNode == nullptr || node->networkNode.metric < bestNode->networkNode.metric)) {
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

HelloPacketNode* RoutingTableService::findHelloPacketNode(HelloPacket* helloPacketNode, uint16_t address) {
    for (size_t i = 0; i < helloPacketNode->getHelloPacketNodesSize(); i++) {
        if (helloPacketNode->helloPacketNodes[i].address == address) {
            return &helloPacketNode->helloPacketNodes[i];
        }
    }

    return nullptr;
}

uint8_t RoutingTableService::get_transmitted_link_quality(HelloPacket* helloPacket) {
    HelloPacketNode* helloPacketNode = findHelloPacketNode(helloPacket, WiFiService::getLocalAddress());
    if (helloPacketNode == nullptr) {
        ESP_LOGW(LM_TAG, "Hello packet node not found");
        return LM_MAX_METRIC;
    }

    return helloPacketNode->received_link_quality;
}

void RoutingTableService::updateMetricOfNextHop(RouteNode* rNode) {
    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if (node->via == rNode->networkNode.address && node->networkNode.address != rNode->networkNode.address) {
                bool updated = updateMetric(node, node->networkNode.hop_count, rNode->received_link_quality, rNode->transmitted_link_quality);
                if (updated)
                    routingTableId++;
            }

        } while (routingTableList->next());
    }
}

uint8_t RoutingTableService::calculateMetric(uint8_t previous_metric, uint8_t hops, uint8_t rlq, uint8_t tlq) {
    uint8_t factor_hops = LM_REDUCED_FACTOR_HOP_COUNT * hops * LM_MAX_METRIC;

    printf("Factor hops: %d\n", factor_hops);

    uint8_t quality_link = (rlq + tlq) / 2;

    printf("Received link quality: %d\n", rlq);
    printf("Transmitted link quality: %d\n", tlq);

    printf("Quality link: %d\n", quality_link);
    printf("Previous metric: %d\n", previous_metric);

    uint8_t new_metric = 0;

    if (hops == 1) {
        // When the node is at 1 hop, the metric is calculated by the quality of the link, the previous metric is irrelevant
        new_metric = std::min(factor_hops, quality_link);
    }
    else {
        uint8_t factor_link_quality = LM_MAX_METRIC / std::sqrt((LM_MAX_METRIC / std::max(previous_metric, uint8_t(1))) ^ 2 + (LM_MAX_METRIC / std::max(quality_link, uint8_t(1))) ^ 2);

        printf("Factor link quality: %d\n", factor_link_quality);

        // Update the received link quality
        new_metric = std::min(factor_hops, factor_link_quality);
    }

    printf("New metric: %d\n", new_metric);

    return new_metric;
}

bool RoutingTableService::updateNode(RouteNode* rNode, uint8_t hops, uint8_t rlq, uint8_t tlq) {
    resetTimeoutRoutingNode(rNode);

    rNode->bitList->addBit(false);

    bool updated = updateMetric(rNode, hops, rlq, tlq);
    return updated;
}

bool RoutingTableService::updateMetric(RouteNode* rNode, uint8_t hops, uint8_t rlq, uint8_t tlq) {
    bool updated = false;
    if (rNode->networkNode.hop_count != hops) {
        rNode->networkNode.hop_count = hops;
        updated = true;
    }

    uint8_t new_metric = calculateMetric(rNode->receivedMetric, hops, rlq, tlq);

    // TODO: Use some kind of hysteresis to avoid oscillations?
    if (rNode->networkNode.metric != new_metric) {
        rNode->networkNode.metric = new_metric;
        updated = true;
        ESP_LOGV(LM_TAG, "Metric for node %X via %x updated from %d to %d", rNode->networkNode.address, rNode->via, rNode->receivedMetric, new_metric);
    }

    return updated;

}

bool RoutingTableService::processHelloPacket(HelloPacket* p, int8_t receivedSNR, Packet<uint8_t>** out_send_packet) {
    ESP_LOGI(LM_TAG, "Hello packet from %X with RTId %d and size %d", p->src, p->routingTableId, p->routingTableSize);

    if (p->routingTableId < routingTableId) {
        ESP_LOGI(LM_TAG, "Hello packet from %X with old RTId %d", p->src, p->routingTableId);
        return false;
    }

    bool updated = false;

    uint8_t transmitted_link_quality = get_transmitted_link_quality(p);

    printf("Transmitted link quality: %d\n", transmitted_link_quality);

    routingTableList->setInUse();

    uint8_t factor_hops = LM_REDUCED_FACTOR_HOP_COUNT * 1 * LM_MAX_METRIC;

    bool block_routing_table = false;
    RouteNode* rNode = findNode(p->src, block_routing_table);
    if (rNode == nullptr) {
        rNode = new RouteNode(p->src, factor_hops, ROLE_DEFAULT, p->src, 1, LM_MAX_METRIC, transmitted_link_quality, LM_MAX_METRIC);

        updateNode(rNode, 1, rNode->received_link_quality, transmitted_link_quality);
        rNode->hasReceivedHelloPacket = true;

        // Add to the routing table
        routingTableList->Append(rNode);
        routingTableList->releaseInUse();

        routingTableId++;

        updated = true;
    }
    else {
        rNode->transmitted_link_quality = transmitted_link_quality;
        rNode->hasReceivedHelloPacket = true;

        bool updated = updateNode(rNode, 1, rNode->received_link_quality, transmitted_link_quality);

        rNode->receivedSNR = receivedSNR;

        routingTableList->releaseInUse();

        if (updated) {
            updateMetricOfNextHop(rNode);

            routingTableId++;
        }

        printRoutingTable();
    }


    if (p->routingTableId > routingTableId || p->routingTableSize != routingTableSize()) {
        ESP_LOGI(LM_TAG, "Hello packet from %X with different RTId %d or RTSize", p->src, p->routingTableId, p->routingTableSize);
        ESP_LOGV(LM_TAG, "Current RTId %d and RTSize %d", routingTableId, routingTableSize());
        // Ask for the updated routing table
        RTRequestPacket* rtRequestPacket = PacketService::createRoutingTableRequestPacket(p->src, WiFiService::getLocalAddress());

        *out_send_packet = reinterpret_cast<Packet<uint8_t>*>(rtRequestPacket);
    }

    return updated;
}

bool RoutingTableService::processRoute(RoutePacket* p, int8_t receivedSNR) {
    if ((p->packetSize - sizeof(RoutePacket)) % sizeof(NetworkNode) != 0) {
        ESP_LOGE(LM_TAG, "Invalid route packet size");
        return false;
    }

    bool rt_updated = false;

    routingTableList->setInUse();

    size_t numNodes = p->getNetworkNodesSize();
    ESP_LOGI(LM_TAG, "Route packet from %X with RTId %d size %d", p->src, p->routingTableId, numNodes);

    NetworkNode* receivedNode = new NetworkNode(p->src, LM_MAX_METRIC, p->nodeRole, 1);
    rt_updated = processRoute(p->src, receivedNode, receivedSNR);
    delete receivedNode;

    for (size_t i = 0; i < numNodes; i++) {
        NetworkNode* node = &p->networkNodes[i];
        node->hop_count++;
        rt_updated = rt_updated || processRoute(p->src, node, receivedSNR);
    }

    routingTableList->releaseInUse();

    printRoutingTable();

    return rt_updated;
}

void RoutingTableService::resetReceiveSNRRoutePacket(uint16_t src, int8_t receivedSNR) {
    RouteNode* rNode = findNode(src);
    if (rNode == nullptr)
        return;

    ESP_LOGI(LM_TAG, "Reset Receive SNR from %X: %d", src, receivedSNR);

    rNode->receivedSNR = receivedSNR;
}

bool RoutingTableService::processRoute(uint16_t via, NetworkNode* node, int8_t receivedSNR) {
    if (node->address == WiFiService::getLocalAddress())
        return false;

    bool block_routing_table = false;
    RouteNode* viaNode = findNode(via, block_routing_table);
    if (viaNode == nullptr) {
        ESP_LOGW(LM_TAG, "Via node not found in the routing table");
        return false;
    }

    RouteNode* rNode = findNode(node->address, block_routing_table);
    //If nullptr the node is not inside the routing table, then add it
    if (rNode == nullptr) {
        addNodeToRoutingTable(node, viaNode);
        routingTableId++;
        return true;
    }

    resetTimeoutRoutingNode(rNode);

    bool updated = false;

    // If the node is the same as the via, then the route is the same
    if (rNode->via == via) {
        rNode->receivedMetric = node->metric;
        updated = updateMetric(rNode, node->hop_count, viaNode->received_link_quality, viaNode->transmitted_link_quality);
        updateMetricOfNextHop(rNode);

    }
    else {
        // If the node is not the same as the via, then check if the new route is better
        uint8_t new_metric = calculateMetric(node->metric, node->hop_count, viaNode->received_link_quality, viaNode->transmitted_link_quality);

        if (new_metric <= rNode->networkNode.metric) {
            printf("New metric is not better for %X via %X metric %d\n", node->address, via, node->metric);
            updated = false;
        }
        else {
            rNode->networkNode.metric = new_metric;
            rNode->networkNode.hop_count = node->hop_count;
            rNode->receivedMetric = node->metric;
            rNode->via = via;
            ESP_LOGI(LM_TAG, "Found better route for %X via %X metric %d", node->address, via, node->metric);
            updated = true;
        }
    }

    if (updated) {
        routingTableId++;
    }

    if (node->role != rNode->networkNode.role) {
        ESP_LOGI(LM_TAG, "Updating role of %X to %d", node->address, node->role);
        rNode->networkNode.role = node->role;
        updated = true;
        routingTableId++;
    }

    return updated;
}

void RoutingTableService::addNodeToRoutingTable(NetworkNode* node, RouteNode* viaNode) {
    if (routingTableList->getLength() >= RTMAXSIZE) {
        ESP_LOGW(LM_TAG, "Routing table max size reached, not adding route and deleting it");
        return;
    }

    RouteNode* rNode = new RouteNode(node->address, LM_MAX_METRIC, node->role, viaNode->networkNode.address, node->hop_count, LM_MAX_METRIC, 0, node->metric);
    updateMetric(rNode, node->hop_count, viaNode->received_link_quality, viaNode->transmitted_link_quality);

    resetTimeoutRoutingNode(rNode);

    // Add to the routing table
    routingTableList->Append(rNode);

    ESP_LOGI(LM_TAG, "New route added: %X via %X metric %d, role %d", node->address, viaNode->networkNode.address, node->metric, node->role);
}

NetworkNode* RoutingTableService::getAllNetworkNodes() {
    routingTableList->setInUse();

    int routingSize = routingTableSize();

    // If the routing table is empty return nullptr
    if (routingSize == 0) {
        routingTableList->releaseInUse();
        return nullptr;
    }

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
    node->timeout = millis() + LM_RT_TIMEOUT * 1000;
}

void RoutingTableService::printRoutingTable() {
    ESP_LOGI(LM_TAG, "Current routing table:");

    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        size_t position = 0;

        ESP_LOGI(LM_TAG, "---------- RTId %d and size: %d ----------", routingTableId, routingTableSize());

        do {
            RouteNode* node = routingTableList->getCurrent();

            ESP_LOGI(LM_TAG, "%d - %X via %X metric %d hop_count %d role %d",
                position,
                node->networkNode.address,
                node->via,
                node->networkNode.metric,
                node->networkNode.hop_count,
                node->networkNode.role);

            position++;
        } while (routingTableList->next());

        ESP_LOGI(LM_TAG, "--------------------------------------------");
    }

    routingTableList->releaseInUse();
}

void RoutingTableService::manageTimeoutRoutingTable() {
    ESP_LOGI(LM_TAG, "Checking routes timeout");

    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if (node->timeout < millis()) {
                ESP_LOGW(LM_TAG, "Route timeout %X via %X", node->networkNode.address, node->via);

                delete node;
                routingTableList->DeleteCurrent();
            }

        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();
}

bool RoutingTableService::checkReceivedHelloPacket() {
    bool rt_updated = false;

    routingTableList->setInUse();

    size_t routingTableSize = routingTableList->getLength();

    size_t nodes_to_penalize_size = 0;
    uint16_t nodes_to_penalize[routingTableSize];

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if (node->networkNode.hop_count != 1)
                continue;

            if (!node->hasReceivedHelloPacket) {
                nodes_to_penalize[nodes_to_penalize_size] = node->networkNode.address;
                nodes_to_penalize_size++;
            }
            else {
                node->hasReceivedHelloPacket = false;
            }

        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();

    for (size_t i = 0; i < nodes_to_penalize_size; i++) {
        RouteNode* node = findNode(nodes_to_penalize[i]);

        ESP_LOGW(LM_TAG, "Route %X via %X has not received hello packet", node->networkNode.address, node->via);

        rt_updated = rt_updated || penalizeNodeReceivedLinkQuality(node->networkNode.address);
    }

    return rt_updated;
}

bool RoutingTableService::penalizeNodeReceivedLinkQuality(uint16_t address) {
    routingTableList->setInUse();

    bool block_routing_table = false;
    RouteNode* node = findNode(address, block_routing_table);
    if (node == nullptr)
        return false;

    if (node->networkNode.hop_count != 1)
        return false;

    ESP_LOGV(LM_TAG, "Penalizing node %X via %X", address, node->via);
    ESP_LOGV(LM_TAG, "Metric before: %d", node->networkNode.metric);

    uint8_t penalization = 255 / LM_QUALITY_WINDOWS_SIZE;

    node->bitList->addBit(true);

    size_t not_received_packets = node->bitList->countBits();

    node->received_link_quality = std::max(255 - not_received_packets * penalization, static_cast<size_t>(0));

    bool updated = updateMetric(node, node->networkNode.hop_count, node->received_link_quality, node->transmitted_link_quality);
    if (updated) {
        routingTableId++;
        updateMetricOfNextHop(node);
    }
    ESP_LOGV(LM_TAG, "Metric after: %d", node->networkNode.metric);

    routingTableList->releaseInUse();

    return updated;
}

uint8_t RoutingTableService::calculateMaximumMetricOfRoutingTable() {
    routingTableList->setInUse();

    uint8_t maximumMetricOfRoutingTable = 0;

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if (node->networkNode.metric > maximumMetricOfRoutingTable)
                maximumMetricOfRoutingTable = node->networkNode.metric;

        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();

    return maximumMetricOfRoutingTable + 1;
}

size_t RoutingTableService::oneHopSize() {
    size_t oneHopSize = 0;

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if (node->networkNode.hop_count == 1)
                oneHopSize++;

        } while (routingTableList->next());
    }

    return oneHopSize;
}

bool RoutingTableService::getAllHelloPacketsNode(HelloPacketNode** helloPacketNode, size_t* size) {
    routingTableList->setInUse();

    size_t oneHop = oneHopSize();
    *size = 0;

    // If the routing table is empty return nullptr
    if (oneHop == 0) {
        routingTableList->releaseInUse();
        return true;
    }

    *size = oneHop;

    *helloPacketNode = new HelloPacketNode[oneHop];
    if (*helloPacketNode == nullptr) {
        routingTableList->releaseInUse();
        return false;
    }

    if (routingTableList->moveToStart()) {
        size_t position = 0;

        do {
            RouteNode* node = routingTableList->getCurrent();

            if (node->networkNode.hop_count == 1) {
                (*helloPacketNode)[position] = HelloPacketNode(node->networkNode.address, node->received_link_quality);
                position++;
            }

        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();

    return true;
}

bool RoutingTableService::clearAllHelloPacketsNode(HelloPacketNode* helloPacketNode) {
    if (helloPacketNode == nullptr)
        return true;

    delete[] helloPacketNode;

    return true;
}

LM_LinkedList<RouteNode>* RoutingTableService::routingTableList = new LM_LinkedList<RouteNode>();
uint8_t RoutingTableService::routingTableId = 0;
