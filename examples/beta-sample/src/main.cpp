#include <Arduino.h>
#include "loramesher.cpp"
using namespace NLoraMesher;

#define BOARD_LED 4

LoraMesher* radio;

uint32_t dataCounter = 0;
struct dataPacket {
  uint32_t counter = 0;
};

dataPacket* helloPacket = new dataPacket;

//Led flash
void led_Flash(uint16_t flashes, uint16_t delaymS) {
  uint16_t index;
  for (index = 1; index <= flashes; index++) {
    digitalWrite(BOARD_LED, HIGH);
    delay(delaymS);
    digitalWrite(BOARD_LED, LOW);
    delay(delaymS);
  }
}

static void printPacket(dataPacket* data) {
  Log.verbose(F("Hello Counter received nº %X" CR), data->counter);
}

static void printDataPacket(LoraMesher::packet<dataPacket>* packet) {
  Log.trace(F("Packet arrived from %X with size %d" CR), packet->src, packet->payloadSize);
  for (size_t i = 0; i < radio->getPayloadLength(packet); i++) {
    printPacket(&packet->payload[i]);
  }
}

static void processReceivedPackets(void* parameters) {
  for (;;) {
    /* Wait for the notification of processReceivedPackets and enter blocking */
    ulTaskNotifyTake(pdPASS, portMAX_DELAY);
    led_Flash(1, 100); //one quick LED flashes to indicate a packet has arrived
    while (radio->ReceivedUserPackets->Size() > 0) {
      Log.trace(F("ReceivedUserData_TaskHandle notify received" CR));
      Log.trace(F("Fifo receiveUserData size: %d" CR), radio->ReceivedUserPackets->Size());

      LoraMesher::packetQueue<dataPacket>* helloReceived = radio->ReceivedUserPackets->Pop<dataPacket>();
      printDataPacket(helloReceived->packet);
      delete helloReceived;
    }
  }
}
TaskHandle_t SendData_TaskHandle = NULL;

void setupLoraMesher() {
  radio = new LoraMesher(processReceivedPackets);
  Serial.println("Lora initialized");
}

void setup() {
  Serial.begin(115200);

  Serial.println("initBoard");
  pinMode(BOARD_LED, OUTPUT); //setup pin as output for indicator LED
  led_Flash(2, 125);          //two quick LED flashes to indicate program start
  setupLoraMesher();
}


void loop() {
  for (;;) {
    Log.trace(F("Send packet %d" CR), dataCounter);
    helloPacket->counter = dataCounter++;
    radio->createPacketAndSend(BROADCAST_ADDR, radio->getLocalAddress(), DATA_P, helloPacket, 1);

    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}