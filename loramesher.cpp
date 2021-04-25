#include "loramesher.h"


LoraMesher::LoraMesher(){
  Log.begin(LOG_LEVEL_VERBOSE, &Serial);
  dutyCycleEnd = 0;
  lastSendTime = 0;
  routeTimeout = 10000000;
  metric = HOPCOUNT;
  broadcastAddress = 0xFF;
  helloCounter = 0;
  receivedPackets = 0;
  dataCounter = 0;
  initializeLocalAddress();
  initializeLoRa();
  sendHelloPacket();
  Log.verbose(F("Initiaization DONE, starting receiving packets..." CR));
  int res = radio->startReceive();
  if (res != 0) Log.error(F("Receiving on constructor gave error: %d" CR), res);

  
}

LoraMesher::~LoraMesher(){

}


void LoraMesher::initializeLocalAddress () {
  byte WiFiMAC[6];

  WiFi.macAddress(WiFiMAC);
  localAddress = WiFiMAC[5];

  Log.notice(F("Local LoRa address (from WiFi MAC): %X" CR), localAddress);
}

void LoraMesher::initializeLoRa () {
  Log.trace(F("LoRa module initialization..." CR));

  // TODO: Optimize memory, this could lead to heap fragmentation
  Log.verbose(F("Initializing Radiolib" CR));
  Module *mod = new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_IO1);
  radio = new SX1276(mod);
  if (radio == NULL) 
  {
      Log.error(F("Radiolib not initialized properly" CR));
  }

  // Set up the radio parameters
  Log.verbose(F("Initializing radio" CR));
  int res = radio->begin(868.0);
  if (res != 0)
  {
    Log.error(F("Radio module gave error: %d" CR), res);
  }

  Log.verbose(F("Setting up callback function" CR));
  //attachInterrupt(LORA_IRQ, std::bind(&LoraMesher::onReceive, this), FALLING);
  radio->setDio0Action(std::bind(&LoraMesher::onReceive, this));
  
  Log.trace(F("LoRa module initialization DONE" CR));
  
  delay (1000);
}


void LoraMesher::sendHelloPacket() {

  Log.trace(F("Sending HELLO packet %d" CR), helloCounter);

  packet tx;
  tx.dst = broadcastAddress;
  tx.src = localAddress;
  tx.type = HELLO_P;
  tx.payload = helloCounter;
  tx.sizExtra = routingTableSize();

  for (size_t i = 0; i < routingTableSize(); i++)
  {
    tx.address[i] = routingTable[i].address;
    tx.metric[i] = routingTable[i].metric;
    
  }
  
  int res = radio->transmit((uint8_t *)&tx, sizeof(tx));
  if (res != 0)
  {
    Log.error(F("Transmit hello gave error: %d" CR), res);
  }
  else{
    Log.trace("HELLO packet sended" CR);
  }
  helloCounter++;
}


void LoraMesher::sendDataPacket() {

  Log.trace(F("Sending DATA packet %d" CR), dataCounter);

  packet tx;
  tx.dst = broadcastAddress;
  tx.src = localAddress;
  tx.type = DATA_P;
  tx.payload = dataCounter;

  int res = radio->transmit((uint8_t *)&tx, sizeof(tx));
  if (res != 0)
  {
    Log.error(F("Transmit data gave error: %d" CR), res);
  }
  else{
    Log.trace("Data packet sended" CR);
  }

  dataCounter++;
  res = radio->startReceive();
  if (res != 0) Log.error(F("Starting listening after sending datapacket gave ERROR: %d" CR), res);
}

