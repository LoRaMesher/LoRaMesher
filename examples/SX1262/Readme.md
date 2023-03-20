There is, in the source files of this first implementation, an example to test the new functionalities. This example is an implementation of a counter, sending a broadcast message every 10 seconds. To make it easier to understand, we will remove additional functions that are not necessary to make the microcontroller work with the LoRaMesher library.

### Defining the data type and the data counter

As a proof of concept, we will be sending a numeric counter over LoRa. Its value will be incremented every 10 seconds, then te packet will be transmitted. To start, we need to implement the type of data we will use.

In this case, we will only send a `uint32_t`, which is the counter itself.

```
uint32_t dataCounter = 0;
struct dataPacket {
  uint32_t counter = 0;
};

dataPacket* helloPacket = new dataPacket;
```

### LoRaMesh Initialization

To initialize the new implementation, you can configure the LoRa parameters that the library will use. If your node needs to receive messages to the application, see Received packets function section.

```
Serial.begin(115200);
Serial.println("initBoard");

//Get the LoraMesher instance
LoraMesher& radio = LoraMesher::getInstance();

//Initialize the LoraMesher. You can specify the LoRa parameters here or later with their respective functions
radio.begin();

//After initializing you need to start the radio with
radio.start();

//You can pause and resume at any moment with
radio.standby();
//And then
radio.start();
```

### Received packets function

If your node needs to receive packets from other nodes you should follow the next steps:

1. Create a function that will receive the packets:

The function that gets a notification each time the library receives a packet for the app looks like this one:

```
/**
 * @brief Function that process the received packets
 *
 */
void processReceivedPackets(void*) {
    for (;;) {
        /* Wait for the notification of processReceivedPackets and enter blocking */
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);

        //Iterate through all the packets inside the Received User Packets FiFo
        while (radio.getReceivedQueueSize() > 0) {
            Log.trace(F("ReceivedUserData_TaskHandle notify received" CR));
            Log.trace(F("Queue receiveUserData size: %d" CR), radio.getReceivedQueueSize());

            //Get the first element inside the Received User Packets FiFo
            AppPacket<dataPacket>* packet = radio.getNextAppPacket<dataPacket>();

            //Print the data packet
            printDataPacket(packet);

            //Delete the packet when used. It is very important to call this function to release the memory of the packet.
            radio.deletePacket(packet);

        }
    }
}
```

There are some important things we need to be aware of:

- This function should have a `void*` in the parameters.
- The function should contain an endless loop.
- Inside the loop, it is mandatory to have the `ulTaskNotifyTake(pdPASS,portMAX_DELAY)` or equivalent. This function allows the library to notify the function to process pending packets.
- All the packets are stored inside a private queue.
- There is a function to get the size of the queue `radio.getReceivedQueueSize()`.
- You can get the first element with `radio.getNextAppPacket<T>()` where T is the type of your data. 
- IMPORTANT!!! Every time you call Pop, you need to be sure to call `radio.deletePacket(packet)`. It will free the memory that has been allocated for the packet. If not executed it can cause memory leaks and out of memory errors.

2. Create a task containing this function:
```
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
        Log.errorln(F("Receive App Task creation gave error: %d"), res);
    }
}

```

2. Add the receiveLoRaMessage_Handle to the LoRaMesher

```
radio.setReceiveAppDataTaskHandle(receiveLoRaMessage_Handle);
```

### User data packet

In this section we will show you what there are inside a `AppPacket`.
```
class AppPacket {
    uint16_t dst; //Destination address, normally it will be local address or BROADCAST_ADDR
    uint16_t src; //Source address
    uint32_t payloadSize = 0; //Payload size in bytes
    T payload[]; //Payload

    size_t getPayloadLength() { return this->payloadSize / sizeof(T); }
};
```

Functionalities to use after getting the packet with `AppPacket<T>* packet = radio.getNextAppPacket<T>()`:
1. `packet->getPayloadLength()` it will get you the payload size in number of T
2. `radio.deletePacket(packet)` it will release the memory allocated for this packet.

### Send data packet function

In this section we will present how you can create and send packets. in this example we will use the `AppPacket` data structure.

```
  void loop() {
        helloPacket->counter = dataCounter++;

        //Create packet and send it.
         radio.createPacketAndSend(BROADCAST_ADDR, helloPacket, 1);
         
         //Or if you want to send large and reliable payloads you can call this function too.
         radio.sendReliable(dstAddr, helloPacket, 1);

        //Wait 10 seconds to send the next packet
        vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
```

In the previous figure we can see that we are using the helloPacket, we add the counter inside it, and we create and send the packet using the LoRaMesher.

The most important part of this piece of code is the function that we call in the `radio.createPacketAndSend()`:

1. The first parameter is the destination, in this case the broadcast address.
2. And finally, the helloPacket (the packet we created) and the number of elements we are sending, in this case only 1 dataPacket.

### Print packet example

When receiving the packet, we need to understand what the Queue will return us. For this reason, in the next subsection, we will explain how to implement a simple packet processing.

```
/**
 * @brief Print the counter of the packet
 *
 * @param data
 */
void printPacket(dataPacket data) {
  Log.verbose(F("Hello Counter received n %X" CR), data.counter);
}

/**
 * @brief Iterate through the payload of the packet and print the counter of the packet
 *
 * @param packet
 */
void printDataPacket(AppPacket<dataPacket>* packet) {
    //Get the payload to iterate through it
    dataPacket* dPacket = packet->payload;
    size_t payloadLength = packet->getPayloadLength();

    for (size_t i = 0; i < payloadLength; i++) {
        //Print the packet
        printPacket(dPacket[i]);
    }
}
```

1. After receiving the packet in the `processReceivedPackets()` function, we call the `printDataPacket()` function.
2. We need to get the payload of the packet using `packet->payload`.
3. We iterate through the `packet->getPayloadLength()`. This will let us know how big the payload is, in dataPackets types, for a given packet. In our case, we always send only one dataPacket.
4. Get the payload and call the `printPacket(dPacket[i])` function, that will print the counter received.