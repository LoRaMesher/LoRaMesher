#include "loramesher.h"

LoraMesher::LoraMesher() {
  Log.begin(LOG_LEVEL_VERBOSE, &Serial);
  dutyCycleEnd = 0;
  lastSendTime = 0;
  routeTimeout = 10000000;
  metric = HOPCOUNT;
  broadcastAddress = 0xFFFF;
  helloCounter = 0;
  receivedPackets = 0;
  dataCounter = 0;
  initializeLocalAddress();
  initializeLoRa();
  initializeNetwork();
  delay(1000);
  Log.verbose(F("Initiaization DONE, starting receiving packets..." CR));
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

void LoraMesher::sendHelloPacket() {
  for (;;) {
    Log.trace(F("Sending HELLO packet %d" CR), helloCounter);
    radio->clearDio0Action(); //For some reason, while transmitting packets, the interrupt pin is set with a ghost packet
    uint32_t counter[1];
    counter[0] = helloCounter;

    packet* tx = CreatePacket(counter, 1);
    tx->dst = broadcastAddress;
    tx->src = localAddress;
    tx->type = HELLO_P;
    tx->sizExtra = routingTableSize();

    for (size_t i = 0; i < routingTableSize(); i++) {
      tx->address[i] = routingTable[i].address;
      tx->metric[i] = routingTable[i].metric;
    }
    Log.trace(F("About to transmit HELLO packet" CR));
    //TODO: Change this to startTransmit as a mitigation to the wdt error so we can raise the priority of the task. We'll have to look on how to start to listen for the radio again
    int res = radio->transmit((uint8_t*) tx, GetPacketLength(tx));
    delete(tx);

    if (res != 0) {
      Log.error(F("Transmit hello gave error: %d" CR), res);
    }
    else {
      Log.trace("HELLO packet sended" CR);
    }
    helloCounter++;

    radio->setDio0Action(std::bind(&LoraMesher::onReceive, this));
    res = radio->startReceive();
    if (res != 0)
      Log.error(F("Receiving on end of HELLO packet transmision gave error: %d" CR), res);
    //TODO: Change this to vTaskDelayUntil to prevent sending too many packets as this is not considering normal data packets sent for legal purposes
    vTaskDelay(30000 / portTICK_PERIOD_MS);
  }
}

void LoraMesher::sendDataPacket() {

  Log.trace(F("Sending DATA packet %d" CR), dataCounter);
  radio->clearDio0Action(); //For some reason, while transmitting packets, the interrupt pin is set with a ghost packet

  uint32_t counter[30];
  counter[0] = dataCounter;
  for (int i = 1; i < 30; i++)
    counter[i] = i;

  packet* tx = CreatePacket(counter, 30);
  tx->dst = broadcastAddress;
  tx->src = localAddress;
  tx->type = DATA_P;

  int res = radio->transmit((uint8_t*) tx, GetPacketLength(tx));
  delete(tx);

  if (res != 0) {
    Log.error(F("Transmit data gave error: %d" CR), res);
  }
  else {
    Log.trace("Data packet sended" CR);
  }

  dataCounter++;

  radio->setDio0Action(std::bind(&LoraMesher::onReceive, this));
  res = radio->startReceive();
  if (res != 0)
    Log.error(F("Starting listening after sending datapacket gave ERROR: %d" CR), res);
}

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
  int rssi, snr, res, helloseqnum;
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
        uint32_t payload[MAXPAYLOAD];
        packet* rx = CreatePacket(payload, MAXPAYLOAD);
        receivedPackets++;

        rssi = radio->getRSSI();
        snr = radio->getSNR();

        Log.notice(F("Receiving LoRa packet %d: Size: %d RSSI: %d SNR: %d" CR), receivedPackets, packetSize, rssi, snr);
        res = radio->readData((uint8_t*) rx, packetSize);
        if (res != 0) {
          Log.error(F("Reading packet data gave error: %d" CR), res);
        }
        if (rx->dst == broadcastAddress) {
          PrintPacket(rx, true);
          if (rx->type == HELLO_P) {
            helloseqnum = rx->payload[0];

            Log.verbose(F("HELLO packet %d from %X" CR), helloseqnum, rx->src);

            switch (metric) {

              case HOPCOUNT:
                ProcessRoute(localAddress, helloseqnum, rssi, snr, rx->src, 1);

                for (size_t i = 0; i < rx->sizExtra; i++)
                  ProcessRoute(rx->src, helloseqnum, rssi, snr, rx->address[i], rx->metric[i] + 1);

                printRoutingTable();
                break;

              case RSSISUM:
                break;
            }
          }
          else if (rx->type == DATA_P) {
            Log.verbose(F("Data broadcast message:" CR));
            Log.verbose(F("PAYLOAD: %X" CR), rx->payload[0]);
          }
          else {
            Log.verbose(F("Random broadcast message... ignoring." CR));
          }
        }

        else if (rx->dst == localAddress) {
          if (rx->type == DATA_P) {
            Log.trace(F("Data packet from %X for me" CR), rx->src);
          }
          else if (rx->type == HELLO_P) {
            Log.trace(F("HELLO packet from %X for me" CR), rx->src);
          }
        }

        else {
          Log.verbose(F("Packet from %X for %X (not for me). IGNORING" CR), rx->src, rx->dst);
        }
        Log.verbose(F("Starting to listen again after receiving a packet" CR));
        res = radio->startReceive();
        if (res != 0)
          Log.error(F("Receiving on end of listener gave error: %d" CR), res);

        delete(rx);
      }
    }
  }
}

