#include "loramesher.h"

LoraMesher::LoraMesher() {}

void LoraMesher::init(void (*func)(void*)) {
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    routeTimeout = 10000000;
    initializeLocalAddress();
    initializeLoRa();
    initializeScheduler(func);

    delay(1000);
    Log.verboseln(F("Initialization DONE, starting receiving packets..."));
    int res = radio->startReceive();
    if (res != 0)
        Log.errorln(F("Receiving on constructor gave error: %d"), res);
}

LoraMesher::~LoraMesher() {
    vTaskDelete(ReceivePacket_TaskHandle);
    vTaskDelete(Hello_TaskHandle);
    vTaskDelete(ReceiveData_TaskHandle);
    vTaskDelete(SendData_TaskHandle);
    vTaskDelete(ReceivedUserData_TaskHandle);

    ReceivedPackets->Clear();
    ToSendPackets->Clear();
    ReceivedUserPackets->Clear();

    radio->clearDio0Action();
    radio->reset();
}

void LoraMesher::initializeLocalAddress() {
    uint8_t WiFiMAC[6];

    WiFi.macAddress(WiFiMAC);
    localAddress = (WiFiMAC[4] << 8) | WiFiMAC[5];

    Log.noticeln(F("Local LoRa address (from WiFi MAC): %X"), localAddress);
}

void LoraMesher::initializeLoRa() {
    Log.traceln(F("LoRa module initialization..."));

    // TODO: Optimize memory, this could lead to heap fragmentation
    Log.verboseln(F("Initializing RadioLib"));
    Module* mod = new Module(LORA_CS, LORA_IRQ, LORA_RST);
    radio = new SX1276(mod);
    if (radio == NULL) {
        Log.errorln(F("RadioLib not initialized properly"));
    }

    // Set up the radio parameters
    Log.verboseln(F("Initializing radio"));
    int res = radio->begin(BAND, BANDWIDTH, LORASF, 7U, 18U, 10);
    if (res != 0) {
        Log.errorln(F("Radio module gave error: %d"), res);
    }

#ifdef ADDCRC_PAYLOAD
    radio->setCRC(true);
#endif

    Log.verboseln(F("Setting up callback function"));
    radio->setDio0Action(onReceive);

    Log.traceln(F("LoRa module initialization DONE"));

    delay(1000);
}

void LoraMesher::initializeScheduler(void (*func)(void*)) {
    Log.verboseln(F("Setting up Schedulers"));
    int res = xTaskCreate(
        [](void* o) { static_cast<LoraMesher*>(o)->receivingRoutine(); },
        "Receiving routine",
        2048,
        this,
        6,
        &ReceivePacket_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Receiving routineeation gave error: %d"), res);
    }
    res = xTaskCreate(
        [](void* o) { static_cast<LoraMesher*>(o)->sendHelloPacket(); },
        "Hello routine",
        2048,
        this,
        5,
        &Hello_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Process Task creation gave error: %d"), res);
    }
    res = xTaskCreate(
        [](void* o) { static_cast<LoraMesher*>(o)->processPackets(); },
        "Process routine",
        4096,
        this,
        3,
        &ReceiveData_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Process Task creation gave error: %d"), res);
    }
    res = xTaskCreate(
        [](void* o) { static_cast<LoraMesher*>(o)->sendPackets(); },
        "Sending routine",
        2048,
        this,
        4,
        &SendData_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Sending Task creation gave error: %d"), res);
    }
    res = xTaskCreate(
        func,
        "Receive User routine",
        4096,
        this,
        2,
        &ReceivedUserData_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Receive User Task creation gave error: %d"), res);
    }
    // res = xTaskCreate(
    //   [](void* o) { static_cast<LoraMesher*>(o)->packetManager(); },
    //   "Packet Manager routine",
    //   4096,
    //   this,
    //   2,
    //   &PacketManager_TaskHandle);
    // if (res != pdPASS) {
    //   Log.errorln(F("Packet Manager Task creation gave error: %d"), res);
    // }
}

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void LoraMesher::onReceive() {

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    xHigherPriorityTaskWoken = xTaskNotifyFromISR(
        LoraMesher::getInstance().ReceivePacket_TaskHandle,
        0,
        eSetValueWithoutOverwrite,
        &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken == pdTRUE)
        portYIELD_FROM_ISR();
}

void LoraMesher::receivingRoutine() {
    BaseType_t TWres;
    bool receivedFlag = false;
    size_t packetSize;
    int rssi, snr, res;
    for (;;) {
        TWres = xTaskNotifyWait(
            pdFALSE,
            ULONG_MAX,
            NULL,
            portMAX_DELAY);
        receivedFlag = false;

        if (TWres == pdPASS) {
            packetSize = radio->getPacketLength();
            if (packetSize == 0)
                Log.warningln(F("Empty packet received"));

            else {
                packet<uint8_t>* rx = createEmptyPacket(packetSize);

                rssi = radio->getRSSI();
                snr = radio->getSNR();

                Log.noticeln(F("Receiving LoRa packet: Size: %d bytes RSSI: %d SNR: %d"), packetSize, rssi, snr);

                if (packetSize > MAXPACKETSIZE) {
                    Log.warningln(F("Received packet with size greater than MAX Packet Size"));
                    packetSize = MAXPACKETSIZE;
                }

                res = radio->readData((uint8_t*) rx, packetSize);

                if (res != 0) {
                    Log.errorln(F("Reading packet data gave error: %d"), res);
                    deletePacket(rx);
                } else if (snr <= 0) {
                    Log.errorln(F("Packet with bad SNR, deleting it"));
                    deletePacket(rx);
                } else {
                    //Set the received flag to true
                    receivedFlag = true;

                    //Add the packet created into the ReceivedPackets List
                    packetQueue<uint8_t>* pq = createPacketQueue(rx, 0);
                    ReceivedPackets->Add(pq);
                }

                Log.verboseln(F("Starting to listen again after receiving a packet"));
                res = radio->startReceive();
                if (res != 0) {
                    Log.errorln(F("Starting to listen in receiving routine gave error: %d"), res);
                }

                if (receivedFlag) {
                    //Notify that there is a packets to be processed
                    TWres = xTaskNotifyFromISR(
                        ReceiveData_TaskHandle,
                        0,
                        eSetValueWithoutOverwrite,
                        &TWres);
                }
            }
        }
    }
}