// TODO: This routine can not be so large as it's the ISR
void IRAM_ATTR LoraMesher::onReceive() {
  
  int packetSize = radio->getPacketLength();
  if (packetSize == 0){
    Log.warning(F("Empty packet received" CR));
    return;
  }

  receivedPackets++;

  //TODO: This two variables should be float, not int. We need to redefine all the functions that use this variables
  int rssi = radio->getRSSI();
  int snr = radio->getSNR();
  
  Log.trace(F("Receiving LoRa packet %d: Size: %d RSSI: %d SNR: %d" CR), receivedPackets, packetSize ,rssi, snr);
  packet rx;
  int res = radio->readData((uint8_t*)&rx, sizeof(rx));
  if (res != 0){
    Log.error(F("Reading packet data gave error: %d" CR), res);
  }
  if (rx.dst == broadcastAddress) {
    if (rx.type == HELLO_P) {
      int helloseqnum = rx.payload;

      Log.verbose(F("HELLO packet %d from %X" CR), helloseqnum, rx.src);

      switch (metric) {
        
        case HOPCOUNT:
          processRoute(localAddress, helloseqnum, rssi, snr, rx.src, 1);
          if (!isNodeInRoutingTable(rx.src)) {
            Log.verbose(F("Adding new neighbour %X to the routing table" CR), rx.src);
          }

          for (size_t i = 0; i < rx.sizExtra; i++) {
            processRoute(rx.src, helloseqnum, rssi, snr, rx.address[i], rx.metric[i]+1);
          }

          printRoutingTable();
          break;

        case RSSISUM:
          break;
      }
    }
    else if (rx.type == DATA_P) {
      Log.verbose(F("Data broadcast message:" CR));
      Log.verbose(F("PAYLOAD: %X" CR), rx.payload);
    }
    else {
      Log.verbose(F("Random broadcast message... ignoring." CR));
    }
  }

  else if (rx.dst == localAddress) {
    if ( rx.payload == DATA_P) {
      Log.trace(F("Data packet from %X for me" CR), rx.src);
    }
    else if ( rx.payload == HELLO_P) {
      Log.trace(F("HELLO packet from %X for me" CR), rx.src);
    }
  }

  else {
    Log.verbose(F("Packet from %X for %X (not for me). IGNORING" CR), rx.src, rx.dst);
  }
}

bool LoraMesher::isNodeInRoutingTable(byte address) {
  for (int i = 0; i < RTMAXSIZE; i++) {
    if ( routingTable[i].address == address) {
      return true;
    }
  }
  return false;
}

void LoraMesher::addNeighborToRoutingTable(byte neighborAddress, int helloID) {

  for (int i = 0; i < RTMAXSIZE; i++) {
    if ( routingTable[i].address == 0) {
      routingTable[i].address = neighborAddress;
      routingTable[i].metric = 1;
      routingTable[i].lastSeqNo = helloID;
      routingTable[i].timeout = micros() + routeTimeout;
      routingTable[i].via = localAddress;
      Log.verbose(F("New neighbor added in position %d" CR), i);
      break;
    }
  }
}

int LoraMesher::knownNodes() {
  int known = 0;
  for (int i = 0; i < RTMAXSIZE; i++) {
    if ( routingTable[i].address != 0) {
      known++;
    }
  }
  return known;
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
    if ( transmissionEnd < transmissionStart ) {
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


// TODO: Run this routine frequently
void LoraMesher::HelloCallback() {
  Log.verbose(F("HELLO callback at t=%l milis" CR), millis());

  if (dutyCycleEnd < micros()) {
    unsigned long transmissionStart = micros();

    sendHelloPacket();

    unsigned long transmissionEnd = micros();

    unsigned long timeToNextPacket = 0;

    // Avoid micros() rollover
    if ( transmissionEnd < transmissionStart ) {
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

void LoraMesher::processRoute(byte sender, int helloseqnum, int rssi, int snr, byte addr, int mtrc) {

  bool knownAddr = false;

  switch (metric) {
    case HOPCOUNT:
      if (addr != localAddress) {
        for (int i = 0; i < routingTableSize(); i++) {
          if ( addr == routingTable[i].address) {
            knownAddr = true;
            if ( mtrc < routingTable[i].metric) {
              routingTable[i].metric = mtrc;
              routingTable[i].via = sender;
            }
            break;
          }
        }
        if (!knownAddr) {
          for (int i = 0; i < RTMAXSIZE; i++) {
            if ( routingTable[i].address == 0) {
              routingTable[i].address = addr;
              routingTable[i].metric = mtrc;
              routingTable[i].lastSeqNo = helloseqnum;
              routingTable[i].timeout = micros() + routeTimeout;
              routingTable[i].via = sender;
              Log.verbose(F("New route added: %X via %X metric %d" CR), addr, sender, mtrc);
              break;
            }
          }
        }
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
