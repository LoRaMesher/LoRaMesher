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
  
}

LoraMesher::~LoraMesher(){

}


void LoraMesher::poll(){
  onReceive(LoRa.parsePacket());
}


void LoraMesher::initializeLocalAddress () {
  byte WiFiMAC[6];

  WiFi.macAddress(WiFiMAC);
  localAddress = WiFiMAC[5];

  Log.notice(F("Local LoRa address (from WiFi MAC): %X" CR), localAddress);
}

void LoraMesher::initializeLoRa () {
  Log.trace(F("LoRa module initialization..." CR));

  // LoRa SPI pins initialization
  SPI.begin(SCK, MISO, MOSI, SS);

  // LoRa transceiver pins setup
  LoRa.setPins(SS, RST, DIO0);
  while (!LoRa.begin(BAND)) {
    Log.error(F("LoRa.begin(BAND) failed. Retrying..." CR));
    delay (1000);
  }

  //LoRa.setSpreadingFactor(LORASF);

  Log.trace(F("LoRa module initialization DONE" CR));


  delay (1000);
}


void LoraMesher::sendHelloPacket() {

  byte packetType = 0x04;

  Log.trace(F("Sending HELLO packet %d" CR), helloCounter);

  //Send LoRa packet to receiver
  LoRa.beginPacket();                   // start packet
  LoRa.write(broadcastAddress);         // add destination address
  LoRa.write(localAddress);             // add sender address
  LoRa.write(packetType);               // packet type
  LoRa.write(helloCounter);             // add message ID

  switch (metric) {
    case HOPCOUNT:
      for (int i = 0; i < routingTableSize(); i++) {
        LoRa.write(routingTable[i].address);
        LoRa.write(routingTable[i].metric);
      }
      break;
    case RSSISUM:
      break;
  }


  LoRa.endPacket();                     // finish packet and send it

  Log.trace("HELLO packet sended" CR);

  helloCounter++;
}


void LoraMesher::sendDataPacket() {

  byte packetType = 0x03;
  String outMessage = String("Data packet");

  Log.trace(F("Sending DATA packet %d" CR), dataCounter);

  //Send LoRa packet to receiver
  LoRa.beginPacket();                   // start packet
  LoRa.write(broadcastAddress);         // add destination address
  LoRa.write(localAddress);             // add sender address
  LoRa.write(packetType);             // add message ID
  LoRa.write(dataCounter);             // add message ID
  //LoRa.write(outMessage.length());    // add payload length
  LoRa.print(outMessage);               // add payload
  LoRa.endPacket();                     // finish packet and send it

  Log.trace("Data packet sended" CR);

  dataCounter++;
}


void LoraMesher::onReceive(int packetSize) {
  if (packetSize == 0)
    return;

  receivedPackets++;

  int rssi = LoRa.packetRssi();
  int snr = LoRa.packetSnr();

  Log.trace(F("Receiving LoRa packet %d: RSSI: %d SNR: %d" CR), receivedPackets, rssi, snr);

  // read packet header bytes:
  byte destination = LoRa.read();       // destination address
  byte sender = LoRa.read();            // sender address
  byte packetType = LoRa.read();        // packet type

  if (destination == broadcastAddress) {
    if (packetType == 0x04) {
      int helloseqnum = LoRa.read();

      Log.verbose(F("HELLO packet %d from %X" CR), helloseqnum, sender);

      switch (metric) {
        
        case HOPCOUNT:
          processRoute(localAddress, helloseqnum, rssi, snr, sender, 1);
          if (!isNodeInRoutingTable(sender)) {
            Log.verbose(F("Adding new neighbour %X to the routing table" CR), sender);
          }
          while (LoRa.available()) {
            byte addr = LoRa.read();
            int mtrc = LoRa.read();
            mtrc = mtrc + 1; // Add one hop to the received metric
            processRoute(sender, helloseqnum, rssi, snr, addr, mtrc);
          }
          printRoutingTable();
          break;

        case RSSISUM:
          break;
      }
    }
    else if (packetType == 0x03) {
      Log.verbose(F("Data broadcast message:" CR));
      String inMessage = "";
      while (LoRa.available()) {
        inMessage += (char)LoRa.read();
      }
      Log.verbose(F("PAYLOAD: %s" CR), inMessage);
    }
    else {
      Log.verbose(F("Random broadcast message... ignoring." CR));
    }
  }

  else if (destination == localAddress) {
    if ( packetType == 0x03 ) {
      Log.trace(F("Data packet from %X for me" CR), sender);
    }
    else if ( packetType == 0x04) {
      Log.trace(F("HELLO packet from %X for me" CR), sender);
    }
  }

  else {
    Log.verbose(F("Packet from %X for %X (not for me). IGNORING" CR), sender, destination);
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