uint16_t LoraMesher::getLocalAddress() {
    return this->localAddress;
}

/**
 *  Region Packet Service
**/

bool LoraMesher::sendPacket(LoraMesher::packet<uint8_t>* p) {
    //Clear Dio0 Action
    radio->clearDio0Action();

    //Blocking transmit, it is necessary due to deleting the packet after sending it. 
    int resT = radio->transmit((uint8_t*) p, getPacketLength(p));

    radio->setDio0Action(onReceive);
    int res = radio->startReceive();

    if (resT != 0) {
        Log.errorln(F("Transmit gave error: %d"), resT);
        return false;
    }

    if (res != 0) {
        Log.errorln(F("Receiving on end of packet transmission gave error: %d"), res);
        return false;
    }

    printHeaderPacket(p, "send");
    return true;
}

void LoraMesher::sendPackets() {
    //Wait an initial 4 seconds
    vTaskDelay(4000 / portTICK_PERIOD_MS);
    int sendCounter = 0;
    uint8_t resendMessage = 0;

    for (;;) {
        /* Wait for the notification of receivingRoutine and enter blocking */
        Log.verboseln("Size of Send Packets Queue: %d", ToSendPackets->Size());

        packetQueue<packet<uint8_t>>* tx = ToSendPackets->Pop<packet<uint8_t>>();

        if (tx != nullptr) {
            Log.verboseln("Send nº %d", sendCounter);

            tx->packet->id = sendCounter;

            //If the packet has a data packet and its destination is not broadcast add the via to the packet and forward the packet
            if (hasDataPacket(tx->packet->type) && tx->packet->dst != BROADCAST_ADDR) {
                uint16_t nextHop = getNextHop(tx->packet->dst);

                if (nextHop != 0) {
                    ((LoraMesher::dataPacket<uint8_t>*) (tx->packet->payload))->via = nextHop;

                } else {
                    Log.errorln(F("NextHop Not found from %X, destination %X, via %X"), tx->packet->src, tx->packet->dst, nextHop);
                    deletePacketQueueAndPacket(tx);
                    continue;

                }
            }

            //Set a random delay, to avoid some collisions. Between 0 and 4 seconds
            // vTaskDelay(radio->random(300000) / portTICK_PERIOD_MS);

            //Send packet
            bool hasSend = sendPacket(tx->packet);

            //If the packet has not been send, add it to the queue and send it again
            if (!hasSend && resendMessage < MAX_RESEND_PACKET) {
                tx->priority = MAX_PRIORITY;
                ToSendPackets->Add((packetQueue<uint8_t>*) tx);

                resendMessage++;

                continue;
            }

            deletePacketQueueAndPacket(tx);

            sendCounter++;

            resendMessage = 0;
        }

        //TODO: This should be regulated about what time we can send a packet, in order to accomplish the regulations 
        //Wait for 10s to send the next packet
        // vTaskDelay((rand() % 10 + 5) * 1000 / portTICK_PERIOD_MS);
        vTaskDelay(SEND_PACKETS_DELAY * 1000 / portTICK_PERIOD_MS);
    }
}

void LoraMesher::sendHelloPacket() {
    //Wait an initial 2 second
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    for (;;) {

        packet<uint8_t>* tx = createRoutingPacket();
        setPackedForSend(tx, DEFAULT_PRIORITY + 1);

        //Wait for HELLO_PACKETS_DELAY seconds to send the next hello packet
        vTaskDelay(HELLO_PACKETS_DELAY * 1000 / portTICK_PERIOD_MS);
    }
}

void LoraMesher::processPackets() {
    for (;;) {
        /* Wait for the notification of receivingRoutine and enter blocking */
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);

        Log.verboseln("Size of Received Packets Queue: %d", ReceivedPackets->Size());

        while (ReceivedPackets->Size() > 0) {
            packetQueue<packet<uint8_t>>* rx = ReceivedPackets->Pop<packet<uint8_t>>();

            if (rx != nullptr) {
                printHeaderPacket(rx->packet, "received");

                uint8_t type = rx->packet->type;

                if ((type & HELLO_P) == HELLO_P) {
                    processRoute((packet<networkNode>*) rx->packet);
                    deletePacketQueueAndPacket(rx);

                } else if (hasDataPacket(type))
                    processDataPacket((packetQueue<packet<dataPacket<uint8_t>>>*) rx);

                else {
                    Log.verboseln(F("Packet not identified, deleting it"));
                    deletePacketQueueAndPacket(rx);

                }
            }
        }
    }
}

