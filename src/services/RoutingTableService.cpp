#include "RoutingTableService.h"
#include "RouteUpdateService.h"

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
    uint16_t bestETX = ETX_MAX_VALUE * 2; // Start with worst possible ETX

    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if ((node->networkNode.role & role) == role) {
                uint16_t totalETX = node->networkNode.reverseETX + node->networkNode.forwardETX;
                if (bestNode == nullptr || totalETX < bestETX) {
                    bestNode = node;
                    bestETX = totalETX;
                }
            }

        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();
    return bestNode;
}

RouteNode* RoutingTableService::findWorstRoute() {
    RouteNode* worstNode = nullptr;
    uint16_t worstETX = 0; // Start with best possible ETX

    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();
            uint16_t totalETX = node->networkNode.reverseETX + node->networkNode.forwardETX;

            if (worstNode == nullptr || totalETX > worstETX) {
                worstNode = node;
                worstETX = totalETX;
            }

        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();
    return worstNode;
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

    // Return total ETX (this function name is misleading now, but kept for compatibility)
    return getTotalETX(address);
}

void RoutingTableService::processRoute(RoutePacket* p, int8_t receivedSNR) {
    // Duplicate detection - check if we've already processed this packet
    if (RouteUpdateService::isDuplicatePacket(p->src, p->id)) {
        ESP_LOGD(LM_TAG, "Duplicate route packet from %X id=%d - ignoring", p->src, p->id);
        return;
    }

    // Record this packet to prevent future duplicates
    RouteUpdateService::recordPacket(p->src, p->id);

    if ((p->packetSize - sizeof(RoutePacket)) % sizeof(NetworkNode) != 0) {
        ESP_LOGE(LM_TAG, "Invalid route packet size");
        return;
    }

    size_t numNodes = p->getNetworkNodesSize();
    ESP_LOGI(LM_TAG, "Route packet from %X with size %d", p->src, numNodes);

    // Process direct sender as 1-hop neighbor
    RouteNode* senderNode = findNode(p->src);
    if (senderNode != nullptr) {
        // Increment received hello counter for ETX tracking
        senderNode->helloPacketsReceived++;
        ESP_LOGV(LM_TAG, "Hello from %X: received=%d/%d (%.1f%%)",
                 p->src, senderNode->helloPacketsReceived,
                 senderNode->helloPacketsExpected,
                 senderNode->helloPacketsExpected > 0 ?
                 (100.0 * senderNode->helloPacketsReceived / senderNode->helloPacketsExpected) : 0.0);

        // Reset timeout - hello packet received means route is alive
        resetTimeoutRoutingNode(senderNode);
    } else {
        // New neighbor - create route with conservative bootstrap ETX = 1.5 and hop_count = 1
        NetworkNode* receivedNode = new NetworkNode(p->src, ETX_BOOTSTRAP_VALUE, ETX_BOOTSTRAP_VALUE, p->nodeRole, 1);
        processRoute(p->src, receivedNode);
        delete receivedNode;

        // Initialize ETX counters for bootstrap
        senderNode = findNode(p->src);
        if (senderNode != nullptr) {
            senderNode->helloPacketsExpected = 1;
            senderNode->helloPacketsReceived = 1;
        }
    }

    // Update SNR
    resetReceiveSNRRoutePacket(p->src, receivedSNR);

    // Calculate sender's link ETX for propagating routes
    uint8_t senderReverseETX = calculateReverseETX(p->src);
    uint8_t senderForwardETX = ETX_BOOTSTRAP_VALUE;  // Default to bootstrap

    // Extract forward ETX from neighbor's routing table (hello-based approach)
    // The neighbor reports their reverse ETX to us, which is our forward ETX to them
    bool foundOurselfInNeighborTable = false;
    for (size_t i = 0; i < numNodes; i++) {
        NetworkNode* node = &p->networkNodes[i];

        if (node->address == WiFiService::getLocalAddress() && node->hop_count == 1) {
            // Found ourselves in neighbor's routing table as direct neighbor
            // Their reverse ETX to us = our forward ETX to them
            senderForwardETX = node->reverseETX;
            foundOurselfInNeighborTable = true;

            ESP_LOGD(LM_TAG, "Extracted forward ETX to %X: %.1f (from neighbor's hello)",
                     p->src, node->reverseETX / 10.0);
            break;
        }
    }

    if (!foundOurselfInNeighborTable) {
        ESP_LOGV(LM_TAG, "Neighbor %X hasn't reported us yet, using bootstrap forward ETX",
                 p->src);
    }

    // Update the sender's routing table entry with calculated ETX values
    // Only update when we have enough samples - otherwise preserve bootstrap value
    if (senderNode != nullptr && senderNode->networkNode.hop_count == 1) {
        // Update reverse ETX only if we have enough hello packet samples
        if (senderNode->helloPacketsExpected >= MIN_ETX_SAMPLES) {
            senderNode->networkNode.reverseETX = senderReverseETX;
        }

        // Update forward ETX with extracted value from neighbor's hello
        // (or bootstrap if neighbor hasn't reported us yet)
        senderNode->networkNode.forwardETX = senderForwardETX;

        ESP_LOGV(LM_TAG, "ETX for %X: R=%.1f F=%.1f (hellos=%d/%d) [F %s]",
                 p->src,
                 senderNode->networkNode.reverseETX / 10.0,
                 senderNode->networkNode.forwardETX / 10.0,
                 senderNode->helloPacketsReceived,
                 senderNode->helloPacketsExpected,
                 foundOurselfInNeighborTable ? "extracted" : "bootstrap");
    }

    // Process advertised routes
    for (size_t i = 0; i < numNodes; i++) {
        NetworkNode* node = &p->networkNodes[i];

        // Add sender's link ETX to the advertised route ETX
        uint16_t newReverseETX = node->reverseETX + senderReverseETX;
        uint16_t newForwardETX = node->forwardETX + senderForwardETX;

        // Clamp to maximum value
        node->reverseETX = (newReverseETX > ETX_MAX_VALUE) ? ETX_MAX_VALUE : (uint8_t)newReverseETX;
        node->forwardETX = (newForwardETX > ETX_MAX_VALUE) ? ETX_MAX_VALUE : (uint8_t)newForwardETX;

        // Increment hop count (add one hop through the sender)
        node->hop_count = node->hop_count + 1;

        processRoute(p->src, node);
    }

    printRoutingTable();
}

