#include "loramesher.h"

LoraMesher::LoraMesher(void (*func)(void*)) {
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
  int res = radio->begin(BAND);
  if (res != 0) {
    Log.error(F("Radio module gave error: %d" CR), res);
  }

#ifdef RELIABLE_PAYLOAD
  radio->setCRC(true);
#endif

  Log.verbose(F("Setting up callback function" CR));
  radio->setDio0Action(std::bind(&LoraMesher::onReceive, this));

  Log.trace(F("LoRa module initialization DONE" CR));

  delay(1000);
}

int helloCounter = 0;
void LoraMesher::initializeScheduler(void (*func)(void*)) {
  Log.verbose(F("Setting up receiving task" CR));
  int res = xTaskCreate(
    [](void* o) { static_cast<LoraMesher*>(o)->receivingRoutine(); },
    "Receiving routine",
    4096,
    this,
    4,
    &ReceivePacket_TaskHandle);
  if (res != pdPASS) {
    Log.error(F("Receiving routine creation gave error: %d" CR), res);
  }
  res = xTaskCreate(
    [](void* o) { static_cast<LoraMesher*>(o)->sendHelloPacket(); },
    "Hello routine",
    4096,
    this,
    2,
    &Hello_TaskHandle);
  if (res != pdPASS) {
    Log.error(F("Process Task creation gave error: %d" CR), res);
  }
  res = xTaskCreate(
    [](void* o) { static_cast<LoraMesher*>(o)->processPackets(); },
    "Process routine",
    4096,
    this,
    1,
    &ReceiveData_TaskHandle);
  if (res != pdPASS) {
    Log.error(F("Process Task creation gave error: %d" CR), res);
  }
  res = xTaskCreate(
    [](void* o) { static_cast<LoraMesher*>(o)->sendPackets(); },
    "Sending routine",
    4096,
    this,
    3,
    &SendData_TaskHandle);
  if (res != pdPASS) {
    Log.error(F("Sending Task creation gave error: %d" CR), res);
  }
  res = xTaskCreate(
    func,
    "Receive User routine",
    4096,
    this,
    0,
    &ReceivedUserData_TaskHandle);
  if (res != pdPASS) {
    Log.error(F("Receive User Task creation gave error: %d" CR), res);
  }
}

