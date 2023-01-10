#include "LoraMesher.h"

LoraMesher::LoraMesher() {}

void LoraMesher::begin(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord, int8_t power, uint16_t preambleLength) {
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    WiFiService::init();
    initializeLoRa(freq, bw, sf, cr, syncWord, power, preambleLength);
    initializeSchedulers();
    recalculateMaxTimeOnAir();
}

void LoraMesher::standby() {
    //Get actual priority
    UBaseType_t prevPriority = uxTaskPriorityGet(NULL);

    //Set max priority
    vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);

    int res = radio->standby();
    if (res != 0)
        Log.errorln(F("Standby gave error: %d"), res);

    //Disable Interrupts
    enableInterrupt = false;

    //Clear Dio Actions
    clearDioActions();

    //Suspend all tasks
    vTaskSuspend(ReceivePacket_TaskHandle);
    vTaskSuspend(Hello_TaskHandle);
    vTaskSuspend(ReceiveData_TaskHandle);
    vTaskSuspend(SendData_TaskHandle);
    vTaskSuspend(PacketManager_TaskHandle);
    vTaskSuspend(ReceiveTimeout_TaskHandle);

    //Set previous priority
    vTaskPrioritySet(NULL, prevPriority);
}

void LoraMesher::start() {
    //Get actual priority
    UBaseType_t prevPriority = uxTaskPriorityGet(NULL);

    //Set max priority
    vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);

    //Enable Interrupt
    enableInterrupt = true;

    //Start Receiving
    startReceiving();

    //Resume all tasks
    vTaskResume(ReceivePacket_TaskHandle);
    vTaskResume(Hello_TaskHandle);
    vTaskResume(ReceiveData_TaskHandle);
    vTaskResume(SendData_TaskHandle);
    vTaskResume(PacketManager_TaskHandle);
    vTaskResume(ReceiveTimeout_TaskHandle);

    //Set previous priority
    vTaskPrioritySet(NULL, prevPriority);
}

LoraMesher::~LoraMesher() {
    vTaskDelete(ReceivePacket_TaskHandle);
    vTaskDelete(Hello_TaskHandle);
    vTaskDelete(ReceiveData_TaskHandle);
    vTaskDelete(SendData_TaskHandle);
    vTaskDelete(PacketManager_TaskHandle);
    vTaskDelete(ReceiveTimeout_TaskHandle);

    ToSendPackets->Clear();
    ReceivedPackets->Clear();
    ReceivedAppPackets->Clear();

    clearDioActions();
    radio->reset();

    delete radio;
    delete genericModule;
}

void LoraMesher::initializeLoRa(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord, int8_t power, uint16_t preambleLength) {
    Log.verboseln(F("Initializing RadioLib"));
    genericModule = new Module(LORA_CS, LORA_IRQ, LORA_RST);
    radio = new SX1276(genericModule);
    if (radio == NULL) {
        Log.errorln(F("RadioLib not initialized properly"));
    }

    // Set up the radio parameters
    Log.verboseln(F("Initializing radio"));
    int res = radio->begin(freq, bw, sf, cr, syncWord, power, preambleLength);
    if (res != 0) {
        Log.errorln(F("Radio module gave error: %d"), res);
    }

#ifdef LM_ADDCRC_PAYLOAD
    radio->setCRC(true);
#endif
    Log.traceln(F("LoRa module initialization DONE"));
}

void LoraMesher::setDioActionsForScanChannel() {
    // set the function that will be called
    // when LoRa preamble is not detected within CAD timeout period
    // or when a packet is received
    radio->setDio0Action(onReceivingTimeout);

    // set the function that will be called
    // when LoRa preamble is detected
    radio->setDio1Action(onReceive);
}

void LoraMesher::setDioActionsForReceivePacket() {
    radio->setDio0Action(onReceive);
    radio->setDio1Action(onReceivingTimeout);
}

void LoraMesher::clearDioActions() {
    Log.verboseln(F("Clearing callback function"));
    radio->clearDio0Action();
    radio->clearDio1Action();
}

//TODO: Retry start receiving if it fails
int LoraMesher::startReceiving() {
    setDioActionsForReceivePacket();

    int res = radio->startReceive();
    if (res != 0) {
        Log.errorln(F("Starting receiving gave error: %d"), res);
    }
    return res;
}

//TODO: Retry start channel scan if it fails
int LoraMesher::startChannelScan() {
    setDioActionsForScanChannel();

    int state = radio->startChannelScan();
    if (state != RADIOLIB_ERR_NONE)
        Log.errorln(F("Starting new scan failed, code %d"), state);

    return state;
}