uint8_t LoraMesher::getMaximumPayloadLength(uint8_t type) {
    return MAXPACKETSIZE - getExtraLengthToPayload(type) - sizeof(packet<uint8_t>);
}

uint8_t LoraMesher::getExtraLengthToPayload(uint8_t type) {
    //Offset to the payload
    uint8_t extraSize = 0;

    //Check if type contains DATA_P or XL_DATA_P, add via size to extra size
    if (hasDataPacket(type))
        extraSize += sizeof(LoraMesher::dataPacket<uint8_t>);

    //Check if needs a control packet given a type. Add seq_id and number size to extra size
    if (hasControlPacket(type))
        extraSize += sizeof(LoraMesher::controlPacket<uint8_t>);

    return extraSize;
}

bool LoraMesher::hasDataPacket(uint8_t type) {
    return (HELLO_P & type) != HELLO_P;
}

bool LoraMesher::hasControlPacket(uint8_t type) {
    return !((HELLO_P & type) == HELLO_P || (DATA_P & type) == DATA_P);
}

void LoraMesher::printHeaderPacket(packet<uint8_t>* p, String title) {
    Log.setShowLevel(false);
    if (hasDataPacket(p->type)) {
        dataPacket<uint8_t>* dPacket = (dataPacket<uint8_t>*) (p->payload);
        if (hasControlPacket(p->type)) {
            controlPacket<uint8_t>* cPacket = (controlPacket<uint8_t>*) (dPacket->payload);
            Log.verboseln(F("Packet %s -- Size: %d Src: %X Dst: %X Id: %d Type: %b Via: %X Seq_Id: %d Num: %d"), title, getPacketLength(p), p->src, p->dst, p->type, p->id, dPacket->via, cPacket->seq_id, cPacket->number);
        } else {
            Log.verboseln(F("Packet %s -- Size: %d Src: %X Dst: %X Id: %d Type: %b Via: %X"), title, getPacketLength(p), p->src, p->dst, p->id, p->type, dPacket->via);
        }
    } else
        Log.verboseln(F("Packet %s -- Size: %d Src: %X Dst: %X Id: %d Type: %b "), title, getPacketLength(p), p->src, p->dst, p->id, p->type);

    Log.setShowLevel(true);
}

void LoraMesher::sendReliablePacket(uint16_t dst, uint8_t* payload, uint32_t payloadSize) {
    //TODO: Need some refactor
   //TODO: Cannot send an empty packet?? Maybe it could be like a ping?
    if (payloadSize == 0)
        return;
    //TODO: Could it send reliable messages to broadcast?
    if (dst == BROADCAST_ADDR)
        return;

    //Generate a sequence Id for this list of packets
    uint8_t seq_id = getSequenceId();

    //Get the Type of the packet
    uint8_t type = NEED_ACK_P | XL_DATA_P;

    //Max payload size per packet
    size_t maxPayloadSize = getMaximumPayloadLength(type);

    //Number of packets
    uint16_t numOfPackets = payloadSize / maxPayloadSize + (payloadSize % maxPayloadSize > 0);

    //Create a new Linked list to store the packetQueues and the payload
    LM_LinkedList<packetQueue<uint8_t>>* packetList = new LM_LinkedList<packetQueue<uint8_t>>();

    //Add the SYNC configuration packet
    packetList->Append(getStartSequencePacketQueue(dst, seq_id, numOfPackets));

    for (uint16_t i = 1; i <= numOfPackets; i++) {
        //Get the position of the payload
        uint8_t* payloadToSend = (uint8_t*) ((unsigned long) payload + ((i - 1) * maxPayloadSize));

        //Get the payload Size in bytes
        size_t payloadSizeToSend = maxPayloadSize;
        if (i == numOfPackets)
            payloadSizeToSend = payloadSize - (maxPayloadSize * (numOfPackets - 1));

        //Create a new packet with the previous payload
        packet<uint8_t>* p = createPacket(dst, getLocalAddress(), type, payloadToSend, payloadSizeToSend);

        //Add control Packet information
        packet<dataPacket<uint8_t>>* dPacket = (packet<dataPacket<uint8_t>>*) p;
        controlPacket<uint8_t>* cPacket = (controlPacket<uint8_t>*) (dPacket->payload->payload);
        cPacket->number = i;
        cPacket->seq_id = seq_id;

        //Create a packet queue
        packetQueue<uint8_t>* pq = createPacketQueue(p, DEFAULT_PRIORITY, i);

        //Append the packet queue in the linked list
        packetList->Append(pq);
    }

    //Create the pair of configuration
    listConfiguration* listConfig = new listConfiguration();
    listConfig->config = new sequencePacketConfig(seq_id, dst, numOfPackets);
    listConfig->list = packetList;

    //Add dataList pair to the waiting send packets queue
    q_WSP->setInUse();
    q_WSP->Append(listConfig);
    q_WSP->releaseInUse();

    //Send the first packet of the sequence (SYNC packet)
    sendPacketSequence(dst, seq_id, 0);
}