void RoutingTableService::resetReceiveSNRRoutePacket(uint16_t src, int8_t receivedSNR) {
    RouteNode* rNode = findNode(src);
    if (rNode == nullptr)
        return;

    ESP_LOGI(LM_TAG, "Reset Receive SNR from %X: %d", src, receivedSNR);

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

        // Calculate total ETX for new route and current route
        uint16_t newTotalETX = node->reverseETX + node->forwardETX;
        uint16_t currentTotalETX = rNode->networkNode.reverseETX + rNode->networkNode.forwardETX;

        // Apply hysteresis: new route must be significantly better to switch
        float hysteresisThreshold = currentTotalETX / ETX_HYSTERESIS;

        //Update the route if new ETX is significantly better
        if (newTotalETX < hysteresisThreshold) {
            uint8_t oldReverseETX = rNode->networkNode.reverseETX;
            uint8_t oldForwardETX = rNode->networkNode.forwardETX;

            rNode->networkNode.reverseETX = node->reverseETX;
            rNode->networkNode.forwardETX = node->forwardETX;
            rNode->via = via;
            resetTimeoutRoutingNode(rNode);

            ESP_LOGI(LM_TAG, "Found better route for %X via %X: old_ETX=%.1f+%.1f=%.1f new_ETX=%.1f+%.1f=%.1f",
                     node->address, via,
                     oldReverseETX / 10.0, oldForwardETX / 10.0, currentTotalETX / 10.0,
                     node->reverseETX / 10.0, node->forwardETX / 10.0, newTotalETX / 10.0);

            // Trigger update hook - Route metric improved significantly
            // Note: This will be called from LoraMesher once we add the sendTriggeredHelloPacket function
            // For now, just log it
            ESP_LOGD(LM_TAG, "Route improvement trigger: %X (would trigger update)", node->address);
        }
        else if (newTotalETX <= currentTotalETX * 1.05) {
            // Route is approximately the same quality - just reset timeout
            resetTimeoutRoutingNode(rNode);
            ESP_LOGV(LM_TAG, "Route refresh for %X via %X ETX=%.1f",
                     node->address, via, newTotalETX / 10.0);
        }
        else {
            // Route degraded - check if still usable before resetting timeout
            if (newTotalETX > ETX_UNUSABLE_THRESHOLD) {
                // Route quality too poor - don't reset timeout, let it expire
                ESP_LOGW(LM_TAG, "Route too degraded, allowing timeout: %X ETX %.1f -> %.1f (threshold %.1f)",
                         node->address, currentTotalETX / 10.0, newTotalETX / 10.0,
                         ETX_UNUSABLE_THRESHOLD / 10.0);
            } else {
                // Route degraded but still usable - keep it alive
                resetTimeoutRoutingNode(rNode);
                ESP_LOGV(LM_TAG, "Route degraded for %X but still usable, ETX %.1f -> %.1f",
                         node->address, currentTotalETX / 10.0, newTotalETX / 10.0);
            }
        }

        // Update the Role from:
        // 1. Active route (current next hop) - most likely to be fresh
        // 2. Direct neighbor (1-hop) - authoritative source
        bool isDirectNeighbor = (node->hop_count == 1 && via == node->address);
        bool isActiveRoute = (getNextHop(node->address) == via);

        if ((isActiveRoute || isDirectNeighbor) && node->role != rNode->networkNode.role) {
            ESP_LOGI(LM_TAG, "Updating role of %X: %d -> %d (source: %s)",
                     node->address,
                     rNode->networkNode.role,
                     node->role,
                     isDirectNeighbor ? "direct neighbor" : "active route");
            rNode->networkNode.role = node->role;
        }
    }
}