void LoraMesher::initializeSchedulers() {
    Log.verboseln(F("Setting up Schedulers"));
    int res = xTaskCreate(
        [](void* o) { static_cast<LoraMesher*>(o)->receivingRoutine(); },
        "Receiving routine",
        2048,
        this,
        6,
        &ReceivePacket_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Receiving routine creation gave error: %d"), res);
    }
    res = xTaskCreate(
        [](void* o) { static_cast<LoraMesher*>(o)->receivingTimeoutRoutine(); },
        "Receiving timeout routine",
        2048,
        this,
        6,
        &ReceiveTimeout_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Receiving timeout routine creation gave error: %d"), res);
    }
    res = xTaskCreate(
        [](void* o) { static_cast<LoraMesher*>(o)->sendPackets(); },
        "Sending routine",
        2048,
        this,
        5,
        &SendData_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Sending Task creation gave error: %d"), res);
    }
    res = xTaskCreate(
        [](void* o) { static_cast<LoraMesher*>(o)->sendHelloPacket(); },
        "Hello routine",
        2048,
        this,
        4,
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
        [](void* o) { static_cast<LoraMesher*>(o)->packetManager(); },
        "Packet Manager routine",
        4096,
        this,
        2,
        &PacketManager_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Packet Manager Task creation gave error: %d"), res);
    }
}

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void LoraMesher::onReceive(void) {
    if (!LoraMesher::getInstance().enableInterrupt)
        return;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    xHigherPriorityTaskWoken = xTaskNotifyFromISR(
        LoraMesher::getInstance().ReceivePacket_TaskHandle,
        0,
        eSetValueWithoutOverwrite,
        &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken == pdTRUE)
        portYIELD_FROM_ISR();
}

// this function is called when no preamble
// is detected within timeout period
// IMPORTANT: this function MUST be 'void' type
//            and MUST NOT have any arguments!
#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void LoraMesher::onReceivingTimeout(void) {
    if (!LoraMesher::getInstance().enableInterrupt)
        return;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    xHigherPriorityTaskWoken = xTaskNotifyFromISR(
        LoraMesher::getInstance().ReceiveTimeout_TaskHandle,
        0,
        eSetValueWithoutOverwrite,
        &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken == pdTRUE)
        portYIELD_FROM_ISR();
}

void LoraMesher::receivingRoutine() {
    vTaskSuspend(NULL);

    BaseType_t TWres;
    size_t packetSize;
    int8_t rssi, snr;
    int16_t state;

    for (;;) {
        TWres = xTaskNotifyWait(
            pdTRUE,
            pdFALSE,
            NULL,
            portMAX_DELAY);

        if (TWres == pdPASS) {
            Log.verboseln(F("Preamble detected, starting reception... "));
            state = RADIOLIB_ERR_NONE;
            enableInterrupt = false;

            if (scanChannelFlag == 1) {
                scanChannelFlag == 2;

                state = radio->receive(0, RADIOLIB_SX127X_RXSINGLE);
            }

            if (state == RADIOLIB_ERR_NONE) {
                packetSize = radio->getPacketLength();
                if (packetSize == 0)
                    Log.warningln(F("Empty packet received"));
                else {
                    Packet<uint8_t>* rx = PacketService::createEmptyPacket(packetSize);

                    rssi = (int8_t) round(radio->getRSSI());
                    snr = (int8_t) round(radio->getSNR());

                    Log.noticeln(F("Receiving LoRa packet: Size: %d bytes RSSI: %d SNR: %d"), packetSize, rssi, snr);

                    if (packetSize > MAXPACKETSIZE) {
                        Log.warningln(F("Received packet with size greater than MAX Packet Size"));
                        packetSize = MAXPACKETSIZE;
                    }

                    state = radio->readData(reinterpret_cast<uint8_t*>(rx), packetSize);

                    if (state != 0) {
                        Log.errorln(F("Reading packet data gave error: %d"), state);
                        deletePacket(rx);
                    }
                    else {
                        //Create a Packet Queue element containing the Packet
                        QueuePacket<Packet<uint8_t>>* pq = PacketQueueService::createQueuePacket(rx, 0, 0, rssi, snr);

                        //Add the Packet Queue element created into the ReceivedPackets List
                        ReceivedPackets->Append(pq);

                        //Notify that a packet needs to be process
                        TWres = xTaskNotifyFromISR(
                            ReceiveData_TaskHandle,
                            0,
                            eSetValueWithoutOverwrite,
                            &TWres);
                    }
                }
            }

            enableInterrupt = true;

            startReceiving();
        }
    }
}

void LoraMesher::receivingTimeoutRoutine() {
    vTaskSuspend(NULL);

    BaseType_t TWres;

    for (;;) {
        TWres = xTaskNotifyWait(
            pdTRUE,
            pdFALSE,
            NULL,
            portMAX_DELAY);

        if (TWres == pdPASS) {
            if (scanChannelFlag == 0)
                continue;

            Log.verboseln(F("Scanning channel timeout"));
            //If timeout and was scanning the channel, set 
            if (scanChannelFlag == 1)
                scanChannelFlag = 3;
        }
    }
}

uint16_t LoraMesher::getLocalAddress() {
    return WiFiService::getLocalAddress();
}

/**
 *  Region Packet Service
**/