void LoraMesher::processDataPacket(LoraMesher::packetQueue<packet<dataPacket<uint8_t>>>* pq) {
    packet<dataPacket<uint8_t>>* packet = pq->packet;

    Log.traceln(F("Data packet from %X, destination %X, via %X"), packet->src, packet->dst, packet->payload->via);

    if (packet->dst == localAddress) {
        Log.verboseln(F("Data packet from %X for me"), packet->src);
        processDataPacketForMe(pq);
        return;

    } else if (packet->dst == BROADCAST_ADDR) {
        Log.verboseln(F("Data packet from %X BROADCAST"), packet->src);
        processDataPacketForMe(pq);
        return;

    } else if (packet->payload->via == localAddress) {
        Log.verboseln(F("Data Packet from %X for %X. Via is me"), packet->src, packet->dst);

        if (hasAddresRoutingTable(packet->dst)) {
            Log.verboseln(F("Data Packet forwarding it."));
            ToSendPackets->Add((packetQueue<uint8_t>*) pq);
            return;
        }

        Log.verboseln(F("Data Packet destination not reachable, deleting it."));
        deletePacketQueueAndPacket(pq);

        return;
    }

    Log.verboseln(F("Packet not for me, deleting it"));
    deletePacketQueueAndPacket(pq);
}

void LoraMesher::processDataPacketForMe(packetQueue<packet<dataPacket<uint8_t>>>* pq) {
    packet<dataPacket<uint8_t>>* p = pq->packet;
    controlPacket<uint8_t>* cPacket = (controlPacket<uint8_t>*) (p->payload->payload);

    //By default, delete the packet queue at the finish of this function
    bool deleteQueuepacket = true;

    if ((p->type & DATA_P) == DATA_P) {
        Log.verboseln(F("Data Packet received"));
        //Convert the packet queue into a packet queue user
        packetQueue<userPacket<uint8_t>>* pqUser = (packetQueue<userPacket<uint8_t>>*) pq;

        //Convert the packet into a user packet
        pqUser->packet = convertPacket((packet<uint8_t>*) p);

        //Add and notify the user of this packet
        notifyUserReceivedPacket(pqUser);

        deleteQueuepacket = false;

    } else if ((p->type & ACK_P) == ACK_P) {
        Log.verboseln(F("ACK Packet received"));
        addAck(p->src, cPacket->seq_id, cPacket->number);

    } else if ((p->type & LOST_P) == LOST_P) {
        Log.verboseln(F("Lost Packet received"));
        //Send the packet sequence that has been lost
        if (sendPacketSequence(p->src, cPacket->seq_id, cPacket->number)) {
            //Reset the timeout of this sequence packets inside the q_WSP
            resetTimeout(q_WSP, p->src, cPacket->seq_id);
        }

    } else if ((p->type & SYNC_P) == SYNC_P) {
        Log.verboseln(F("Synchronization Packet received"));
        processSyncPacket(p->src, cPacket->seq_id, cPacket->number);

        //Change the number to send the ack to the correct one
        cPacket->number = 0;

    } else if ((p->type & XL_DATA_P) == XL_DATA_P) {
        Log.verboseln(F("Large payload Packet received"));
        processLargePayloadPacket(pq);

        deleteQueuepacket = false;

    }

    //Need ack
    if ((p->type & NEED_ACK_P) == NEED_ACK_P) {
        Log.verboseln(F("Previous packet need an ACK"));
        sendAckPacket(p->src, cPacket->seq_id, cPacket->number);
    }

    if (deleteQueuepacket)
        //Delete packet queue
        deletePacketQueueAndPacket(pq);
}

void LoraMesher::notifyUserReceivedPacket(LoraMesher::packetQueue<userPacket<uint8_t>>* pq) {
    //Add the packet inside the receivedUsers Queue
    ReceivedUserPackets->Add((packetQueue<uint8_t>*) pq);

    //Notify the received user task handle
    xTaskNotifyFromISR(
        ReceivedUserData_TaskHandle,
        0,
        eSetValueWithoutOverwrite,
        NULL);
}

/**
 *  End Region Packet Service
**/


/**
 *  Region Routing Table
**/

int LoraMesher::routingTableSize() {
    int i = 0;
    while (i < RTMAXSIZE && routingTable[i].networkNode.address != 0)
        i++;

    return i;
}

bool LoraMesher::hasAddresRoutingTable(uint16_t address) {
    int i = 0;
    while (i < RTMAXSIZE && routingTable[i].networkNode.address != 0) {
        if (routingTable[i].networkNode.address == address)
            return true;
        i++;
    }

    return false;
}

uint16_t LoraMesher::getNextHop(uint16_t dst) {
    int i = 0;
    while (i < RTMAXSIZE && routingTable[i].networkNode.address != 0) {
        if (routingTable[i].networkNode.address == dst)
            return routingTable[i].via;
        i++;
    }

    return 0;
}

void LoraMesher::processRoute(LoraMesher::packet<networkNode>* p) {
    Log.verboseln(F("HELLO packet from %X with size %d"), p->src, p->payloadSize);
    printPacket(p, true);

    LoraMesher::networkNode* receivedNode = new networkNode();
    receivedNode->address = p->src;
    receivedNode->metric = 1;
    processRoute(localAddress, receivedNode);
    delete receivedNode;

    for (size_t i = 0; i < getPayloadLength(p); i++) {
        LoraMesher::networkNode* node = &p->payload[i];
        node->metric++;
        processRoute(p->src, node);
    }

    printRoutingTable();
}

