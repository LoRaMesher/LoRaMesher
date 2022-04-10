#include "loramesher.h"

LoraMesher::LoraMesher() {}

void LoraMesher::init(void (*func)(void*)) {
  Log.begin(LOG_LEVEL_VERBOSE, &Serial);
  dutyCycleEnd = 0;
  lastSendTime = 0;
  routeTimeout = 10000000;
  initializeLocalAddress();
  initializeLoRa();
  initializeScheduler(func);

  delay(1000);
  Log.verbose(F("Initialization DONE, starting receiving packets..." CR));
  int res = radio->startReceive();
  if (res != 0)
    Log.error(F("Receiving on constructor gave error: %d" CR), res);
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

  Log.notice(F("Local LoRa address (from WiFi MAC): %X" CR), localAddress);
}

void LoraMesher::initializeLoRa() {
  Log.trace(F("LoRa module initialization..." CR));

  // TODO: Optimize memory, this could lead to heap fragmentation
  Log.verbose(F("Initializing Radiolib" CR));
  Module* mod = new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_IO1);
  radio = new SX1276(mod);
  if (radio == NULL) {
    Log.error(F("Radiolib not initialized properly" CR));
  }

  // Set up the radio parameters
  Log.verbose(F("Initializing radio" CR));
  int res = radio->begin(BAND, BANDWIDTH, LORASF, 7U, 18U, 10);
  if (res != 0) {
    Log.error(F("Radio module gave error: %d" CR), res);
  }

#ifdef RELIABLE_PAYLOAD
  radio->setCRC(true);
#endif

  Log.verbose(F("Setting up callback function" CR));
  radio->setDio0Action(onReceive);

  Log.trace(F("LoRa module initialization DONE" CR));

  delay(1000);
}