void IRAM_ATTR LoraMesher::onReceive() {

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  xHigherPriorityTaskWoken = xTaskNotifyFromISR(
    ReceivePacket_TaskHandle,
    0,
    eSetValueWithoutOverwrite,
    &xHigherPriorityTaskWoken);

  if (xHigherPriorityTaskWoken == pdTRUE)
    portYIELD_FROM_ISR();
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
        uint8_t payload[MAXPAYLOADSIZE];
        packet<byte>* rx = createPacket<byte>(payload, MAXPAYLOADSIZE, true);

        rssi = radio->getRSSI();
        snr = radio->getSNR();

        Log.notice(F("Receiving LoRa packet: Size: %d RSSI: %d SNR: %d" CR), packetSize, rssi, snr);
        res = radio->readData((uint8_t*) rx, packetSize);
        if (res != 0) {
          Log.error(F("Reading packet data gave error: %d" CR), res);
        }
        else {
          //Set the received flag to true
          receivedFlag = true;

          //Add the packet created into the ReceivedPackets List
          packetQueue<uint32_t>* pq = createPacketQueue<uint32_t>(rx, 15, DEFAULT_TIMEOUT);
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
  Log.trace(F("Sending packet of type %d" CR), p->type);
  radio->clearDio0Action(); //For some reason, while transmitting packets, the interrupt pin is set with a ghost packet

  Log.trace(F("About to transmit packet" CR));
  //TODO: Change this to startTransmit as a mitigation to the wdt error so we can raise the priority of the task. We'll have to look on how to start to listen for the radio again
  int res = radio->transmit((uint8_t*) p, getPacketLength(p));

  if (res != 0) {
    Log.error(F("Transmit gave error: %d" CR), res);
  }
  else {
    Log.trace("Packet sent" CR);
  }

  radio->setDio0Action(std::bind(&LoraMesher::onReceive, this));
  res = radio->startReceive();
  if (res != 0)
    Log.error(F("Receiving on end of packet transmission gave error: %d" CR), res);
}

void LoraMesher::sendPackets() {
  for (;;) {
    /* Wait for the notification of receivingRoutine and enter blocking */
    Log.verbose("Size of Send Packets Fifo: %d" CR, ToSendPackets->Size());
    while (ToSendPackets->Size() > 0) {

      packetQueue<uint8_t>* tx = ToSendPackets->Pop<uint8_t>();

      if (tx != nullptr) {
        Log.verbose("Send nº %d" CR, helloCounter);
        helloCounter++;

        //If the packet is type DATA_P add the via to the packet and forward the packet
        if (tx->packet->type == DATA_P) {
          ((LoraMesher::dataPacket<uint32_t>*) (&tx->packet->payload))->via = getNextHop(tx->packet->dst);
          Log.trace(F("NextHop %X" CR), getNextHop(tx->packet->dst));
          Log.trace(F("Sending data packet from %X, destiny %X, via %X" CR), tx->packet->src, tx->packet->dst, ((LoraMesher::dataPacket<uint32_t>*) (&tx->packet->payload))->via);
        }

        sendPacket(tx->packet);

        delete tx->packet;
        delete tx;
      }
    }

    //TODO: This should be regulated about what time we can send a packet, in order to accomplish the regulations 
    //Wait for 10s to send the next packet
    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
}

void LoraMesher::sendHelloPacket() {
  for (;;) {
    packet<networkNode>* tx = createRoutingPacket();
    setPackedForSend(tx, DEFAULT_PRIORITY + 1, DEFAULT_TIMEOUT);

    //Wait for 30s to send the next hello packet
    vTaskDelay(30000 / portTICK_PERIOD_MS);
  }
}

template <typename T>
void LoraMesher::setPackedForSend(packet<T>* tx, uint8_t priority, uint32_t timeout) {
  LoraMesher::packetQueue<uint32_t>* send = createPacketQueue<uint32_t>(tx, priority, timeout);
  ToSendPackets->Add(send);
  //TODO: Using vTaskDelay to kill the packet inside LoraMesher
}

void LoraMesher::processPackets() {
  BaseType_t TWres;
  bool userNotify = false;
  bool isDataPacket = false;

  for (;;) {
    /* Wait for the notification of receivingRoutine and enter blocking */
    ulTaskNotifyTake(pdPASS, portMAX_DELAY);
    userNotify = false;
    isDataPacket = false;

    Log.verbose("Size of Received Packets Fifo: %d" CR, ReceivedPackets->Size());
    packetQueue<uint32_t>* rx = ReceivedPackets->Pop<uint32_t>();
    //Important! The packet should be deleted or moved to another section

    if (rx != nullptr) {
      switch (rx->packet->type) {
        case HELLO_P:
          processRoute((packet<networkNode>*) rx->packet);
          break;

        case DATA_P:
          isDataPacket = true;
          Log.trace(F("Data packet from %X, destiny %X, via %X" CR), rx->packet->src, rx->packet->dst, ((LoraMesher::dataPacket<uint32_t>*) (&rx->packet->payload))->via);
        case PAYLOADONLY_P:
          if (rx->packet->dst == localAddress) {
            Log.trace(F("Data packet from %X for me" CR), rx->packet->src);
            userNotify = true;
          }
          else if (rx->packet->dst == BROADCAST_ADDR) {
            Log.trace(F("Data packet from %X BROADCAST" CR), rx->packet->src);
            userNotify = true;
          }
          //If the packet is a Datapacket and the via is me
          else if (isDataPacket && (((LoraMesher::dataPacket<uint32_t>*) (&rx->packet->payload))->via) == localAddress) {
            Log.verbose(F("Data Packet from %X for %X. Via is me" CR), rx->packet->src, rx->packet->dst);

            //If the destiny of the packet is inside the Routing table, forward the packet
            if (hasAddresRoutingTable(rx->packet->dst)) {
              Log.verbose(F("Data Packet forwarding it." CR));
              ToSendPackets->Add(rx);
            }
            //If the destiny is not inside the Routing table, delete the packet and ignore it
            else {
              Log.verbose(F("Data Packet destiny not reachable, deleting it." CR));
              delete rx->packet;
              delete rx;
            }
          }
          else {
            Log.verbose(F("Data Packet not for me, deleting it." CR));
            delete rx->packet;
            delete rx;
          }
          break;

        default:
          Log.verbose(F("Packet not identified, deleting it" CR));
          delete rx->packet;
          delete rx;
      }

      if (userNotify) {
        //Add packet to the received Packets
        ReceivedUserPackets->Add((packetQueue<uint32_t>*)rx);

        //Notify that there is a packets to be processed by the user
        TWres = xTaskNotifyFromISR(
          ReceivedUserData_TaskHandle,
          0,
          eSetValueWithoutOverwrite, &TWres);
      }
    }
  }
}

template <typename T>
void LoraMesher::createPacketAndSend(uint16_t dst, T* payload, uint8_t payloadSize) {
  uint8_t type = dst == BROADCAST_ADDR ? PAYLOADONLY_P : DATA_P;
  setPackedForSend(createPacket(dst, getLocalAddress(), type, payload, payloadSize), DEFAULT_PRIORITY, DEFAULT_TIMEOUT);
}

template <typename T>
size_t LoraMesher::getPacketLength(LoraMesher::packet<T>* p) {
  return sizeof(LoraMesher::packet<T>) + p->payloadSize + (p->type == DATA_P ? sizeof(LoraMesher::dataPacket<uint32_t>::via) : 0);
}

template <typename T>
T* LoraMesher::getPayload(packet<T>* packet) {
  if (packet->type == DATA_P)
    return (T*) (((LoraMesher::dataPacket<T>*) (&packet->payload))->payload);

  return (T*) packet->payload;
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
  deletePacket(p);
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
LoraMesher::packet<T>* LoraMesher::createPacket(T* payload, uint8_t payloadSize, bool hasVia) {
  int payloadLength = payloadSize * sizeof(T);

  int viaSize = (hasVia ? sizeof(LoraMesher::dataPacket<uint32_t>::via) : 0);

  //Packet length = size of the packet + size of the payload + if has via -> size of via
  int packetLength = sizeof(packet<T>) + payloadLength + viaSize;

  packet<T>* p = (packet<T>*) malloc(packetLength);

  if (p) {
    //Copy the payload into the packet
    //If hasVia, we need to add the sizeof via to point correctly to the p->payload
    memcpy((void*) ((unsigned long) p->payload + (viaSize)), payload, payloadLength);

    p->payloadSize = payloadLength;
  }

  Log.trace(F("Packet created with %d bytes" CR), packetLength);

  return p;
};

template <typename T>
LoraMesher::packet<T>* LoraMesher::createPacket(uint16_t dst, uint16_t src, uint8_t type, T* payload, uint8_t payloadSize) {
  bool hasVia = type == DATA_P;
  packet<T>* p = createPacket(payload, payloadSize, hasVia);
  p->dst = dst;
  p->type = type;
  p->src = src;

  return p;
};

LoraMesher::packet<LoraMesher::networkNode>* LoraMesher::createRoutingPacket() {
  int routingSize = routingTableSize();
  networkNode* payload = new networkNode[routingSize];

  for (int i = 0; i < routingSize; i++)
    payload[i] = routingTable[i].networkNode;

  packet<networkNode>* networkPacket = createPacket<LoraMesher::networkNode>(BROADCAST_ADDR, localAddress, HELLO_P, payload, routingSize);
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
LoraMesher::packetQueue<T>* LoraMesher::createPacketQueue(LoraMesher::packet<I>* p, uint8_t priority, uint32_t timeout) {
  packetQueue<T>* pq = new packetQueue<T>();
  pq->timeout = micros() + timeout * 1000;
  pq->priority = priority;
  pq->packet = (packet<T>*) p;
  pq->next = nullptr;
  return pq;
}

void LoraMesher::PacketQueue::Add(packetQueue<uint32_t>* pq) {
  if (first == nullptr)
    first = pq;

  else {
    packetQueue<uint32_t>* firstCp = first;
    if (firstCp->next == nullptr && first->priority < pq->priority) {
      first = pq;
      pq->next = firstCp;
      return;
    }

    while (firstCp->next != nullptr && firstCp->priority >= pq->priority)
      firstCp = firstCp->next;

    if (firstCp->next == nullptr)
      firstCp->next = pq;

    else {
      packetQueue<uint32_t>* cp = firstCp->next;
      firstCp->next = pq;
      pq->next = cp;
    }
  }
}

template <typename T>
LoraMesher::packetQueue<T>* LoraMesher::PacketQueue::Pop() {
  if (first == nullptr)
    return nullptr;

  packetQueue<T>* firstCp = (packetQueue<T>*) first;
  first = first->next;
  return firstCp;
}

size_t LoraMesher::PacketQueue::Size() {
  packetQueue<uint32_t>* firstCp = (packetQueue<uint32_t>*) first;
  if (firstCp == nullptr)
    return 0;

  size_t size = 1;
  while (firstCp->next != nullptr) {
    size++;
    firstCp = firstCp->next;
  }
  return size;
}

void LoraMesher::PacketQueue::Clear() {
  if (first == nullptr)
    return;

  packetQueue<uint32_t>* firstCp = (packetQueue<uint32_t>*) first;
  while (first != nullptr) {
    firstCp = first->next;
    delete first->packet;
    delete first;
    first = firstCp;
  }
}

/**
 *  End Region PacketQueue
**/