void LoraMesher::processRoute(uint16_t via, LoraMesher::networkNode* node) {
    if (node->address != localAddress) {
        bool knownAddr = false;
        for (int i = 0; i < routingTableSize(); i++) {
            if (routingTable[i].networkNode.address != 0 && node->address == routingTable[i].networkNode.address) {
                knownAddr = true;
                if (node->metric < routingTable[i].networkNode.metric) {
                    routingTable[i].networkNode.metric = node->metric;
                    routingTable[i].via = via;
                    Log.verboseln(F("Found better route for %X via %X metric %d"), node->address, via, node->metric);
                }
                break;
            }
        }
        if (!knownAddr)
            addNodeToRoutingTable(node, via);
    }
}

void LoraMesher::addNodeToRoutingTable(LoraMesher::networkNode* node, uint16_t via) {
    for (int i = 0; i < RTMAXSIZE; i++) {
        if (routingTable[i].networkNode.address == 0) {
            routingTable[i].networkNode.address = node->address;
            routingTable[i].networkNode.metric = node->metric;
            routingTable[i].timeout = micros() + routeTimeout;
            routingTable[i].via = via;
            Log.verboseln(F("New route added: %X via %X metric %d"), node->address, via, node->metric);
            break;
        }
    }
}

void LoraMesher::printRoutingTable() {
    Log.verboseln(F("Current routing table:"));
    for (int i = 0; i < routingTableSize(); i++)
        Log.verboseln(F("%d - %X via %X metric %d"), i,
            routingTable[i].networkNode.address,
            routingTable[i].via,
            routingTable[i].networkNode.metric);
}

/**
 *  End Region Routing Table
**/


/**
 *  Region Packet
**/

LoraMesher::packet<uint8_t>* LoraMesher::createPacket(uint8_t* payload, uint8_t payloadSize, uint8_t extraSize) {
    int packetLength = sizeof(packet<uint8_t>) + payloadSize + extraSize;

    if (packetLength > MAXPACKETSIZE) {
        Log.warningln(F("Trying to create a packet greater than MAXPACKETSIZE"));
        // return nullptr;
    }

    //Packet length = size of the packet + size of the payload + extraSize before payload

    packet<uint8_t>* p = (packet<uint8_t>*) malloc(packetLength);

    if (p) {
        //Copy the payload into the packet
        //If has extraSize, we need to add the extraSize to point correctly to the p->payload
        memcpy((void*) ((unsigned long) p->payload + (extraSize)), payload, payloadSize);
    } else {
        Log.errorln(F("packet not allocated"));
        return nullptr;
    }

    p->payloadSize = payloadSize + extraSize;

    Log.traceln(F("Packet created with %d bytes"), packetLength);

    return p;
};

LoraMesher::packet<uint8_t>* LoraMesher::createEmptyPacket(size_t packetSize) {
    if (packetSize > MAXPACKETSIZE) {
        Log.warningln(F("Trying to create a packet greater than MAXPACKETSIZE"));
        packetSize = MAXPACKETSIZE;
    }

    packet<uint8_t>* p = (packet<uint8_t>*) malloc(packetSize);

    return p;

}

LoraMesher::packet<uint8_t>* LoraMesher::copyPacket(packet<uint8_t>* p) {
    int packetLength = getPacketLength(p);

    packet<uint8_t>* cpPacket = (packet<uint8_t>*) malloc(packetLength);
    if (p) {
        memcpy(cpPacket, p, packetLength);
    }

    return cpPacket;
}

LoraMesher::userPacket<uint8_t>* LoraMesher::convertPacket(packet<uint8_t>* p) {
    auto uPacket = createUserPacket(p->dst, p->src, getPayload(p), getPayloadLength(p));
    deletePacket(p);

    return uPacket;

}

LoraMesher::userPacket<uint8_t>* LoraMesher::createUserPacket(uint16_t dst, uint16_t src, uint8_t* payload, uint32_t payloadSize) {
    int packetLength = sizeof(userPacket<uint8_t>) + payloadSize;

    userPacket<uint8_t>* p = (userPacket<uint8_t>*) malloc(packetLength);

    if (p) {
        //Copy the payload into the packet
        memcpy((void*) ((unsigned long) p->payload), payload, payloadSize);
    } else {
        Log.errorln(F("User Packet not allocated"));
        return nullptr;
    }

    p->dst = dst;
    p->src = src;
    p->payloadSize = payloadSize;

    return p;
}

LoraMesher::packet<uint8_t>* LoraMesher::createRoutingPacket() {
    int routingSize = routingTableSize();
    size_t routingSizeInBytes = routingSize * sizeof(LoraMesher::networkNode);

    networkNode* payload = new networkNode[routingSize];

    for (int i = 0; i < routingSize; i++)
        payload[i] = routingTable[i].networkNode;


    packet<uint8_t>* networkPacket = createPacket(BROADCAST_ADDR, localAddress, HELLO_P, (uint8_t*) payload, routingSizeInBytes);
    delete[]payload;
    return networkPacket;
}


/**
 *  End Region Packet
**/


/**
 *  Region PacketQueue
**/

size_t LoraMesher::getReceivedQueueSize() {
    return ReceivedUserPackets->Size();
}

void LoraMesher::PacketQueue::Enable() {
    *enabled = true;
}

void LoraMesher::PacketQueue::WaitAndDisable() {
    while (!*enabled) {
        Log.traceln(F("Waiting for free queue"));
        vTaskDelay(100);
    }
    *enabled = false;
}


