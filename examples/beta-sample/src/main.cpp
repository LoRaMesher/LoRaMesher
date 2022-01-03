#include <Arduino.h>
#include "loramesher.cpp"

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

/**
 * @brief Print the counter of the packet
 *
 * @param data
 */
void printPacket(dataPacket* data) {
    Log.verbose(F("Hello Counter received nº %X" CR), data->counter);
}

/**
 * @brief Iterate through the payload of the packet and print the counter of the packet
 *
 * @param packet
 */
void printDataPacket(LoraMesher::packet<dataPacket>* packet) {
    Log.trace(F("Packet arrived from %X with size %d" CR), packet->src, packet->payloadSize);
    dataPacket* packets = radio->getPayload(packet);
    for (size_t i = 0; i < radio->getPayloadLength(packet); i++) {
        //Print the packet
        printPacket(&packets[i]);
    }
    //Delete the packet. It is very important to delete the packet.
    delete packet;
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
        while (radio->ReceivedUserPackets->Size() > 0) {
            Log.trace(F("ReceivedUserData_TaskHandle notify received" CR));
            Log.trace(F("Fifo receiveUserData size: %d" CR), radio->ReceivedUserPackets->Size());

            //Get the first element inside the Received User Packets FiFo
            LoraMesher::packetQueue<dataPacket>* helloReceived = radio->ReceivedUserPackets->Pop<dataPacket>();

            //Print the data packet
            printDataPacket(helloReceived->packet);

            //Delete the packet when used. It is very important to delete the packets.
            delete helloReceived;
        }
    }
}

void setupLoraMesher() {

    //Create a loramesher with a processReceivedPackets function
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

        //Create packet and send it.
        radio->createPacketAndSend(0xC5FC, helloPacket, 1);

        //Wait 10 seconds to send the next packet
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}