void RoutingTableService::addNodeToRoutingTable(NetworkNode* node, uint16_t via) {
    // Calculate total ETX for this route
    uint16_t totalETX = node->reverseETX + node->forwardETX;

    // Check if routing table is full
    if (routingTableList->getLength() >= RTMAXSIZE) {
        // Try to evict worst route to make room for better route
        RouteNode* worstRoute = findWorstRoute();

        if (worstRoute == nullptr) {
            ESP_LOGW(LM_TAG, "Routing table full but no eviction candidate found");
            return;
        }

        uint16_t worstETX = worstRoute->networkNode.reverseETX + worstRoute->networkNode.forwardETX;

        // Only evict if new route is meaningfully better (at least 1.0 ETX improvement)
        // OR if worst route is unusable (above threshold)
        if (totalETX < worstETX - ETX_MIN_VALUE || worstETX > ETX_UNUSABLE_THRESHOLD) {
            ESP_LOGW(LM_TAG, "Evicting route %X (ETX=%.1f) for new route %X (ETX=%.1f)",
                     worstRoute->networkNode.address, worstETX / 10.0,
                     node->address, totalETX / 10.0);

            // Remove worst route
            routingTableList->setInUse();
            if (routingTableList->moveToStart()) {
                do {
                    RouteNode* current = routingTableList->getCurrent();
                    if (current == worstRoute) {
                        delete current;
                        routingTableList->DeleteCurrent();
                        break;
                    }
                } while (routingTableList->next());
            }
            routingTableList->releaseInUse();
        } else {
            ESP_LOGW(LM_TAG, "New route %X (ETX=%.1f) not better than worst route (ETX=%.1f), not adding",
                     node->address, totalETX / 10.0, worstETX / 10.0);
            return;
        }
    }

    // Check if this is a direct neighbor (1-hop)
    bool isDirectNeighbor = (node->hop_count == 1);

    // Apply ETX threshold only to multi-hop routes (direct neighbors always accepted)
    if (!isDirectNeighbor && calculateMaximumMetricOfRoutingTable() < totalETX) {
        ESP_LOGW(LM_TAG, "Trying to add multi-hop route with ETX higher than maximum, not adding (ETX=%.1f)", totalETX / 10.0);
        return;
    }

    // Log if adding direct neighbor with high ETX
    if (isDirectNeighbor && totalETX > calculateMaximumMetricOfRoutingTable()) {
        ESP_LOGI(LM_TAG, "Adding direct neighbor despite high ETX (ETX=%.1f)", totalETX / 10.0);
    }

    // Use legacy constructor (accepts metric which gets split into reverseETX and forwardETX equally)
    // For new routes, we have proper ETX values from the NetworkNode
    RouteNode* rNode = new RouteNode(node->address, node->reverseETX, node->role, via);

    // Set the correct ETX values and hop_count
    rNode->networkNode.reverseETX = node->reverseETX;
    rNode->networkNode.forwardETX = node->forwardETX;
    rNode->networkNode.hop_count = node->hop_count;

    //Reset the timeout of the node
    resetTimeoutRoutingNode(rNode);

    routingTableList->setInUse();

    routingTableList->Append(rNode);

    routingTableList->releaseInUse();

    ESP_LOGI(LM_TAG, "New route added: %X via %X reverse_ETX=%.1f forward_ETX=%.1f total_ETX=%.1f hops=%d role=%d",
             node->address, via,
             node->reverseETX / 10.0, node->forwardETX / 10.0, totalETX / 10.0,
             node->hop_count, node->role);

    // Trigger update hook - New route discovered
    ESP_LOGD(LM_TAG, "New route trigger: %X (would trigger update)", node->address);
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
    node->timeout = millis() + DEFAULT_TIMEOUT * 1000;
}