void LoraMesher::waitBeforeSend(uint8_t repeatedDetectPreambles) {
    //Random delay, to avoid some collisions. Between 0 and 100
    uint32_t randomDelay = random(1, 10 * repeatedDetectPreambles) * maxTimeOnAir;

    Log.verboseln(F("RandomDelay %d ms"), randomDelay);

    //Set a random delay, to avoid some collisions.
    vTaskDelay(randomDelay / portTICK_PERIOD_MS);

    scanChannelFlag = 1;

    Log.verboseln(F("Starting scanning channel"));
    startChannelScan();

    while (scanChannelFlag == 1) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    //Found preamble
    if (scanChannelFlag == 2) {
        Log.verboseln(F("Preamble detected while waiting"));
        waitBeforeSend(repeatedDetectPreambles + 1);
        return;
    }

    scanChannelFlag = 0;
}

bool LoraMesher::sendPacket(Packet<uint8_t>* p) {
    waitBeforeSend(1);

    //Disable interrupts
    enableInterrupt = false;

    //Blocking transmit, it is necessary due to deleting the packet after sending it. 
    int resT = radio->transmit(reinterpret_cast<uint8_t*>(p), p->getPacketLength());

    //Enable interrupts
    enableInterrupt = true;

    //Start receiving again after sending a packet
    startReceiving();

    if (resT != RADIOLIB_ERR_NONE) {
        Log.errorln(F("Transmit gave error: %d"), resT);
        return false;
    }

    printHeaderPacket(p, "send");
    return true;
}

void LoraMesher::sendPackets() {
    vTaskSuspend(NULL);

    int sendCounter = 0;
    uint8_t sendId = 0;
    uint8_t resendMessage = 0;

    randomSeed(getLocalAddress());

    const uint8_t dutyCycleEvery = (100 - LM_DUTY_CYCLE) / portTICK_PERIOD_MS;

    for (;;) {

        /* Wait for the notification of new packet has to be sent and enter blocking */
        ulTaskNotifyTake(pdPASS, 30000 / portTICK_PERIOD_MS);

        while (ToSendPackets->getLength() > 0) {

            ToSendPackets->setInUse();

            Log.verboseln("Size of Send Packets Queue: %d", ToSendPackets->getLength());

            QueuePacket<Packet<uint8_t>>* tx = ToSendPackets->Pop();

            ToSendPackets->releaseInUse();

            if (tx) {
                Log.verboseln("Send n. %d", sendCounter);

                if (tx->packet->src == getLocalAddress())
                    tx->packet->id = sendId++;

                //If the packet has a data packet and its destination is not broadcast add the via to the packet and forward the packet
                if (PacketService::isDataPacket(tx->packet->type) && tx->packet->dst != BROADCAST_ADDR) {
                    uint16_t nextHop = RoutingTableService::getNextHop(tx->packet->dst);

                    //Next hop not found
                    if (nextHop == 0) {
                        Log.errorln(F("NextHop Not found from %X, destination %X"), tx->packet->src, tx->packet->dst);
                        PacketQueueService::deleteQueuePacketAndPacket(tx);
                        incDestinyUnreachable();
                        continue;
                    }

                    (reinterpret_cast<DataPacket*>(tx->packet))->via = nextHop;
                }

                //Send packet
                bool hasSend = sendPacket(tx->packet);

                sendCounter++;

                if (hasSend) {
                    incSendPackets();
                    incSentPayloadBytes(PacketService::getPacketPayloadLengthWithoutControl(tx->packet));
                    incSentControlBytes(PacketService::getControlLength(tx->packet));
                    if (tx->packet->src != getLocalAddress())
                        incForwardedPackets();
                }

                //TODO: If the packet has not been send, add it to the queue and send it again
                if (!hasSend && resendMessage < MAX_RESEND_PACKET) {
                    tx->priority = MAX_PRIORITY;
                    PacketQueueService::addOrdered(ToSendPackets, tx);

                    resendMessage++;
                    continue;
                }

                resendMessage = 0;

                uint32_t timeOnAir = radio->getTimeOnAir(tx->packet->getPacketLength()) / 1000;

                TickType_t delayBetweenSend = timeOnAir * dutyCycleEvery;

                Log.verboseln("TimeOnAir %d ms, next message in %d ms", timeOnAir, delayBetweenSend);

                PacketQueueService::deleteQueuePacketAndPacket(tx);

                vTaskDelay(delayBetweenSend);
            }
        }
    }
}

void LoraMesher::sendHelloPacket() {
    vTaskSuspend(NULL);

    //Wait an initial 2 second
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    for (;;) {
        Log.traceln(F("Creating Routing Packet"));

        incSentHelloPackets();

        NetworkNode* nodes = RoutingTableService::getAllNetworkNodes();

        RoutePacket* tx = PacketService::createRoutingPacket(
            getLocalAddress(), nodes, RoutingTableService::routingTableSize(), RoleService::getRole());

        delete[] nodes;

        setPackedForSend(reinterpret_cast<Packet<uint8_t>*>(tx), DEFAULT_PRIORITY + 1);

        //Wait for HELLO_PACKETS_DELAY seconds to send the next hello packet
        vTaskDelay(HELLO_PACKETS_DELAY * 1000 / portTICK_PERIOD_MS);
    }
}

