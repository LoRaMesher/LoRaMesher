#include "loramesher.h"

LoraMesher::LoraMesher() {
  Log.begin(LOG_LEVEL_VERBOSE, &Serial);
  dutyCycleEnd = 0;
  lastSendTime = 0;
  routeTimeout = 10000000;
  initializeLocalAddress();
  initializeLoRa();
  initializeNetwork();
  delay(1000);
  Log.verbose(F("Initialization DONE, starting receiving packets..." CR));
  int res = radio->startReceive();
  if (res != 0)
    Log.error(F("Receiving on constructor gave error: %d" CR), res);
}

LoraMesher::~LoraMesher() {
  vTaskDelete(Hello_TaskHandle);
  vTaskDelete(ReceivePacket_TaskHandle);
  radio->clearDio0Action();
  radio->reset();
}

void LoraMesher::initializeNetwork() {
  int res = xTaskCreate(
    [](void* o) { static_cast<LoraMesher*>(o)->sendHelloPacket(); },
    "Hello routine",
    4096,
    this,
    0,
    &Hello_TaskHandle);
  if (res != pdPASS) {
    Log.error(F("Hello Task creation gave error: %d" CR), res);
  }
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

  Log.verbose(F("Setting up receiving task" CR));
  res = xTaskCreate(
    [](void* o) { static_cast<LoraMesher*>(o)->receivingRoutine(); },
    "Receiving routine",
    4096,
    this,
    0,
    &ReceivePacket_TaskHandle);
  if (res != pdPASS) {
    Log.error(F("Hello Task creation gave error: %d" CR), res);
  }

  Log.verbose(F("Setting up callback function" CR));
  radio->setDio0Action(std::bind(&LoraMesher::onReceive, this));

  Log.trace(F("LoRa module initialization DONE" CR));

  delay(1000);
}

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
    Log.trace("Packet sended" CR);
  }

  radio->setDio0Action(std::bind(&LoraMesher::onReceive, this));
  res = radio->startReceive();
  if (res != 0)
    Log.error(F("Receiving on end of packet transmission gave error: %d" CR), res);
}

template <typename T>
void LoraMesher::deletePacket(LoraMesher::packet<T>* p) {
  // free(p->payload);
  free(p);
}

void LoraMesher::sendHelloPacket() {
  for (;;) {
    packet<networkNode>* tx = createRoutingPacket();
    sendPacket(tx);
    deletePacket(tx);

    //TODO: Change this to vTaskDelayUntil to prevent sending too many packets as this is not considering normal data packets sent for legal purposes
    //Set random task delay between sending data.
    //This will prevent that two microcontrollers sending data at the same time every time and one of them not received for the other ones.
    uint32_t randomTime = esp_random() % 10000;
    vTaskDelay(((randomTime + 30000) / portTICK_PERIOD_MS));
  }
}

// void LoraMesher::sendDataPacket() {

  // Log.trace(F("Sending DATA packet %d" CR), dataCounter);
  // radio->clearDio0Action(); //For some reason, while transmitting packets, the interrupt pin is set with a ghost packet

  // uint8_t counter[30];
  // counter[0] = dataCounter;
  // for (int i = 1; i < 30; i++)
  //   counter[i] = i;

  // packet* tx = CreatePacket(counter, 30);
  // tx->dst = broadcastAddress;
  // tx->src = localAddress;
  // tx->type = DATA_P;

  // int res = radio->transmit((uint8_t*) tx, getPacketLength(tx));
  // free(tx);

  // if (res != 0) {
  //   Log.error(F("Transmit data gave error: %d" CR), res);
  // } else {
  //   Log.trace("Data packet sended" CR);
  // }

  // dataCounter++;

  // radio->setDio0Action(std::bind(&LoraMesher::onReceive, this));
  // res = radio->startReceive();
  // if (res != 0)
  //   Log.error(F("Starting listening after sending datapacket gave ERROR: %d" CR), res);
// }

void IRAM_ATTR LoraMesher::onReceive() {

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  xTaskNotifyFromISR(
    ReceivePacket_TaskHandle,
    0,
    eSetValueWithoutOverwrite,
    &xHigherPriorityTaskWoken);

  if (xHigherPriorityTaskWoken == pdTRUE)
    portYIELD_FROM_ISR();
}

void LoraMesher::receivingRoutine() {
  BaseType_t TWres;
  int packetSize;
  int rssi, snr, res;
  for (;;) {
    TWres = xTaskNotifyWait(
      pdFALSE,
      ULONG_MAX,
      NULL,
      portMAX_DELAY // The most amount of time possible
    );

    if (TWres == pdPASS) {
      packetSize = radio->getPacketLength();
      if (packetSize == 0)
        Log.warning(F("Empty packet received" CR));

      else {
        uint8_t payload[MAXPAYLOADSIZE];
        packet<byte>* rx = createPacket<byte>(payload, MAXPAYLOADSIZE);

        rssi = radio->getRSSI();
        snr = radio->getSNR();

        Log.notice(F("Receiving LoRa packet: Size: %d RSSI: %d SNR: %d" CR), packetSize, rssi, snr);
        res = radio->readData((uint8_t*) rx, packetSize);
        if (res != 0) {
          Log.error(F("Reading packet data gave error: %d" CR), res);
        }
        else {
          switch (rx->type) {
            case HELLO_P:
              processRoute((packet<networkNode>*) rx);
              break;

            case DATA_P:
              if (rx->dst == localAddress) {
                Log.trace(F("Data packet from %X for me" CR), rx->src);
              }
              else if (rx->dst == BROADCAST_ADDR) {
                Log.verbose(F("Data broadcast message:" CR));
                Log.verbose(F("PAYLOAD: %X" CR), rx->payload[0]);
              }
              else {
                Log.verbose(F("Data Packet from %X for %X (not for me). IGNORING" CR), rx->src, rx->dst);
              }
              break;

            default:
              Log.verbose(F("Packet not identified" CR));
          }

          deletePacket(rx);
        }

        Log.verbose(F("Starting to listen again after receiving a packet" CR));
        res = radio->startReceive();
        if (res != 0) {
          Log.error(F("Receiving on end of listener gave error: %d" CR), res);
        }
      }
    }
  }
}