void RoutingTableService::aMessageHasBeenReceivedBy(uint16_t address) {
    RouteNode* rNode = findNode(address);
    if (rNode == nullptr)
        return;

    resetTimeoutRoutingNode(rNode);
}

void RoutingTableService::printRoutingTable() {
    ESP_LOGI(LM_TAG, "Current routing table:");

    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        size_t position = 0;

        do {
            RouteNode* node = routingTableList->getCurrent();

            uint16_t totalETX = node->networkNode.reverseETX + node->networkNode.forwardETX;
            float asymmetry = (node->networkNode.forwardETX > 0) ?
                              (float)node->networkNode.reverseETX / node->networkNode.forwardETX : 1.0;

            ESP_LOGI(LM_TAG, "%d - %X via %X ETX=%.1f (R:%.1f+F:%.1f) asym:%.2f hops:%d Role:%d", position,
                node->networkNode.address,
                node->via,
                totalETX / 10.0,
                node->networkNode.reverseETX / 10.0,
                node->networkNode.forwardETX / 10.0,
                asymmetry,
                node->networkNode.hop_count,
                node->networkNode.role);

            // For direct neighbors, also show reception statistics
            if (node->networkNode.hop_count == 1 && node->helloPacketsExpected > 0) {
                float receptionRate = (float)node->helloPacketsReceived / node->helloPacketsExpected * 100.0;
                ESP_LOGI(LM_TAG, "    └─ Direct: hellos=%d/%d (%.1f%%) SNR=%d",
                         node->helloPacketsReceived, node->helloPacketsExpected,
                         receptionRate, node->receivedSNR);
            }

            position++;
        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();
}

void RoutingTableService::manageTimeoutRoutingTable() {
    ESP_LOGI(LM_TAG, "Checking routes timeout");

    bool routeDeleted = false;
    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if (node->timeout < millis()) {
                ESP_LOGW(LM_TAG, "Route timeout %X via %X ETX=%.1f+%.1f",
                         node->networkNode.address, node->via,
                         node->networkNode.reverseETX / 10.0,
                         node->networkNode.forwardETX / 10.0);

                routeDeleted = true;

                delete node;
                routingTableList->DeleteCurrent();
            }

        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();

    // Trigger update hook if any routes were deleted
    if (routeDeleted) {
        ESP_LOGD(LM_TAG, "Route deletion trigger (would trigger update)");
    }

    printRoutingTable();
}

uint8_t RoutingTableService::calculateMaximumMetricOfRoutingTable() {
    routingTableList->setInUse();

    uint16_t maximumETX = 0;

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            uint16_t totalETX = node->networkNode.reverseETX + node->networkNode.forwardETX;
            if (totalETX > maximumETX)
                maximumETX = totalETX;

        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();

    // If routing table is empty, use bootstrap threshold to accept initial routes
    if (maximumETX == 0) {
        return ETX_BOOTSTRAP_THRESHOLD;
    }

    // Add some headroom for new routes
    uint16_t result = maximumETX + ETX_MIN_VALUE;
    if (result > ETX_MAX_VALUE) {
        return ETX_MAX_VALUE;
    }

    return (uint8_t)result;
}

uint8_t RoutingTableService::calculateReverseETX(uint16_t address) {
    RouteNode* node = findNode(address);

    if (node == nullptr || node->networkNode.hop_count != 1) {
        // Not a direct neighbor (multi-hop route), return default perfect link
        return ETX_MIN_VALUE; // ETX = 1.0
    }

    // Require minimum sample size for reliability
    if (node->helloPacketsExpected < MIN_ETX_SAMPLES) {
        return ETX_BOOTSTRAP_VALUE; // ETX = 1.5 (conservative bootstrap)
    }

    // Calculate reception rate
    float receptionRate = (float)node->helloPacketsReceived / (float)node->helloPacketsExpected;

    // Prevent division by zero and cap maximum ETX
    if (receptionRate < 0.04) {
        receptionRate = 0.04; // Cap at ETX = 25.0
    }

    // Calculate ETX scaled by ETX_SCALE_FACTOR
    float etx = 1.0 / receptionRate;
    float scaledETX = etx * ETX_SCALE_FACTOR;

    // Clamp to valid range BEFORE casting to uint8_t
    if (scaledETX < ETX_MIN_VALUE) scaledETX = ETX_MIN_VALUE;
    if (scaledETX > ETX_MAX_VALUE) scaledETX = ETX_MAX_VALUE;

    uint8_t metric = (uint8_t)scaledETX;

    ESP_LOGV(LM_TAG, "Reverse ETX for %X: received=%d expected=%d rate=%.2f%% ETX=%.2f metric=%d",
             address, node->helloPacketsReceived, node->helloPacketsExpected,
             receptionRate * 100.0, etx, metric);

    return metric;
}

uint8_t RoutingTableService::calculateForwardETX(uint16_t address) {
    // NOTE: Forward ETX is now extracted from neighbor's hello packets (hello-based approach)
    // This function only provides bootstrap value as fallback
    // The neighbor reports their reverse ETX to us, which is our forward ETX to them

    RouteNode* node = findNode(address);

    if (node == nullptr || node->networkNode.hop_count != 1) {
        // Not a direct neighbor (multi-hop route), return default perfect link
        return ETX_MIN_VALUE; // ETX = 1.0
    }

    // Return bootstrap value until neighbor reports us in their hello
    return ETX_BOOTSTRAP_VALUE; // ETX = 1.5 (conservative bootstrap)
}

void RoutingTableService::updateExpectedHelloPackets() {
    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            // Only update for direct neighbors (1-hop)
            if (node->networkNode.hop_count == 1) {
                node->helloPacketsExpected++;

                // Apply exponential decay when threshold is reached
                // This prevents overflow and gives more weight to recent measurements
                if (node->helloPacketsExpected >= ETX_DECAY_THRESHOLD) {
                    node->helloPacketsExpected = (uint16_t)(node->helloPacketsExpected * ETX_DECAY_FACTOR);
                    node->helloPacketsReceived = (uint16_t)(node->helloPacketsReceived * ETX_DECAY_FACTOR);

                    ESP_LOGV(LM_TAG, "Applied ETX decay to %X: exp=%d rcv=%d",
                             node->networkNode.address,
                             node->helloPacketsExpected,
                             node->helloPacketsReceived);
                }
            }
        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();
}

uint8_t RoutingTableService::getTotalETX(uint16_t address) {
    RouteNode* node = findNode(address);

    if (node == nullptr) {
        return ETX_MAX_VALUE; // No route available
    }

    // For direct neighbors, calculate ETX from tracked data
    if (node->networkNode.hop_count == 1) {
        uint8_t reverseETX = calculateReverseETX(address);
        uint8_t forwardETX = calculateForwardETX(address);
        uint16_t total = reverseETX + forwardETX;

        // Ensure total doesn't overflow uint8_t
        if (total > ETX_MAX_VALUE) {
            return ETX_MAX_VALUE;
        }

        return (uint8_t)total;
    }

    // For multi-hop routes, use the advertised values in networkNode
    uint16_t total = node->networkNode.reverseETX + node->networkNode.forwardETX;
    if (total > ETX_MAX_VALUE) {
        return ETX_MAX_VALUE;
    }

    return (uint8_t)total;
}

LM_LinkedList<RouteNode>* RoutingTableService::routingTableList = new LM_LinkedList<RouteNode>();