void LoraMesher::processPackets() {
    vTaskSuspend(NULL);

    for (;;) {
        /* Wait for the notification of receivingRoutine and enter blocking */
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);

        Log.verboseln("Size of Received Packets Queue: %d", ReceivedPackets->getLength());

        while (ReceivedPackets->getLength() > 0) {
            QueuePacket<Packet<uint8_t>>* rx = ReceivedPackets->Pop();

            if (rx) {
                printHeaderPacket(rx->packet, "received");

                uint8_t type = rx->packet->type;

                incReceivedPayloadBytes(PacketService::getPacketPayloadLengthWithoutControl(rx->packet));
                incReceivedControlBytes(PacketService::getControlLength(rx->packet));

                if (PacketService::isHelloPacket(type)) {
                    incRecHelloPackets();

                    RoutingTableService::processRoute(reinterpret_cast<RoutePacket*>(rx->packet), rx->snr);
                    PacketQueueService::deleteQueuePacketAndPacket(rx);

                }
                else if (PacketService::isDataPacket(type))
                    processDataPacket(reinterpret_cast<QueuePacket<DataPacket>*>(rx));

                else {
                    Log.verboseln(F("Packet not identified, deleting it"));
                    incReceivedNotForMe();
                    PacketQueueService::deleteQueuePacketAndPacket(rx);
                }
            }
        }
    }
}

void LoraMesher::packetManager() {
    vTaskSuspend(NULL);

    for (;;) {
        RoutingTableService::manageTimeoutRoutingTable();
        managerReceivedQueue();
        managerSendQueue();

        vTaskDelay(DEFAULT_TIMEOUT * 1000 / portTICK_PERIOD_MS);
    }
}