void LoraMesher::PacketQueue::Add(packetQueue<uint8_t>* pq) {
    WaitAndDisable();

    if (first == nullptr)
        first = pq;

    else {
        packetQueue<uint8_t>* firstCp = first;
        if (firstCp->next == nullptr && first->priority < pq->priority) {
            first = pq;
            pq->next = firstCp;
            Enable();
            return;
        }

        while (firstCp->next != nullptr && firstCp->priority >= pq->priority)
            firstCp = firstCp->next;

        if (firstCp->next == nullptr)
            firstCp->next = pq;

        else {
            packetQueue<uint8_t>* cp = firstCp->next;
            firstCp->next = pq;
            pq->next = cp;
        }
    }

    Enable();
}

size_t LoraMesher::PacketQueue::Size() {
    WaitAndDisable();
    packetQueue<uint8_t>* firstCp = (packetQueue<uint8_t>*) first;
    if (firstCp == nullptr) {
        Enable();
        return 0;
    }

    size_t size = 1;
    while (firstCp->next != nullptr) {
        size++;
        firstCp = firstCp->next;
    }

    Enable();
    return size;
}

void LoraMesher::PacketQueue::Clear() {
    WaitAndDisable();

    if (first == nullptr) {
        Enable();
        return;
    }

    packetQueue<uint8_t>* firstCp = (packetQueue<uint8_t>*) first;
    while (first != nullptr) {
        firstCp = first->next;
        deletePacketQueueAndPacket(first);
        first = firstCp;
    }

    Enable();
}
/**
 *  End Region PacketQueue
**/


/**
 * Large and Reliable payloads
 */

LoraMesher::packetQueue<uint8_t>* LoraMesher::getStartSequencePacketQueue(uint16_t destination, uint8_t seq_id, uint16_t num_packets) {
    uint8_t type = SYNC_P | NEED_ACK_P | XL_DATA_P;

    //Create the packet
    packet<uint8_t>* p = createPacket(destination, getLocalAddress(), type, (uint8_t*) 0, 0);

    dataPacket<uint8_t>* dPacket = (dataPacket<uint8_t>*) (p->payload);
    controlPacket<uint8_t>* cPacket = (controlPacket<uint8_t>*) (dPacket->payload);

    //Add the sequence id and the number of packets of the sequence
    cPacket->seq_id = seq_id;
    cPacket->number = num_packets;

    //Create a packet queue
    return createPacketQueue(p, DEFAULT_PRIORITY, 0);
}

void LoraMesher::sendAckPacket(uint16_t destination, uint8_t seq_id, uint16_t seq_num) {
    uint8_t type = ACK_P;

    //Create the packet
    packet<uint8_t>* p = createPacket(destination, getLocalAddress(), type, (uint8_t*) 0, 0);

    dataPacket<uint8_t>* dPacket = (dataPacket<uint8_t>*) (p->payload);
    controlPacket<uint8_t>* cPacket = (controlPacket<uint8_t>*) (dPacket->payload);

    //Add the sequence id and the number of packets of the sequence
    cPacket->seq_id = seq_id;
    cPacket->number = seq_num;

    setPackedForSend(p, DEFAULT_PRIORITY + 1);
}

void LoraMesher::sendLostPacket(uint16_t destination, uint8_t seq_id, uint16_t seq_num) {
    uint8_t type = LOST_P;

    //Create the packet
    packet<uint8_t>* p = createPacket(destination, getLocalAddress(), type, (uint8_t*) 0, 0);

    dataPacket<uint8_t>* dPacket = (dataPacket<uint8_t>*) (p->payload);
    controlPacket<uint8_t>* cPacket = (controlPacket<uint8_t>*) (dPacket->payload);

    //Add the sequence id and the number of packets of the sequence
    cPacket->seq_id = seq_id;
    cPacket->number = seq_num;

    setPackedForSend(p, DEFAULT_PRIORITY);
}

bool LoraMesher::sendPacketSequence(uint16_t destination, uint8_t seq_id, uint16_t seq_num) {
    Log.verboseln(F("Trying to send the sequence packet. Dst: %X Seq_Id: %d Seq_Num: %d"), destination, seq_id, seq_num);
    //Get the listConfiguration with the sequence id
    listConfiguration* config = findSequenceList(q_WSP, seq_id, destination);

    if (config == nullptr) {
        Log.errorln(F("NOT FOUND the sequence packet config with Id: %d"), seq_id);
        return false;
    }

    //Get the packet queue with the sequence number
    packetQueue<uint8_t>* pq = findPacketQueue(config->list, seq_num);

    if (pq == nullptr) {
        Log.errorln(F("NOT FOUND the packet queue with Seq_id: %d, Num: %d"), seq_id, seq_num);
        return false;
    }

    //Create the packet
    packet<uint8_t>* p = copyPacket((packet<uint8_t>*) pq->packet);

    //Add the packet to the send queue
    setPackedForSend(p, DEFAULT_PRIORITY);

    return true;
}