uint8_t LoraMesher::getLocalAddress() {
  return this->localAddress;
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

// This function should be erased as it's the user the one deciding when to send data.
// void LoraMesher::dataCallback() {
  // Log.verbose(F("DATA callback at t=%l ms" CR), millis());

  // if (dutyCycleEnd < millis()) {
  //   unsigned long transmissionStart = micros();

  //   sendDataPacket();

  //   unsigned long transmissionEnd = micros();

  //   unsigned long timeToNextPacket = 0;

  //   // Avoid micros() rollover
  //   if (transmissionEnd < transmissionStart) {
  //     timeToNextPacket = 99 * (timeToNextPacket - 1 - transmissionStart + transmissionEnd);
  //   }
  //   // Default behaviour
  //   else {
  //     timeToNextPacket = 99 * (micros() - transmissionStart);
  //   }

  //   dutyCycleEnd = millis() + timeToNextPacket / 1000 + 1;

  //   Log.verbose(F("Scheduling next DATA packet in %d ms" CR), 2 * timeToNextPacket / 1000);

  //   //HelloTask.setInterval(2 * (timeToNextPacket) / 1000);
  // }
// }

int LoraMesher::routingTableSize() {
  size_t sum = 0;
  for (int i = 0; i < RTMAXSIZE; i++)
    sum += (routingTable[i].networkNode.address != 0);
  return sum;
}

void LoraMesher::processRoute(uint16_t via, LoraMesher::networkNode* node) {
  bool knownAddr = false;

  if (node->address != localAddress) {
    for (int i = 0; i < routingTableSize(); i++) {
      if (routingTable[i].networkNode.address != 0 && node->address == routingTable[i].networkNode.address) {
        knownAddr = true;
        if (node->metric < routingTable[i].networkNode.metric) {
          routingTable[i].networkNode.metric = node->metric;
          routingTable[i].via = via;
        }
        break;
      }
    }
    if (!knownAddr)
      addNodeToRoutingTable(node, via);
  }
}

void LoraMesher::processRoute(LoraMesher::packet<networkNode>* p) {
  Log.verbose(F("HELLO packet from %X with size %d" CR), p->src, p->payloadSize);
  printPacket(p, true);

  LoraMesher::networkNode* receivedNode = new networkNode();
  receivedNode->address = p->src;
  receivedNode->metric = 1;
  processRoute(localAddress, receivedNode);
  delete receivedNode;

  Log.verbose(F("HELLO packet from %X with size %d" CR), p->src, p->payloadSize);

  for (size_t i = 0; i < p->payloadSize; i++) {
    LoraMesher::networkNode* node = &p->payload[i];
    node->metric++;
    // Log.verbose(F("Before: %d + -> Address: %X - Metric: %d" CR), i, node->address, node->metric);
    processRoute(p->src, node);
  }

  printRoutingTable();
}


void LoraMesher::printRoutingTable() {
  Serial.println("Current routing table:");
  for (int i = 0; i < routingTableSize(); i++) {
    Serial.print(routingTable[i].networkNode.address, HEX);
    Serial.print(" via ");
    Serial.print(routingTable[i].via, HEX);
    Serial.print(" metric ");
    Serial.println(routingTable[i].networkNode.metric);
  }
  Serial.println("");
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
      for (int i = 0; i < p->payloadSize; i++)
        Log.verbose(F("%d ->(%u) Address: %X - Metric: %d" CR), i, &p->payload[i], ((networkNode*) &p->payload[i])->address, ((networkNode*) &p->payload[i])->metric);

  }

  Log.verbose(F("-----------------------------------------\n"));
}

template <typename T>
size_t LoraMesher::getPacketLength(LoraMesher::packet<T>* p) {
  return sizeof(LoraMesher::packet<T>) + sizeof(T) * p->payloadSize;
}

template <typename T>
LoraMesher::packet<T>* LoraMesher::createPacket(T* payload, uint8_t payloadSize) {
  int payloadLength = payloadSize * sizeof(T);
  int packetLength = sizeof(packet<T>) + payloadLength;
  packet<T>* p = (packet<T>*) malloc(packetLength);
  Log.trace(F("Packet created with %d bytes" CR), packetLength);

  if (p) {
    memcpy(p->payload, payload, payloadLength);
    p->payloadSize = payloadSize;
  }
  return p;
};

template <typename T>
LoraMesher::packet<T>* LoraMesher::createPacket(uint16_t dst, uint16_t src, uint8_t type, T* payload, uint8_t payloadSize) {
  packet<T>* p = createPacket(payload, payloadSize);
  p->dst = dst;
  p->type = type;
  p->src = src;

  return p;
};

LoraMesher::packet<LoraMesher::networkNode>* LoraMesher::createRoutingPacket() {
  int routingSize = routingTableSize();
  Log.trace(F("Network node" CR));


  networkNode* payload = new networkNode[routingSize];

  for (int i = 0; i < routingSize; i++)
    payload[i] = routingTable[i].networkNode;

  packet<networkNode>* networkPacket = createPacket<LoraMesher::networkNode>(BROADCAST_ADDR, localAddress, HELLO_P, payload, routingSize);
  delete[]payload;
  return networkPacket;
}