void LoraMesher::printHeaderPacket(Packet<uint8_t>* p, String title) {
    String log = F("Packet %s -- Size: %d Src: %X Dst: %X Id: %d Type: %b");
    if (PacketService::isDataPacket(p->type)) {
        log += F(" Via: %X");
        if (PacketService::isControlPacket(p->type)) {
            log += F(" Seq_Id: %d Num: %d");
        }
    }

    Log.verboseln(log.c_str(), title.c_str(), p->getPacketLength(), p->src, p->dst, p->id, p->type,
        (reinterpret_cast<DataPacket*>(p))->via,
        (reinterpret_cast<ControlPacket*>(p))->seq_id,
        (reinterpret_cast<ControlPacket*>(p))->number);
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
    size_t maxPayloadSize = PacketService::getMaximumPayloadLengthControlPacket(type);

    //Number of packets
    uint16_t numOfPackets = payloadSize / maxPayloadSize + (payloadSize % maxPayloadSize > 0);

    //Create a new Linked list to store the QueuePackets and the payload
    LM_LinkedList<QueuePacket<ControlPacket>>* packetList = new LM_LinkedList<QueuePacket<ControlPacket>>();

    //Add the SYNC configuration packet
    packetList->Append(getStartSequencePacketQueue(dst, seq_id, numOfPackets));

    for (uint16_t i = 1; i <= numOfPackets; i++) {
        //Get the position of the payload
        uint8_t* payloadToSend = reinterpret_cast<uint8_t*>((unsigned long) payload + ((i - 1) * maxPayloadSize));

        //Get the payload Size in bytes
        size_t payloadSizeToSend = maxPayloadSize;
        if (i == numOfPackets)
            payloadSizeToSend = payloadSize - (maxPayloadSize * (numOfPackets - 1));

        //Create a new packet with the previous payload
        ControlPacket* cPacket = PacketService::createControlPacket(dst, getLocalAddress(), type, payloadToSend, payloadSizeToSend);
        cPacket->number = i;
        cPacket->seq_id = seq_id;

        //Create a packet queue
        QueuePacket<ControlPacket>* pq = PacketQueueService::createQueuePacket(cPacket, DEFAULT_PRIORITY, i);

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
    sendPacketSequence(listConfig, 0);
}


void LoraMesher::processDataPacket(QueuePacket<DataPacket>* pq) {
    DataPacket* packet = pq->packet;

    incReceivedDataPackets();

    Log.traceln(F("Data packet from %X, destination %X, via %X"), packet->src, packet->dst, packet->via);

    if (packet->dst == getLocalAddress()) {
        Log.verboseln(F("Data packet from %X for me"), packet->src);
        incDataPacketForMe();

        processDataPacketForMe(pq);
        return;

    }
    else if (packet->dst == BROADCAST_ADDR) {
        Log.verboseln(F("Data packet from %X BROADCAST"), packet->src);
        incReceivedBroadcast();
        processDataPacketForMe(pq);
        return;

    }
    else if (packet->via == getLocalAddress()) {
        Log.verboseln(F("Data Packet from %X for %X. Via is me. Forwarding it"), packet->src, packet->dst);
        incReceivedIAmVia();
        addToSendOrderedAndNotify(reinterpret_cast<QueuePacket<Packet<uint8_t>>*>(pq));
        return;
    }

    Log.verboseln(F("Packet not for me, deleting it"));
    incReceivedNotForMe();
    PacketQueueService::deleteQueuePacketAndPacket(pq);
}

void LoraMesher::processDataPacketForMe(QueuePacket<DataPacket>* pq) {
    DataPacket* p = pq->packet;
    ControlPacket* cPacket = reinterpret_cast<ControlPacket*>(p);

    //By default, delete the packet queue at the finish of this function
    bool deleteQueuePacket = true;

    bool needAck = PacketService::isNeedAckPacket(p->type);

    if (PacketService::isOnlyDataPacket(p->type)) {
        Log.verboseln(F("Data Packet received"));
        //Convert the packet into a user packet
        AppPacket<uint8_t>* appPacket = PacketService::convertPacket(p);

        //Add and notify the user of this packet
        notifyUserReceivedPacket(appPacket);

    }
    else if (PacketService::isAckPacket(p->type)) {
        Log.verboseln(F("ACK Packet received"));
        addAck(p->src, cPacket->seq_id, cPacket->number);

    }
    else if (PacketService::isLostPacket(p->type)) {
        Log.verboseln(F("Lost Packet received"));
        processLostPacket(p->src, cPacket->seq_id, cPacket->number);

    }
    else if (PacketService::isSyncPacket(p->type)) {
        Log.verboseln(F("Synchronization Packet received"));
        processSyncPacket(p->src, cPacket->seq_id, cPacket->number);

        //Change the number to send the ack to the correct one
        //cPacket->number in SYNC_P specify the number of packets and it needs to ACK the 0
        sendAckPacket(p->src, cPacket->seq_id, 0);
        needAck = false;

    }
    else if (PacketService::isXLPacket(p->type)) {
        Log.verboseln(F("Large payload Packet received"));
        processLargePayloadPacket(reinterpret_cast<QueuePacket<ControlPacket>*>(pq));
        needAck = false;
        deleteQueuePacket = false;
    }

    //Need ack
    if (needAck) {
        //TODO: All packets with this ack will ack?
        Log.verboseln(F("Previous packet need an ACK"));
        sendAckPacket(p->src, cPacket->seq_id, cPacket->number);
    }

    if (deleteQueuePacket)
        //Delete packet queue
        PacketQueueService::deleteQueuePacketAndPacket(pq);
}

void LoraMesher::notifyUserReceivedPacket(AppPacket<uint8_t>* appPacket) {
    if (ReceiveAppData_TaskHandle) {
        ReceivedAppPackets->setInUse();
        //Add the packet inside the receivedUsers Queue
        ReceivedAppPackets->Append(appPacket);

        ReceivedAppPackets->releaseInUse();

        //Notify the received user task handle
        xTaskNotify(
            ReceiveAppData_TaskHandle,
            0,
            eSetValueWithOverwrite);

    }
    else
        deletePacket(appPacket);
}

void LoraMesher::recalculateMaxTimeOnAir() {
    maxTimeOnAir = radio->getTimeOnAir(MAXPACKETSIZE) / 1000;
    Log.verboseln(F("Max Time on Air changed %d ms"), maxTimeOnAir);
}

/**
 *  End Region Packet Service
**/


/**
 *  Region Routing Table
**/

int LoraMesher::routingTableSize() {
    return RoutingTableService::routingTableSize();
}

/**
 *  End Region Routing Table
**/


/**
 *  Region PacketQueue
**/

size_t LoraMesher::getReceivedQueueSize() {
    return ReceivedAppPackets->getLength();
}

size_t LoraMesher::getSendQueueSize() {
    return ToSendPackets->getLength();
}

void LoraMesher::addToSendOrderedAndNotify(QueuePacket<Packet<uint8_t>>* qp) {
    PacketQueueService::addOrdered(ToSendPackets, qp);
    Log.traceln(F("Added packet to Q_SP, notifying sender task"));

    //Notify the sendData task handle
    xTaskNotify(SendData_TaskHandle, 0, eSetValueWithOverwrite);
}

/**
 *  End Region PacketQueue
**/


/**
 * Large and Reliable payloads
 */

QueuePacket<ControlPacket>* LoraMesher::getStartSequencePacketQueue(uint16_t destination, uint8_t seq_id, uint16_t num_packets) {
    uint8_t type = SYNC_P | NEED_ACK_P | XL_DATA_P;

    //Create the packet
    ControlPacket* cPacket = PacketService::createEmptyControlPacket(destination, getLocalAddress(), type, seq_id, num_packets);

    //Create a packet queue
    return PacketQueueService::createQueuePacket(cPacket, DEFAULT_PRIORITY, 0);
}

void LoraMesher::sendAckPacket(uint16_t destination, uint8_t seq_id, uint16_t seq_num) {
    uint8_t type = ACK_P;

    //Create the packet
    ControlPacket* cPacket = PacketService::createEmptyControlPacket(destination, getLocalAddress(), type, seq_id, seq_num);

    setPackedForSend(reinterpret_cast<Packet<uint8_t>*>(cPacket), DEFAULT_PRIORITY + 1);
}

void LoraMesher::sendLostPacket(uint16_t destination, uint8_t seq_id, uint16_t seq_num) {
    uint8_t type = LOST_P;

    //Create the packet
    ControlPacket* cPacket = PacketService::createEmptyControlPacket(destination, getLocalAddress(), type, seq_id, seq_num);

    setPackedForSend(reinterpret_cast<Packet<uint8_t>*>(cPacket), DEFAULT_PRIORITY);
}

bool LoraMesher::sendPacketSequence(listConfiguration* lstConfig, uint16_t seq_num) {
    //Get the packet queue with the sequence number
    QueuePacket<ControlPacket>* pq = PacketQueueService::findPacketQueue(lstConfig->list, seq_num);

    if (pq == nullptr) {
        Log.errorln(F("NOT FOUND the packet queue with Seq_id: %d, Num: %d"), lstConfig->config->seq_id, seq_num);
        return false;
    }

    if (lstConfig->config->lastAck > seq_num) {
        Log.errorln(F("Trying to send packet sequence previously acknowledged Seq_id: %d, Num: %d"), lstConfig->config->seq_id, seq_num);
        return false;
    }

    //Create the packet
    Packet<uint8_t>* p = PacketService::copyPacket(pq->packet, pq->packet->getPacketLength());

    //Add the packet to the send queue
    setPackedForSend(p, DEFAULT_PRIORITY);

    return true;
}

void LoraMesher::addAck(uint16_t source, uint8_t seq_id, uint16_t seq_num) {
    listConfiguration* config = findSequenceList(q_WSP, seq_id, source);
    if (config == nullptr) {
        Log.errorln(F("NOT FOUND the sequence packet config in add ack with Seq_id: %d, Source: %d"), seq_id, source);
        return;
    }

    //If all packets has been arrived to the destiny
    //Delete this sequence
    if (config->config->number == seq_num) {
        Log.traceln(F("All the packets has been arrived to the seq_Id: %d"), seq_id);
        findAndClearLinkedList(q_WSP, config);
        return;
    }

    if (config->config->lastAck > seq_num) {
        Log.errorln(F("ACK received that has been yet acknowledged Seq_id: %d, Num: %d"), config->config->seq_id, seq_num);
        return;
    }

    //Set has been received some ACK
    config->config->firstAckReceived = 1;

    //Add the last ack to the config packet
    config->config->lastAck = seq_num;

    //Reset the timeouts
    resetTimeout(config->config);

    Log.verboseln(F("Sending next packet after receiving an ACK"));

    //Send the next packet sequence
    sendPacketSequence(config, seq_num + 1);
}

bool LoraMesher::processLargePayloadPacket(QueuePacket<ControlPacket>* pq) {
    ControlPacket* cPacket = pq->packet;

    listConfiguration* configList = findSequenceList(q_WRP, cPacket->seq_id, cPacket->src);
    if (configList == nullptr) {
        Log.errorln(F("NOT FOUND the sequence packet config in Process Large Payload with Seq_id: %d, Source: %d"), cPacket->seq_id, cPacket->src);
        PacketQueueService::deleteQueuePacketAndPacket(pq);
        return false;
    }

    if (configList->config->lastAck + 1 != cPacket->number) {
        Log.errorln(F("Sequence number received in bad order in seq_Id: %d, received: %d expected: %d"), cPacket->seq_id, cPacket->number, configList->config->lastAck + 1);
        sendLostPacket(cPacket->src, cPacket->seq_id, configList->config->lastAck + 1);

        PacketQueueService::deleteQueuePacketAndPacket(pq);
        return false;
    }

    configList->config->lastAck++;

    // actualizeRTT(configList);

    configList->list->setInUse();
    configList->list->Append(pq);
    configList->list->releaseInUse();

    //Send ACK
    sendAckPacket(cPacket->src, cPacket->seq_id, cPacket->number);

    //All packets has been arrived, join them and send to the user
    if (configList->config->lastAck == configList->config->number) {
        joinPacketsAndNotifyUser(configList);
        return true;
    }

    resetTimeout(configList->config);
    return true;
}

void LoraMesher::joinPacketsAndNotifyUser(listConfiguration* listConfig) {
    Log.verboseln(F("Joining packets seq_Id: %d Src: %X"), listConfig->config->seq_id, listConfig->config->source);

    LM_LinkedList<QueuePacket<ControlPacket>>* list = listConfig->list;

    list->setInUse();
    if (!list->moveToStart()) {
        list->releaseInUse();
        return;
    }

    //TODO: getPacketPayloadLength could be done when adding the packets inside the list
    size_t payloadSize = 0;
    size_t number = 1;

    do {
        ControlPacket* currentP = reinterpret_cast<ControlPacket*>(list->getCurrent()->packet);

        if (number != (currentP->number))
            //TODO: ORDER THE PACKETS if they are not ordered?
            Log.errorln(F("Wrong packet order"));

        number++;
        payloadSize += PacketService::getPacketPayloadLength(currentP);
    } while (list->next());

    //Move to start again
    list->moveToStart();

    ControlPacket* currentP = list->getCurrent()->packet;

    uint32_t appPacketLength = sizeof(AppPacket<uint8_t>);

    //Packet length = size of the packet + size of the payload
    uint32_t packetLength = appPacketLength + payloadSize;

    AppPacket<uint8_t>* p = static_cast<AppPacket<uint8_t>*>(malloc(packetLength));

    Log.verboseln(F("Large Packet Packet length: %d Payload Size: %d"), packetLength, payloadSize);

    if (p) {
        //Copy the payload into the packet
        unsigned long actualPayloadSizeDst = appPacketLength;

        do {
            currentP = list->getCurrent()->packet;

            size_t actualPayloadSizeSrc = PacketService::getPacketPayloadLength(currentP);

            memcpy(reinterpret_cast<void*>((unsigned long) p + (actualPayloadSizeDst)), currentP->payload, actualPayloadSizeSrc);
            actualPayloadSizeDst += actualPayloadSizeSrc;
        } while (list->next());
    }

    list->releaseInUse();

    //Set values to the AppPacket
    p->payloadSize = payloadSize;
    p->src = listConfig->config->source;
    p->dst = getLocalAddress();

    //TODO: When finished, clear everything? Or maintain the config until timeout?
    findAndClearLinkedList(q_WRP, listConfig);

    notifyUserReceivedPacket(p);
}

void LoraMesher::processSyncPacket(uint16_t source, uint8_t seq_id, uint16_t seq_num) {
    //Check for repeated sequence lists
    listConfiguration* listConfig = findSequenceList(q_WRP, seq_id, source);

    if (listConfig == nullptr) {
        //Create the pair of configuration
        listConfig = new listConfiguration();
        listConfig->config = new sequencePacketConfig(seq_id, source, seq_num);
        listConfig->list = new LM_LinkedList<QueuePacket<ControlPacket>>();

        listConfig->config->firstAckReceived = 1;

        //Add list configuration to the waiting received packets queue
        q_WRP->setInUse();
        q_WRP->Append(listConfig);
        q_WRP->releaseInUse();

        resetTimeout(listConfig->config);
    }
}

void LoraMesher::processLostPacket(uint16_t destination, uint8_t seq_id, uint16_t seq_num) {
    //Find the list config
    listConfiguration* listConfig = findSequenceList(q_WSP, seq_id, destination);

    if (listConfig == nullptr) {
        Log.errorln(F("NOT FOUND the sequence packet config in ost packet with Seq_id: %d, Source: %d"), seq_id, destination);
        return;
    }

    //TODO: Check for duplicate consecutive lost packets, set a timeout to resend the lost packet.

    //Send the packet sequence that has been lost
    if (sendPacketSequence(listConfig, seq_num)) {
        //Reset the timeout of this sequence packets inside the q_WSP
        addTimeout(listConfig->config);
    }
}

void LoraMesher::addTimeout(LM_LinkedList<listConfiguration>* queue, uint8_t seq_id, uint16_t source) {
    listConfiguration* config = findSequenceList(q_WSP, seq_id, source);
    if (config == nullptr) {
        Log.errorln(F("NOT FOUND the sequence packet config in add timeout with Seq_id: %d, Source: %d"), seq_id, source);
        return;
    }

    addTimeout(config->config);
}

void LoraMesher::resetTimeout(sequencePacketConfig* configPacket) {
    configPacket->numberOfTimeouts = 0;
    addTimeout(configPacket);
}

void LoraMesher::actualizeRTT(listConfiguration* config) {
    uint32_t actualRTT = config->config->timeout - millis();
    uint16_t numberOfPackets = config->config->lastAck;

    if (config->config->RTT = 0) {
        config->config->RTT = actualRTT;
        return;
    }

    config->config->RTT = (actualRTT + config->config->RTT * numberOfPackets) / (numberOfPackets + 1);
}

void LoraMesher::clearLinkedList(listConfiguration* listConfig) {
    LM_LinkedList<QueuePacket<ControlPacket>>* list = listConfig->list;
    list->setInUse();
    Log.traceln(F("Clearing list configuration Seq_Id: %d Src: %X"), listConfig->config->seq_id, listConfig->config->source);

    for (int i = 0; i < list->getLength(); i++) {
        PacketQueueService::deleteQueuePacketAndPacket(list->getCurrent());
        list->DeleteCurrent();

    }

    delete listConfig->list;
    delete listConfig->config;
    delete listConfig;
}

void LoraMesher::findAndClearLinkedList(LM_LinkedList<listConfiguration>* queue, listConfiguration* listConfig) {
    queue->setInUse();

    listConfiguration* current = queue->getCurrent();

    //Find list config inside the queue, if not find error
    if (current != listConfig && !queue->Search(listConfig)) {
        Log.errorln(F("Not found list config"));
        queue->releaseInUse();
        //TODO: What to do inside all errors????????????????
        return;
    }

    clearLinkedList(listConfig);
    queue->DeleteCurrent();

    queue->releaseInUse();
}

LoraMesher::listConfiguration* LoraMesher::findSequenceList(LM_LinkedList<listConfiguration>* queue, uint8_t seq_id, uint16_t source) {
    queue->setInUse();

    if (queue->moveToStart()) {
        do {
            listConfiguration* current = queue->getCurrent();

            if (current->config->seq_id == seq_id && current->config->source == source) {
                queue->releaseInUse();
                return current;
            }

        } while (queue->next());
    }

    queue->releaseInUse();

    return nullptr;

}


void LoraMesher::managerReceivedQueue() {
    Log.verboseln(F("Checking Q_WRP timeouts"));

    q_WRP->setInUse();
    if (q_WRP->moveToStart()) {
        do {
            listConfiguration* current = q_WRP->getCurrent();

            //Get Config packet
            sequencePacketConfig* configPacket = current->config;

            //If Config packet have reached timeout
            if (configPacket->timeout < millis()) {
                //Increment number of timeouts
                configPacket->numberOfTimeouts++;

                Log.warningln(F("Timeout reached from Waiting Received Queue, Seq_Id: %d, N.TimeOuts %d"), configPacket->seq_id, configPacket->numberOfTimeouts);

                //If number of timeouts is greater than Max timeouts, erase it
                if (configPacket->numberOfTimeouts >= MAX_TIMEOUTS) {
                    Log.errorln(F("MAX TIMEOUTS reached from Waiting Received Queue, erasing Id: %d"), configPacket->seq_id);
                    clearLinkedList(current);
                    q_WRP->DeleteCurrent();
                    continue;
                }

                addTimeout(configPacket);

                if (configPacket->lastAck != 0) {
                    //Send Last ACK + 1 (Request this packet)
                    sendLostPacket(configPacket->source, configPacket->seq_id, configPacket->lastAck + 1);
                }
            }
        } while (q_WRP->next());
    }

    q_WRP->releaseInUse();
}

void LoraMesher::managerSendQueue() {
    Log.verboseln(F("Checking Q_WSP timeouts"));

    q_WSP->setInUse();

    if (q_WSP->moveToStart()) {
        do {
            listConfiguration* current = q_WSP->getCurrent();

            //Get Config packet
            sequencePacketConfig* configPacket = current->config;

            //If Config packet have reached timeout
            if (configPacket->timeout < millis()) {
                //Increment number of timeouts
                configPacket->numberOfTimeouts++;

                Log.warningln(F("Timeout reached from Waiting Send Queue, Seq_Id: %d, N.TimeOuts %d"), configPacket->seq_id, configPacket->numberOfTimeouts);

                //If number of timeouts is greater than Max timeouts, erase it
                if (configPacket->numberOfTimeouts >= MAX_TIMEOUTS) {
                    Log.errorln(F("MAX TIMEOUTS reached from Waiting Send Queue, erasing Id: %d"), configPacket->seq_id);
                    clearLinkedList(current);
                    q_WSP->DeleteCurrent();
                    continue;
                }

                addTimeout(configPacket);

                //Repeat the configPacket ACK
                if (configPacket->firstAckReceived == 0)
                    //Send the first packet of the sequence (SYNC packet)
                    sendPacketSequence(current, 0);
            }
        } while (q_WSP->next());
    }

    q_WSP->releaseInUse();
}

void LoraMesher::addTimeout(sequencePacketConfig* configPacket) {
    //TODO: This timeout should account for the number of send packets waiting to send + how many time between send packets?
    //TODO: This timeout should be a little variable depending on the duty cycle. 
    //TODO: Account for how many hops the packet needs to do
    //TODO: Account for how many packets are inside the Q_SP
    uint8_t hops = RoutingTableService::getNumberOfHops(configPacket->source);
    if (hops = 0) {
        Log.errorln(F("Find next hop in add timeout"));
        configPacket->timeout = 0;
        return;
    }

    configPacket->timeout = millis() + DEFAULT_TIMEOUT * 1000 * hops;
}

uint8_t LoraMesher::getSequenceId() {
    if (sequence_id == 255) {
        sequence_id = 0;
        return 255;
    }

    uint8_t seqId = sequence_id;
    sequence_id++;

    return seqId;
}

/**
 * End Large and Reliable payloads
 */