void LoraMesher::addAck(uint16_t source, uint8_t seq_id, uint16_t seq_num) {
    listConfiguration* config = findSequenceList(q_WSP, seq_id, source);
    if (config == nullptr) {
        Log.errorln(F("NOT FOUND the sequence packet config in add ack with Id: %d"), seq_id);
        return;
    }

    //If all packets has been arrived to the destiny
    //Delete this sequence
    if (config->config->number == seq_num) {
        Log.traceln(F("All the packets has been arrived to the seq_Id: %d"), seq_id);
        findAndClearLinkedList(q_WSP, config);
        return;
    }

    //Set has been received some ACK
    config->config->firstAckReceived = 1;

    //Add the last ack to the config packet
    config->config->lastAck = seq_num;

    //Reset the timeouts
    resetTimeout(config->config);

    //Send the next packet sequence
    if (sendPacketSequence(source, seq_id, seq_num + 1))
        Log.verboseln(F("Sending next packet after receiving an ACK"));
    else
        Log.errorln(F("Error at sending next packet after receiving an ACK"));

}

void LoraMesher::processLargePayloadPacket(packetQueue<packet<dataPacket<uint8_t>>>* pq) {
    packet<dataPacket<uint8_t>>* packet = pq->packet;
    controlPacket<uint8_t>* cPacket = (controlPacket<uint8_t>*) (packet->payload->payload);

    listConfiguration* configList = findSequenceList(q_WRP, cPacket->seq_id, packet->src);
    if (configList == nullptr) {
        Log.errorln(F("NOT FOUND the sequence packet config in Process Large Payload ack with Id: %d"), cPacket->seq_id);
        deletePacketQueueAndPacket(pq);
        return;
    }

    if (configList->config->lastAck + 1 != cPacket->number) {
        Log.errorln(F("Sequence number received in bad order in seq_Id: %d, received: %d expected: %d"), cPacket->seq_id, cPacket->number, configList->config->lastAck + 1);
        sendLostPacket(packet->src, cPacket->seq_id, configList->config->lastAck + 1);

        deletePacketQueueAndPacket(pq);
        return;
    }

    configList->config->lastAck++;

    configList->list->setInUse();
    configList->list->Append((packetQueue<uint8_t>*) pq);
    configList->list->releaseInUse();

    //All packets has been arrived, join them and send to the user
    if (configList->config->lastAck == configList->config->number) {
        joinPacketsAndNotifyUser(configList);
        return;
    }

    resetTimeout(configList->config);
}

void LoraMesher::joinPacketsAndNotifyUser(listConfiguration* listConfig) {
    Log.verboseln(F("Joining packets seq_Id: %d Src: %X"), listConfig->config->seq_id, listConfig->config->source);

    auto list = listConfig->list;

    list->setInUse();
    if (!list->moveToStart())
        return;

    size_t payloadSize = 0;
    size_t number = 1;

    do {
        auto currentP = (packet<dataPacket<controlPacket<uint8_t>>>*) list->getCurrent()->packet;

        if (number != (currentP->payload->payload->number))
            //TODO: ORDER THE PACKETS if they are not ordered?
            Log.errorln(F("Wrong packet order"));

        number++;
        payloadSize += getPacketPayloadLength(currentP);
    } while (list->next());

    //Move to start again
    list->moveToStart();

    auto currentP = (packet<dataPacket<controlPacket<uint8_t>>>*) list->getCurrent()->packet;

    uint32_t userPacketLength = sizeof(userPacket<uint8_t>);

    //Packet length = size of the packet + size of the payload
    uint32_t packetLength = userPacketLength + payloadSize;

    userPacket<uint8_t>* p = (userPacket<uint8_t>*) malloc(packetLength);

    Log.verboseln(F("Large Packet Packet length: %d Payload Size: %d"), packetLength, payloadSize);

    if (p) {
        //Copy the payload into the packet
        unsigned long actualPayloadSizeDst = userPacketLength;

        do {
            Log.verboseln(F("Actual size dst: %d"), actualPayloadSizeDst);

            currentP = (packet<dataPacket<controlPacket<uint8_t>>>*) list->getCurrent()->packet;

            size_t actualPayloadSizeSrc = getPacketPayloadLength(currentP);
            Log.verboseln(F("Actual size src: %d"), actualPayloadSizeSrc);

            memcpy((void*) ((unsigned long) p + (actualPayloadSizeDst)), currentP->payload->payload->payload, actualPayloadSizeSrc);
            actualPayloadSizeDst += actualPayloadSizeSrc;
        } while (list->next());
    }

    //Change payload size to all payload size + size of the headers
    p->payloadSize = payloadSize;

    list->releaseInUse();

    findAndClearLinkedList(q_WRP, listConfig);

    packetQueue<userPacket<uint8_t>>* pq = (packetQueue<userPacket<uint8_t>>*) createPacketQueue(p, DEFAULT_PRIORITY);

    notifyUserReceivedPacket(pq);
}

void LoraMesher::processSyncPacket(uint16_t source, uint8_t seq_id, uint16_t seq_num) {
    //Create the pair of configuration
    listConfiguration* listConfig = new listConfiguration();
    listConfig->config = new sequencePacketConfig(seq_id, source, seq_num);
    listConfig->list = new LM_LinkedList<packetQueue<uint8_t>>();

    resetTimeout(listConfig->config);

    //Add list configuration to the waiting received packets queue
    q_WRP->setInUse();
    q_WRP->Append(listConfig);
    q_WRP->releaseInUse();

}

