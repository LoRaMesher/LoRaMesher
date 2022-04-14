#include "loramesher.cpp"
#include "display.h"
#include "WString.h"

#define BOARD_LED 4

LoraMesher& radio = LoraMesher::getInstance();

uint32_t dataCounter = 0;
struct dataPacket {
  uint32_t counter[35] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34};
};

dataPacket* helloPacket = new dataPacket;

/**
 * @brief Flash the lead
 *
 * @param flashes number of flashes
 * @param delaymS delay between is on and off of the LED
 */
void led_Flash(uint16_t flashes, uint16_t delaymS) {
  uint16_t index;
  for (index = 1; index <= flashes; index++) {
    digitalWrite(BOARD_LED, HIGH);
    vTaskDelay(delaymS / portTICK_PERIOD_MS);
    digitalWrite(BOARD_LED, LOW);
    vTaskDelay(delaymS / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Print the counter of the packet
 *
 * @param data
 */
void printPacket(dataPacket* data, uint16_t sourceAddress) {
  char text[32];
  snprintf(text, 32, ("%X-> %d" CR), sourceAddress, data->counter[0]);

  Screen.changeLineThree(String(text));
  Log.verbose(F("Received data nº %d" CR), data->counter[0]);
}

/**
 * @brief Iterate through the payload of the packet and print the counter of the packet
 *
 * @param packet
 */
void printDataPacket(LoraMesher::packet<dataPacket>* packet) {
  Log.trace(F("Packet arrived from %X with size %d bytes" CR), packet->src, packet->payloadSize);

  //Get the payload to iterate through it
  dataPacket* packets = radio.getPayload(packet);

  printPacket(&packets[0], packet->src);

  Log.trace(F("---- Payload ---- " CR));
  for (int i = 0; i < 35; i++)
    Log.trace(F("%d, "), packets[0].counter[i]);
  Log.trace(F("---- Payload Done ---- " CR));
}

/**
 * @brief Function that process the received packets
 *
 */
void processReceivedPackets(void*) {
  for (;;) {
    /* Wait for the notification of processReceivedPackets and enter blocking */
    ulTaskNotifyTake(pdPASS, portMAX_DELAY);
    led_Flash(1, 100); //one quick LED flashes to indicate a packet has arrived

    //Iterate through all the packets inside the Received User Packets FiFo
    while (radio.ReceivedUserPackets->Size() > 0) {
      Log.trace(F("ReceivedUserData_TaskHandle notify received" CR));
      Log.trace(F("Queue receiveUserData size: %d" CR), radio.ReceivedUserPackets->Size());

      //Get the first element inside the Received User Packets FiFo
      LoraMesher::packetQueue<dataPacket>* helloReceived = radio.ReceivedUserPackets->Pop<dataPacket>();

      //Print the data packet
      printDataPacket(helloReceived->packet);

      //Delete the packet when used. It is very important to call this function to delete the packet queue and the packet.
      radio.deletepacketQueue(helloReceived);

    }
  }
}

/**
 * @brief Initialize the LoRa Mesher
 *
 */
void setupLoraMesher() {
  //Create a loramesher with a processReceivedPackets function
  radio.init(processReceivedPackets);

  Log.verbose("Lora initialized" CR);
}

/**
 * @brief Displays the address in the first line
 *
 */
void printAddressDisplay() {
  char addrStr[15];
  snprintf(addrStr, 15, "Id: %X\r\n", radio.getLocalAddress());

  Screen.changeLineOne(String(addrStr));
}


void printRoutingTableToDisplay() {
  char text[15];
  for (int i = 0; i < radio.routingTableSize(); i++) {
    LoraMesher::networkNode node = radio.routingTable[i].networkNode;
    snprintf(text, 15, ("|%X(%d)->%X"), node.address, node.metric, radio.routingTable[i].via);
    Screen.changeRoutingText(text, i);
  }

  Screen.changeSizeRouting(radio.routingTableSize());
  Screen.changeLineFour();
}


/**
 * @brief Every 20 seconds it will send a counter to a position of the dataTable
 *
 */
void sendLoRaMessage(void*) {
  int dataTablePosition = 0;

  for (;;) {
    if (radio.routingTableSize() == 0) {
      vTaskDelay(20000 / portTICK_PERIOD_MS);
      continue;
    }

    if (radio.routingTableSize() <= dataTablePosition)
      dataTablePosition = 0;

    uint16_t addr = radio.routingTable[dataTablePosition].networkNode.address;

    Log.trace(F("Send data packet nº %d to %X (%d)" CR), dataCounter, addr, dataTablePosition);

    dataTablePosition++;

    //Create packet and send it reliable
    radio.sendReliable(addr, helloPacket, 1);

    //Print second line in the screen
    Screen.changeLineTwo("Send " + String(dataCounter));

    //Increment data counter
    helloPacket->counter[0] = dataCounter++;

    //Print routing Table to Display
    printRoutingTableToDisplay();

    //Wait 20 seconds to send the next packet
    vTaskDelay(20000 / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Setup the Task to create and send periodical messages
 *
 */
void createSendMessages() {
  TaskHandle_t sendLoRaMessage_Handle = NULL;
  BaseType_t res = xTaskCreate(
    sendLoRaMessage,
    "Send LoRa Message routine",
    4098,
    (void*) 1,
    1,
    &sendLoRaMessage_Handle);
  if (res != pdPASS) {
    Log.error(F("Send LoRa Message task creation gave error: %d" CR), res);
    vTaskDelete(sendLoRaMessage_Handle);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BOARD_LED, OUTPUT); //setup pin as output for indicator LED

  Screen.initDisplay();
  Log.verbose("Board Init" CR);


  led_Flash(2, 125);          //two quick LED flashes to indicate program start
  setupLoraMesher();
  printAddressDisplay();
  createSendMessages();
}

void loop() {
  vTaskPrioritySet(NULL, 1);
  Screen.drawDisplay();
}