bool LoraMesher::isNodeInRoutingTable(byte address) {
  for (int i = 0; i < RTMAXSIZE; i++) {
    if (routingTable[i].address == address) {
      return true;
    }
  }
  return false;
}

uint8_t LoraMesher::getLocalAddress() {
  return this->localAddress;
}

void LoraMesher::AddNodeToRoutingTable(uint16_t address, int helloID, uint8_t metric, uint16_t via) {
  for (int i = 0; i < RTMAXSIZE; i++) {
    if (routingTable[i].address == 0) {
      routingTable[i].address = address;
      routingTable[i].metric = metric;
      routingTable[i].lastSeqNo = helloID;
      routingTable[i].timeout = micros() + routeTimeout;
      routingTable[i].via = via;
      Log.verbose(F("New route added: %X via %X metric %d" CR), address, via, metric);
      break;
    }
  }
}

// This function should be erased as it's the user the one deciding when to send data.
void LoraMesher::DataCallback() {
  Log.verbose(F("DATA callback at t=%l milis" CR), millis());

  if (dutyCycleEnd < millis()) {
    unsigned long transmissionStart = micros();

    sendDataPacket();

    unsigned long transmissionEnd = micros();

    unsigned long timeToNextPacket = 0;

    // Avoid micros() rollover
    if (transmissionEnd < transmissionStart) {
      timeToNextPacket = 99 * (timeToNextPacket - 1 - transmissionStart + transmissionEnd);
    }
    // Default behaviour
    else {
      timeToNextPacket = 99 * (micros() - transmissionStart);
    }

    dutyCycleEnd = millis() + timeToNextPacket / 1000 + 1;

    Log.verbose(F("Scheduling next DATA packet in %d ms" CR), 2 * timeToNextPacket / 1000);

    //HelloTask.setInterval(2 * (timeToNextPacket) / 1000);
  }
}

int LoraMesher::routingTableSize() {
  int size = 0;

  for (int i = 0; i < RTMAXSIZE; i++) {
    if (routingTable[i].address != 0) {
      size++;
    }
  }
  return size;
}

void LoraMesher::ProcessRoute(uint16_t sender, int helloseqnum, int rssi, int snr, uint16_t addr, uint8_t mtrc) {

  bool knownAddr = false;

  switch (metric) {
    case HOPCOUNT:
      if (addr != localAddress) {
        for (int i = 0; i < routingTableSize(); i++) {
          if (routingTable[i].address != 0 && addr == routingTable[i].address) {
            knownAddr = true;
            if (mtrc < routingTable[i].metric) {
              routingTable[i].metric = mtrc;
              routingTable[i].via = sender;
            }
            break;
          }
        }
        if (!knownAddr)
          AddNodeToRoutingTable(addr, helloseqnum, mtrc, sender);
      }
      break;
    case RSSISUM:
      break;
  }
  return;
}

void LoraMesher::printRoutingTable() {
  Serial.println("Current routing table:");
  for (int i = 0; i < routingTableSize(); i++) {
    Serial.print(routingTable[i].address, HEX);
    Serial.print(" via ");
    Serial.print(routingTable[i].via, HEX);
    Serial.print(" metric ");
    Serial.println(routingTable[i].metric);
  }
  Serial.println("");
}

void LoraMesher::PrintPacket(packet* p, bool received) {
  Log.verbose(F("-----------------------------------------\n"));
  Log.verbose(F("Current Packet: %s\n"), received ? "Received" : "Sended");
  Log.verbose(F("Destinatary: %X\n"), p->dst);
  Log.verbose(F("Source: %X\n"), p->src);
  Log.verbose(F("Type: %d\n"), p->type);
  Log.verbose(F("----Routing table from packet: %d of size----\n"), p->sizExtra);
  for (int i = 0; i < ((p->sizExtra < 20) ? p->sizExtra : 20); i++) {
    Log.verbose(F("-- Address: %X, "), p->address[i]);
    Log.verbose(F("via: %X, "), p->src);
    Log.verbose(F("Metric: %d -- "), p->metric[i]);
  }
  Log.verbose(F("\n"));
  Log.verbose(F("------- Payload Size: %d bytes ------\n"), p->payloadSize);
  for (int i = 0; i < (p->payloadSize < MAXPAYLOAD ? p->payloadSize : MAXPAYLOAD); i++) //GetPacketLength(p)
    Log.verbose(F("%d - %d --- "), i, p->payload[i]);
  Log.verbose(F("\n"));
  Log.verbose(F("-----------------------------------------\n"));
}

size_t LoraMesher::GetPacketLength(packet* p) {
  return sizeof(LoraMesher::packet) + sizeof(LoraMesher::packet::payload[0]) * p->payloadSize;
}

struct LoraMesher::packet* LoraMesher::CreatePacket(uint32_t payload[], uint8_t payloadLength) {
  int packetLength = sizeof(LoraMesher::packet) + payloadLength * sizeof(packet::payload[0]);
  packet* p = (packet*) malloc(packetLength);
  Log.trace("Packet created with %d bytes." CR, packetLength);

  if (p) {
    memcpy(p->payload, payload, payloadLength * sizeof(packet::payload[0]));
    p->payloadSize = payloadLength;

    //Default values --- TODO: this should be automatically done?
    p->type = 3;
    p->sizExtra = 0;
  }

  return p;
}