void LoraMesher::resetTimeout(LM_LinkedList<listConfiguration>* queue, uint8_t seq_id, uint16_t source) {
    listConfiguration* config = findSequenceList(q_WSP, seq_id, source);
    if (config == nullptr) {
        Log.errorln(F("NOT FOUND the sequence packet config in reset timeout with Id: %d"), seq_id);
        return;
    }

    resetTimeout(config->config);
}

void LoraMesher::resetTimeout(sequencePacketConfig* configPacket) {
    configPacket->numberOfTimeouts = 0;
    addTimeoutTime(configPacket);
}

void LoraMesher::clearLinkedList(listConfiguration* listConfig) {
    auto list = listConfig->list;
    list->setInUse();
    Log.traceln(F("Clearing list configuration Seq_Id: %d Src: %X"), listConfig->config->seq_id, listConfig->config->source);

    for (int i = 0; i < list->getLength(); i++) {
        deletePacketQueueAndPacket(list->getCurrent());
        list->DeleteCurrent();

    }

    delete listConfig->list;
    delete listConfig->config;
    delete listConfig;
}

void LoraMesher::findAndClearLinkedList(LM_LinkedList<listConfiguration>* queue, listConfiguration* listConfig) {
    queue->setInUse();

    auto current = queue->getCurrent();
    //Find list config inside the queue, if not find error
    if (current != listConfig && !queue->Search(listConfig)) {
        Log.errorln(F("Not found list config"));
        //TODO: What to do inside all errors????????????????
        return;
    }

    clearLinkedList(listConfig);
    queue->DeleteCurrent();

    queue->releaseInUse();
}

LoraMesher::listConfiguration* LoraMesher::findSequenceList(LM_LinkedList<listConfiguration>* queue, uint8_t seq_id, uint16_t source) {
    queue->setInUse();
    queue->moveToStart();

    for (int i = 0; i < queue->getLength(); i++) {
        auto current = queue->getCurrent();

        if (current->config->seq_id == seq_id && current->config->source == source) {
            queue->releaseInUse();
            return current;
        }

        if (!queue->next())
            break;
    }

    queue->releaseInUse();

    return nullptr;

}

LoraMesher::packetQueue<uint8_t>* LoraMesher::findPacketQueue(LM_LinkedList<packetQueue<uint8_t>>* queue, uint8_t num) {
    queue->setInUse();
    queue->moveToStart();

    for (int i = 0; i < queue->getLength(); i++) {
        auto current = queue->getCurrent();

        if (current->number == num) {
            queue->releaseInUse();
            return current;
        }

        if (!queue->next())
            break;
    }

    queue->releaseInUse();

    return nullptr;

}

void LoraMesher::packetManager() {
    for (;;) {
        managerReceivedQueue();
        managerSendQueue();

        vTaskDelay(DEFAULT_TIMEOUT / portTICK_PERIOD_MS);
    }

}

void LoraMesher::managerReceivedQueue() {
    q_WRP->setInUse();
    q_WRP->moveToStart();

    for (int i = 0; i < q_WRP->getLength(); i++) {
        auto current = q_WRP->getCurrent();
        //Get Config packet
        auto configPacket = current->config;

        //If Config packet have reached timeout
        if (configPacket->timeout <= micros()) {
            //Increment number of timeouts
            configPacket->numberOfTimeouts++;

            //If number of timeouts is greater than Max timeouts, erase it
            if (configPacket->numberOfTimeouts > MAX_TIMEOUTS) {
                Log.errorln(F("MAX TIMEOUTS reached from Waiting Received Queue, erasing Id: %d"), configPacket->seq_id);
                clearLinkedList(current);
                q_WRP->DeleteCurrent();
                continue;
            }

            addTimeoutTime(configPacket);


            //Repeat the configPacket ACK
            sendAckPacket(configPacket->source, configPacket->seq_id, configPacket->lastAck);
        }

        //TODO: Check for nullptrs?
        if (!q_WRP->next())
            break;
    }
    q_WRP->releaseInUse();
}

void LoraMesher::managerSendQueue() {
    //TODO: Same as before but with the q_WSP
}

void LoraMesher::addTimeoutTime(sequencePacketConfig* configPacket) {
    //TODO: This timeout should account for the number of send packets waiting to send + how many time between send packets?
    //TODO: This timeout should be a little variable depending on the duty cycle. 
    configPacket->timeout = micros() + (DEFAULT_TIMEOUT * 1000) / portTICK_PERIOD_MS;
}

uint8_t LoraMesher::getSequenceId() {
    if (sequence_id == 255) {
        sequence_id = 0;
        return 255;
    }

    uint8_t id = sequence_id;
    sequence_id++;

    return id;
}


// void LoraMesher::sendStartSequencePackets(uint16_t destination, uint8_t seq_id, uint16_t num_packets) {
//   uint8_t type = SYNC_P | NEED_ACK_P | XL_DATA_P;

//   //Create the packet
//   packet<uint8_t>* p = createPacket(destination, getLocalAddress(), type, (uint8_t*) 0, 0);

//   dataPacket<uint8_t>* dPacket = (dataPacket<uint8_t>*) (&p->payload);
//   controlPacket<uint8_t>* cPacket = (controlPacket<uint8_t>*) (&dPacket->payload);

//   //Add the sequence id and the number of packets of the sequence
//   cPacket->seq_id = seq_id;
//   cPacket->number = num_packets;

//   //Add the packet to the send queue
//   setPackedForSend(p, DEFAULT_PRIORITY);
// }

/**
 * End Large and Reliable payloads
 */
