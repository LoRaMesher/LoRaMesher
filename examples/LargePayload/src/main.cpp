#include <Arduino.h>
#include "LoraMesher.h"
#include "display.h"

//Using LILYGO TTGO T-BEAM v1.1 
#define BOARD_LED   4
#define LED_ON      LOW
#define LED_OFF     HIGH

#define DATA_NUM 6

LoraMesher& radio = LoraMesher::getInstance();

uint32_t dataCounter = 0;
struct dataPacket {
    uint32_t counter[35] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34};
};

dataPacket helloPackets[DATA_NUM];

/**
 * @brief Flash the lead
 *
 * @param flashes number of flashes
 * @param delaymS delay between is on and off of the LED
 */
void led_Flash(uint16_t flashes, uint16_t delaymS) {
    uint16_t index;
    for (index = 1; index <= flashes; index++) {
        digitalWrite(BOARD_LED, LED_OFF);
        vTaskDelay(delaymS / portTICK_PERIOD_MS);
        digitalWrite(BOARD_LED, LED_ON);
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
    snprintf(text, 32, ("%X-> %d\n"), sourceAddress, data->counter[0]);

    Screen.changeLineThree(String(text));
    Serial.printf("Received data nº %d\n", data->counter[0]);
}

/**
 * @brief Iterate through the payload of the packet and print the counter of the packet
 *
 * @param packet
 */
void printDataPacket(AppPacket<dataPacket>* packet) {
    Serial.printf("Packet arrived from %X with size %d bytes\n", packet->src, packet->payloadSize);

    //Get the payload to iterate through it
    dataPacket* dPacket = packet->payload;
    size_t payloadLength = packet->getPayloadLength();

    printPacket(&dPacket[0], packet->src);

    Serial.printf("---- Payload ---- Payload length in dataP: %d \n", payloadLength);

    for (size_t i = 0; i < payloadLength; i++) {
        Serial.printf("Received data nº %d", i);
        Serial.printf("%d -- ", i);

        for (size_t j = 0; j < 35; j++) {
            Serial.printf("%d, ", dPacket[i].counter[j]);

        }
        Serial.println();
    }

    Serial.println("---- Payload Done ---- ");
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
        while (radio.getReceivedQueueSize() > 0) {
            Serial.println("ReceivedUserData_TaskHandle notify received");
            Serial.printf("Queue receiveUserData size: %d\n", radio.getReceivedQueueSize());

            //Get the first element inside the Received User Packets Queue
            AppPacket<dataPacket>* packet = radio.getNextAppPacket<dataPacket>();

            //Print the data packet
            printDataPacket(packet);

            //Delete the packet when used. It is very important to call this function to release the memory of the packet.
            radio.deletePacket(packet);
        }
    }
}

TaskHandle_t receiveLoRaMessage_Handle = NULL;

/**
 * @brief Create a Receive Messages Task and add it to the LoRaMesher
 *
 */
void createReceiveMessages() {
    int res = xTaskCreate(
        processReceivedPackets,
        "Receive App Task",
        4096,
        (void*) 1,
        2,
        &receiveLoRaMessage_Handle);
    if (res != pdPASS) {
        Serial.printf("Error: Receive App Task creation gave error: %d\n", res);
    }
}

/**
 * @brief Initialize LoRaMesher
 *
 */
void setupLoraMesher() {
    //Get the configuration of the LoRaMesher
    LoraMesher::LoraMesherConfig config = LoraMesher::LoraMesherConfig();

    //Set the configuration of the LoRaMesher (TTGO T-BEAM v1.1)
    config.loraCs = 18;
    config.loraRst = 23;
    config.loraIrq = 26;
    config.loraIo1 = 33;

    config.module = LoraMesher::LoraModules::SX1276_MOD;

    //Init the loramesher with a configuration
    radio.begin(config);

    //Create the receive task and add it to the LoRaMesher
    createReceiveMessages();

    //Set the task handle to the LoRaMesher
    radio.setReceiveAppDataTaskHandle(receiveLoRaMessage_Handle);

    //Start LoRaMesher
    radio.start();

    Serial.println("Lora initialized");
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

/**
 * @brief Print the routing table into the display
 *
 */
void printRoutingTableToDisplay() {

    //Set the routing table list that is being used and cannot be accessed (Remember to release use after usage)
    LM_LinkedList<RouteNode>* routingTableList = radio.routingTableListCopy();

    routingTableList->setInUse();

    Screen.changeSizeRouting(radio.routingTableSize());

    char text[15];
    for (int i = 0; i < radio.routingTableSize(); i++) {
        RouteNode* rNode = (*routingTableList)[i];
        NetworkNode node = rNode->networkNode;
        snprintf(text, 15, ("|%X(%d)->%X"), node.address, node.metric, rNode->via);
        Screen.changeRoutingText(text, i);
    }

    //Release routing table list usage.
    routingTableList->releaseInUse();

    //Delete the routing table list
    delete routingTableList;

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

        LM_LinkedList<RouteNode>* routingTableList = radio.routingTableListCopy();

        uint16_t addr = (*routingTableList)[dataTablePosition]->networkNode.address;

        Serial.printf("Send data packet nº %d to %X (%d)\n", dataCounter, addr, dataTablePosition);

        dataTablePosition++;

        //Send packet reliable
        radio.sendReliable(addr, helloPackets, DATA_NUM);

        //Print second line in the screen
        Screen.changeLineTwo("Send " + String(dataCounter));

        //Increment the dataCounter
        dataCounter++;

        //Increment data counter of all the packets
        // for (int i = 0; i < DATA_NUM; i++)
        //     helloPackets[i].counter[0] = dataCounter;

        //Print routing Table to Display
        printRoutingTableToDisplay();

        //Release routing table list usage.
        delete routingTableList;

        break;

        //Wait 120 seconds to send the next packet
        vTaskDelay(120000 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
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
        Serial.printf("Error: Send LoRa Message task creation gave error: %d\n", res);
        vTaskDelete(sendLoRaMessage_Handle);
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(BOARD_LED, OUTPUT); //setup pin as output for indicator LED

    Screen.initDisplay();

    Serial.println("Board Init");

    led_Flash(2, 125);          //two quick LED flashes to indicate program start
    setupLoraMesher();
    printAddressDisplay();

    createSendMessages();
}

void loop() {
    vTaskPrioritySet(NULL, 1);
    Screen.drawDisplay();
}