int helloCounter = 0;
void LoraMesher::initializeScheduler(void (*func)(void*)) {
  Log.verbose(F("Setting up Schedulers" CR));
  int res = xTaskCreate(
    [](void* o) { static_cast<LoraMesher*>(o)->receivingRoutine(); },
    "Receiving routine",
    4096,
    this,
    6,
    &ReceivePacket_TaskHandle);
  if (res != pdPASS) {
    Log.error(F("Receiving routine creation gave error: %d" CR), res);
  }
  res = xTaskCreate(
    [](void* o) { static_cast<LoraMesher*>(o)->sendHelloPacket(); },
    "Hello routine",
    4096,
    this,
    5,
    &Hello_TaskHandle);
  if (res != pdPASS) {
    Log.error(F("Process Task creation gave error: %d" CR), res);
  }
  res = xTaskCreate(
    [](void* o) { static_cast<LoraMesher*>(o)->processPackets(); },
    "Process routine",
    4096,
    this,
    3,
    &ReceiveData_TaskHandle);
  if (res != pdPASS) {
    Log.error(F("Process Task creation gave error: %d" CR), res);
  }
  res = xTaskCreate(
    [](void* o) { static_cast<LoraMesher*>(o)->sendPackets(); },
    "Sending routine",
    4096,
    this,
    4,
    &SendData_TaskHandle);
  if (res != pdPASS) {
    Log.error(F("Sending Task creation gave error: %d" CR), res);
  }
  res = xTaskCreate(
    func,
    "Receive User routine",
    4096,
    this,
    2,
    &ReceivedUserData_TaskHandle);
  if (res != pdPASS) {
    Log.error(F("Receive User Task creation gave error: %d" CR), res);
  }
  // res = xTaskCreate(
  //   [](void* o) { static_cast<LoraMesher*>(o)->packetManager(); },
  //   "Packet Manager routine",
  //   4096,
  //   this,
  //   2,
  //   &PacketManager_TaskHandle);
  // if (res != pdPASS) {
  //   Log.error(F("Packet Manager Task creation gave error: %d" CR), res);
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

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void LoraMesher::onFinishTransmit() {
  //Add the onReceive function into the Dio0 Action again.
  LoraMesher::getInstance().radio->setDio0Action(onReceive);

  //Start receiving packets again
  int res = LoraMesher::getInstance().radio->startReceive();
  if (res != 0) {
    Log.error(F("Receiving on end of packet transmission gave error: %d" CR), res);
    return;
  }

  Log.verbose(F("Packet send" CR));
}

void LoraMesher::receivingRoutine() {
  BaseType_t TWres;
  bool receivedFlag = false;
  int packetSize;
  int rssi, snr, res;
  for (;;) {
    TWres = xTaskNotifyWait(
      pdFALSE,
      ULONG_MAX,
      NULL,
      portMAX_DELAY // The most amount of time possible
    );
    receivedFlag = false;

    if (TWres == pdPASS) {
      packetSize = radio->getPacketLength();
      if (packetSize == 0)
        Log.warning(F("Empty packet received" CR));

      else {
        //TODO: Optimize memory, no maxpayloadSize, set packetSize as the size.
        uint8_t payload[MAXPAYLOADSIZE];
        packet<uint8_t>* rx = createPacket<uint8_t>(payload, MAXPAYLOADSIZE, 0);

        rssi = radio->getRSSI();
        snr = radio->getSNR();

        Log.notice(F("Receiving LoRa packet: Size: %d bytes RSSI: %d SNR: %d" CR), packetSize, rssi, snr);

        if (packetSize > MAXPAYLOADSIZE) {
          Log.notice(F("Received packet with size greater than MAX Payload Size" CR));
          res = radio->readData((uint8_t*) rx, MAXPAYLOADSIZE);
        } else
          res = radio->readData((uint8_t*) rx, packetSize);

        if (res != 0) {
          Log.error(F("Reading packet data gave error: %d" CR), res);
        } else {
          //Set the received flag to true
          receivedFlag = true;

          //Add the packet created into the ReceivedPackets List
          packetQueue<uint8_t>* pq = createPacketQueue<uint8_t>(rx, 15);
          ReceivedPackets->Add(pq);

          Log.verbose(F("Starting to listen again after receiving a packet" CR));
          res = radio->startReceive();
          if (res != 0) {
            Log.error(F("Receiving on end of listener gave error: %d" CR), res);
          }
        }
        if (receivedFlag) {
          //Notify that there is a packets to be processed
          TWres = xTaskNotifyFromISR(
            ReceiveData_TaskHandle,
            0,
            eSetValueWithoutOverwrite, &TWres);
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

template <typename T>
void LoraMesher::sendPacket(LoraMesher::packet<T>* p) {
  //Put Radio on StandBy mode. Prevent receiving packets when changing the Dio0 action.
  radio->standby();

  //Set onFinishTransmit Dio0 Action
  radio->setDio0Action(onFinishTransmit);

  //Blocking transmit, it is necessary due to deleting the packet after sending it. 
  int res = radio->startTransmit((uint8_t*) p, getPacketLength(p));

  if (res != 0) {
    Log.error(F("Transmit gave error: %d" CR), res);
    return;
  }

  Log.trace(F("Sending packet of type %d" CR), p->type);
}

void LoraMesher::sendPackets() {
  for (;;) {
    /* Wait for the notification of receivingRoutine and enter blocking */
    Log.verbose("Size of Send Packets Queue: %d" CR, ToSendPackets->Size());

    //Set a random delay, to avoid some collisions. Between 1 and 4 seconds
    // vTaskDelay((rand() % 4 + 1) * 1000 / portTICK_PERIOD_MS);

    packetQueue<uint8_t>* tx = ToSendPackets->Pop<uint8_t>();

    if (tx != nullptr) {
      Log.verbose("Send nº %d" CR, helloCounter);
      helloCounter++;

      //If the packet has a data packet add the via to the packet and forward the packet
      if (hasDataPacket(tx->packet->type)) {
        uint16_t nextHop = getNextHop(tx->packet->dst);
        if (nextHop != 0) {
          ((LoraMesher::dataPacket<uint8_t>*) (&tx->packet->payload))->via = nextHop;
          Log.trace(F("NextHop %X" CR), nextHop);
          Log.trace(F("Sending data packet from %X, destination %X, via %X" CR), tx->packet->src, tx->packet->dst, nextHop);
        } else
          Log.error(F("NextHop Not found from %X, destination %X, via %X" CR), tx->packet->src, tx->packet->dst, nextHop);
      }

      sendPacket(tx->packet);

      deletepacketQueue(tx);
    }

    //TODO: This should be regulated about what time we can send a packet, in order to accomplish the regulations 
    //Wait for 10s to send the next packet
    // vTaskDelay((rand() % 10 + 5) * 1000 / portTICK_PERIOD_MS);
    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
}

void LoraMesher::sendHelloPacket() {
  for (;;) {
    packet<networkNode>* tx = createRoutingPacket();
    setPackedForSend(tx, DEFAULT_PRIORITY + 1);

    //Wait for 60s to send the next hello packet
    vTaskDelay(60000 / portTICK_PERIOD_MS);
  }
}

template <typename T>
void LoraMesher::setPackedForSend(packet<T>* tx, uint8_t priority) {
  LoraMesher::packetQueue<uint8_t>* send = createPacketQueue<uint8_t>(tx, priority);
  ToSendPackets->Add(send);
  //TODO: Using vTaskDelay to kill the packet inside LoraMesher
}

void LoraMesher::processPackets() {
  for (;;) {
    /* Wait for the notification of receivingRoutine and enter blocking */
    ulTaskNotifyTake(pdPASS, portMAX_DELAY);

    Log.verbose("Size of Received Packets Queue: %d" CR, ReceivedPackets->Size());
    packetQueue<uint8_t>* rx = ReceivedPackets->Pop<uint8_t>();

    if (rx != nullptr) {
      uint8_t type = rx->packet->type;

      if ((type & HELLO_P) == HELLO_P) {
        processRoute((packet<networkNode>*) rx->packet);
        deletepacketQueue(rx);
      } else if ((type & DATA_P) == DATA_P || (type & XL_DATA_P) == XL_DATA_P)
        processDataPacket((packetQueue<dataPacket<uint8_t>>*) rx);
      else {
        Log.verbose(F("Packet not identified, deleting it" CR));
        deletepacketQueue(rx);
      }
    }
  }
}

template <typename T>
void LoraMesher::createPacketAndSend(uint16_t dst, T* payload, uint8_t payloadSize) {
  //Cannot send an empty packet
  if (payloadSize == 0)
    return;

  //Get the size of the payload in bytes
  size_t payloadSizeInBytes = payloadSize * sizeof(T);

  //Create the packet and set it to the send queue
  setPackedForSend(createPacket(dst, getLocalAddress(), DATA_P, payload, payloadSizeInBytes), DEFAULT_PRIORITY);
}

template <typename T>
void LoraMesher::sendReliable(uint16_t dst, T* payload, uint8_t payloadSize) {
  //Cannot send an empty packet?? Maybe it could be like a ping?
  if (payloadSize == 0)
    return;
  //TODO: Could it send reliable messages to broadcast?
  if (dst == BROADCAST_ADDR)
    return;

  //Generate a sequence Id for this list of packets
  uint8_t sequence_id = getSequenceId();

  //Get the Type of the packet
  uint8_t type = NEED_ACK_P | XL_DATA_P;

  //Get the extra length of the headers
  auto extraLength = getExtraLengthToPayload(type);

  //Get the payload size in bytes
  size_t payloadInBytes = sizeof(T) * payloadSize;

  //Create a new Linked list to store the packetQueues and the payload
  LinkedList<packetQueue<uint8_t>>* packetList = new LinkedList<packetQueue<uint8_t>>();

  //Max payload size per packet
  size_t maxPayloadSize = MAXPAYLOADSIZE - extraLength;

  //Number of packets
  size_t numOfPackets = payloadInBytes / maxPayloadSize + (payloadInBytes % maxPayloadSize > 0);

  for (int i = 0; i < numOfPackets; i++) {
    //Get the position of the payload
    uint8_t* payloadToSend = (uint8_t*) ((unsigned long) payload + i * maxPayloadSize);

    //Get the payload Size in bytes
    size_t payloadSizeToSend = maxPayloadSize;
    if (i == numOfPackets - 1)
      payloadSizeToSend = payloadInBytes - (maxPayloadSize * (numOfPackets - 1));

    //Create a new packet with the previous payload
    packet<T>* packet = createPacket(dst, getLocalAddress(), type, payloadToSend, payloadSizeToSend);

    //Add control Packet information
    controlPacket<T>* cPacket = ((controlPacket<T>*) ((dataPacket<T>*) (packet->payload))->payload);
    cPacket->number = i;
    cPacket->seq_id = sequence_id;

    //Create a packet queue
    packetQueue<T>* pq = createPacketQueue<uint8_t>(packet, DEFAULT_PRIORITY, DEFAULT_TIMEOUT, i);

    //Append the packet queue in the linked list
    packetList->Append(pq);
  }

  //Create the pair of configuration
  listConfiguration* listConfig = new listConfiguration();
  listConfig->config = new sequencePacketConfig();
  listConfig->list = packetList;

  dataList->config->seq_id = sequence_id;
  dataList->config->number = numOfPackets;
  dataList->second = packetList;

  //Add dataList pair to the waiting send packets queue
  q_WSP->Append(dataList);

  //Create configuration packet, send it and wait for ack to send first element of sequence
  sendStartSequencePackets(dst, sequence_id, numOfPackets);
}

template <typename T>
size_t LoraMesher::getPacketLength(LoraMesher::packet<T>* p) {
  return sizeof(LoraMesher::packet<T>) + p->payloadSize + getExtraLengthToPayload(p->type);
}

size_t LoraMesher::getExtraLengthToPayload(uint8_t type) {
  //Offset to the payload
  size_t extraSize = 0;

  //Check if type contains DATA_P or XL_DATA_P, add via size to extra size
  if (hasDataPacket(type))
    extraSize += sizeof(LoraMesher::dataPacket<uint8_t>);

  //Check if needs a control packet given a type. Add seq_id and number size to extra size
  if (hasControlPacket(type))
    extraSize += sizeof(LoraMesher::controlPacket<uint8_t>);

  return extraSize;
}

bool LoraMesher::hasDataPacket(uint8_t type) {
  return (type & DATA_P) == DATA_P || (type & XL_DATA_P) == XL_DATA_P;
}

bool LoraMesher::hasControlPacket(uint8_t type) {
  return (type & NEED_ACK_P) == NEED_ACK_P || (type & ACK_P) == ACK_P;
}

template <typename T>
T* LoraMesher::getPayload(packet<T>* packet) {
  return (T*) ((unsigned long) packet->payload + getExtraLengthToPayload(packet->type));
}

template <typename T>
size_t LoraMesher::getPayloadLength(LoraMesher::packet<T>* p) {
  return p->payloadSize / sizeof(T);
}

template <typename T>
void LoraMesher::printPacket(LoraMesher::packet<T>* p, bool received) {
  Log.verbose(F("-----------------------------------------\n"));
  Log.verbose(F("Current Packet: %s\n"), received ? "Received" : "Created");
  Log.verbose(F("Destination: %X\n"), p->dst);
  Log.verbose(F("Source: %X\n"), p->src);
  Log.verbose(F("Type: %d\n"), p->type);
  Log.verbose(F("----Packet of size %d bytes----\n"), getPacketLength(p));
  switch (p->type) {
    case HELLO_P:
      for (int i = 0; i < getPayloadLength((packet<networkNode>*) p); i++)
        Log.verbose(F("%d ->(%u) Address: %X - Metric: %d" CR), i, &p->payload[i], ((networkNode*) &p->payload[i])->address, ((networkNode*) &p->payload[i])->metric);

  }

  Log.verbose(F("-----------------------------------------\n"));
}

template <typename T>
void LoraMesher::deletePacket(LoraMesher::packet<T>* p) {
  free(p);
}

void LoraMesher::processDataPacket(LoraMesher::packetQueue<dataPacket<uint8_t>>* pq) {
  packet<dataPacket<uint8_t>>* packet = pq->packet;

  Log.trace(F("Data packet from %X, destination %X, via %X" CR), packet->src, packet->dst, packet->payload->via);

  if (packet->dst == localAddress) {
    Log.verbose(F("Data packet from %X for me" CR), packet->src);
    processDataPacketForMe(pq);
    return;

  } else if (packet->dst == BROADCAST_ADDR) {
    Log.verbose(F("Data packet from %X BROADCAST" CR), packet->src);
    processDataPacketForMe(pq);
    return;

  } else if (packet->payload->via == localAddress) {
    Log.verbose(F("Data Packet from %X for %X. Via is me" CR), packet->src, packet->dst);

    if (hasAddresRoutingTable(packet->dst)) {
      Log.verbose(F("Data Packet forwarding it." CR));
      ToSendPackets->Add((packetQueue<uint8_t>*) pq);
      return;
    }

    Log.verbose(F("Data Packet destination not reachable, deleting it." CR));
    deletepacketQueue(pq);

    return;
  }

  Log.verbose(F("Packet not for me, deleting it" CR));
  deletepacketQueue(pq);
}

void LoraMesher::processDataPacketForMe(LoraMesher::packetQueue<dataPacket<uint8_t>>* pq) {
  packet<dataPacket<uint8_t>>* packet = pq->packet;
  controlPacket<uint8_t>* cPacket = (controlPacket<uint8_t>*) packet->payload->payload;

  notifyUserReceivedPacket(pq);

  if ((packet->type & ACK_P) == ACK_P) {
    //TODO: Add the ack to the specified queue and send the next packet
    //TODO: If all sended, remove the list

  } else if ((packet->type & LOST_P) == LOST_P) {
    Log.verbose(F("Lost Packet received" CR));
    sendPacketSequence(packet->dst, cPacket->seq_id, cPacket->number);

  } else if ((packet->type & SYNC_P) == SYNC_P) {
    Log.verbose(F("Syncronization Packet received" CR));
    //TODO: Create a new instane inside the Q_WRP
    //TODO: THEN SEND ACK

  } else if ((packet->type & XL_DATA_P) == XL_DATA_P) {
    Log.verbose(F("Large payload Packet received" CR));
    //TODO: Add the packet inside the Q_WRP
    //TODO: If all the packet are being received, send lastack, rejoin all the payloads, and send it to the user.

  }

  //Need ack
  if ((packet->type & NEED_ACK_P) == NEED_ACK_P) {
    Log.verbose(F("Previous packet need an ACK" CR));
    sendAckPacket(packet->src, cPacket->seq_id, cPacket->number);
  }
}

void LoraMesher::notifyUserReceivedPacket(LoraMesher::packetQueue<dataPacket<uint8_t>>* pq) {
  //Add the packet inside the receivedUsers Queue
  ReceivedUserPackets->Add((packetQueue<uint8_t>*)pq);

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
  Log.verbose(F("HELLO packet from %X with size %d" CR), p->src, p->payloadSize);
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
          Log.verbose(F("Found better route for %X via %X metric %d" CR), node->address, via, node->metric);
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
      Log.verbose(F("New route added: %X via %X metric %d" CR), node->address, via, node->metric);
      break;
    }
  }
}

void LoraMesher::printRoutingTable() {
  Log.verbose(F("Current routing table:" CR));
  for (int i = 0; i < routingTableSize(); i++)
    Log.verbose(F("%d - %X via %X metric %d" CR), i,
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

template <typename T>
LoraMesher::packet<T>* LoraMesher::createPacket(T* payload, uint8_t payloadSize, size_t extraSize) {
  if (payloadSize + extraSize > MAXPAYLOADSIZE) {
    Log.error(F("Trying to create a packet with a payload greater than MAXPAYLOAD" CR));
    return nullptr;
  }

  //Packet length = size of the packet + size of the payload + extraSize before payload
  int packetLength = sizeof(packet<T>) + payloadSize + extraSize;

  packet<T>* p = (packet<T>*) malloc(packetLength);

  if (p && payloadSize > 0) {
    //Copy the payload into the packet
    //If has extraSize, we need to add the extraSize to point correctly to the p->payload
    memcpy((void*) ((unsigned long) p->payload + (extraSize)), payload, payloadSize);
  }

  p->payloadSize = payloadSize;

  Log.trace(F("Packet created with %d bytes" CR), packetLength);

  return p;
};

template <typename T>
LoraMesher::packet<T>* LoraMesher::createPacket(uint16_t dst, uint16_t src, uint8_t type, T* payload, uint8_t payloadSize) {
  size_t extraSize = getExtraLengthToPayload(type);

  packet<T>* p = createPacket(payload, payloadSize, extraSize);
  p->dst = dst;
  p->type = type;
  p->src = src;

  return p;
};

LoraMesher::packet<LoraMesher::networkNode>* LoraMesher::createRoutingPacket() {
  int routingSize = routingTableSize();
  size_t routingSizeInBytes = routingSize * sizeof(LoraMesher::networkNode);

  networkNode* payload = new networkNode[routingSize];

  for (int i = 0; i < routingSize; i++)
    payload[i] = routingTable[i].networkNode;


  packet<networkNode>* networkPacket = createPacket<LoraMesher::networkNode>(BROADCAST_ADDR, localAddress, HELLO_P, payload, routingSizeInBytes);
  delete[]payload;
  return networkPacket;
}


/**
 *  End Region Packet
**/


/**
 *  Region PacketQueue
**/
template <typename T, typename I>
LoraMesher::packetQueue<T>* LoraMesher::createPacketQueue(LoraMesher::packet<I>* p, uint8_t priority, uint16_t number) {
  packetQueue<T>* pq = new packetQueue<T>();
  pq->priority = priority;
  pq->packet = (packet<T>*) p;
  pq->next = nullptr;
  pq->number = number;
  return pq;
}

template <typename T>
void LoraMesher::deletepacketQueue(LoraMesher::packetQueue<T>* pq) {
  deletePacket(pq->packet);
  delete pq;
}

void LoraMesher::PacketQueue::Enable() {
  *enabled = true;
}

void LoraMesher::PacketQueue::WaitAndDisable() {
  while (!*enabled) {
    Log.verbose(F("Waiting for free queue" CR));
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

template <typename T>
LoraMesher::packetQueue<T>* LoraMesher::PacketQueue::Pop() {
  WaitAndDisable();

  if (first == nullptr) {
    Enable();
    return nullptr;
  }

  packetQueue<T>* firstCp = (packetQueue<T>*) first;
  first = first->next;

  Enable();
  return firstCp;
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
    deletepacketQueue(first);
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

void LoraMesher::sendStartSequencePackets(uint16_t destination, uint16_t seq_id, uint16_t num_packets) {
  uint16_t type = SYNC_P | NEED_ACK_P | XL_DATA_P;

  //Create the packet
  packet<uint8_t>* p = createPacket(destination, getLocalAddress(), type, (uint8_t*) 0, 0);

  //Add the sequence id and the number of packets of the sequence
  controlPacket<uint8_t>* cPacket = ((controlPacket<uint8_t>*) ((dataPacket<uint8_t>*) (p->payload))->payload);
  cPacket->seq_id = seq_id;
  cPacket->number = num_packets;

  //Add the packet to the send queue
  setPackedForSend(p, DEFAULT_PRIORITY);
}

void LoraMesher::sendAckPacket(uint16_t destination, uint16_t seq_id, uint16_t seq_num) {
  setPackedForSend(createPacket(destination, getLocalAddress(), ACK_P, (uint8_t*) 0, 0), DEFAULT_PRIORITY + 1);
}

void LoraMesher::sendPacketSequence(uint16_t destination, uint16_t seq_id, uint16_t seq_num) {
  //Get the listConfiguration with the sequence id
  listConfiguration* config = findSequenceList(q_WSP, seq_id);

  if (config == nullptr) {
    Log.error(F("NOT FOUND the sequence packet config with Id: %d" CR), seq_id);
    return;
  }

  //Get the packet queue with the sequence number
  packetQueue<uint8_t>* pq = findPacketQueue(config->list, seq_num);

  if (pq == nullptr) {
    Log.error(F("NOT FOUND the packet queue with Seq_id: %d, Num: %d" CR), seq_id, seq_num);
    return;
  }

  //Set the type
  uint16_t type = NEED_ACK_P | XL_DATA_P;

  //Create the packet
  packet<uint8_t>* p = createPacket(destination, getLocalAddress(), type, pq->packet->payload, pq->packet->payloadSize);

  //Add the packet to the send queue
  setPackedForSend(p, DEFAULT_PRIORITY);

}

void LoraMesher::clearLinkedList(listConfiguration* listConfig) {
  auto list = listConfig->list;
  list->setInUse();

  for (int i = 0; i < list->getLength(); i++) {
    deletepacketQueue(list->getCurrent());
    list->DeleteCurrent();
  }

  delete listConfig->list;
  delete listConfig->config;
  delete listConfig;
}

LoraMesher::listConfiguration* LoraMesher::findSequenceList(LinkedList<listConfiguration>* queue, uint8_t seq_id) {
  queue->setInUse();
  queue->moveToStart();

  for (int i = 0; i < queue->getLength(); i++) {
    auto current = queue->getCurrent();

    if (current->config->seq_id == seq_id) {
      queue->releaseInUse();
      return current;
    }

    if (!queue->next())
      break;
  }

  queue->releaseInUse();

  return nullptr;

}

LoraMesher::packetQueue<uint8_t>* LoraMesher::findPacketQueue(LinkedList<packetQueue<uint8_t>>* queue, uint8_t num) {
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
        Log.error(F("MAX TIMEOUTS reached from Waiting Received Queue, erasing Id: %d" CR), configPacket->seq_id);
        clearLinkedList(current);
        q_WRP->DeleteCurrent();
        continue;
      }

      configPacket->timeout = micros() + (DEFAULT_TIMEOUT * 1000) / portTICK_PERIOD_MS;

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

uint8_t LoraMesher::getSequenceId() {
  if (sequence_id == 255) {
    sequence_id = 0;
    return 255;
  }

  uint8_t id = sequence_id;
  sequence_id++;

  return id;
}

/**
 * End Large and Reliable payloads
 */
