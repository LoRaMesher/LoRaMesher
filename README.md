# LoRaMesher

## See the [GitHub Pages](https://jaimi5.github.io/LoRaMesher) for more information

## Introduction

The LoRaMesher library implements a distance-vector routing protocol for communicating messages among LoRa nodes. For the interaction with the LoRa radio chip, we leverage RadioLib, a versatile communication library which supports different LoRa series modules.

### Compatibility
At this moment, LoRaMesher has been tested within the following modules:
- SX1262
- SX1268
- SX1276
- SX1278
- SX1280

You can request another module to be added to the library by opening an issue.

## Dependencies

You can check `library.json` for more details. Basically, we use [Radiolib](https://github.com/jgromes/RadioLib) that implements the low level communication to the different LoRa modules and [FreeRTOS](https://freertos.org/index.html) for scheduling maintenance tasks.

## Configure LoRaMesher with PlatformIO and Visual Studio Code

1. Download Visual Studio Code.
2. Download PlatformIO inside Visual Studio Code.
3. Clone the LoraMesher repository.
4. Go to PlatformIO Home, click on the Projects button, then on "Add Existing", and find the examples/beta-sample source in the files.
5. Select the examples/beta-example project.
6. Build the project with PlatformIO.
7. Upload the project to the specified LoRa microcontroller. In our case, we use the TTGO T-Beam module.

## LoRaMesher Example

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

#### Default Configuration

To initialize the new implementation, you can configure the LoRa parameters that the library will use. If your node needs to receive messages to the application, see Received packets function section.

You can configure different parameters for LoRa configuration. 
Using the ```LoRaMesherConfig``` you can configure the following parameters (Mandatory*):
- LoRaCS*
- LoRaIRQ*
- LoRaRST*
- LoRaI01*
- LoRa Module (See Compatibility)*
- Frequency
- Band
- Spreading Factor
- Synchronization Word
- Power
- Preamble Length
- SPI Class.

Here is an example on how to configure LoRaMesher using this configuration:

```
//Get the LoraMesher instance
LoraMesher& radio = LoraMesher::getInstance();

//Get the default configuration
LoraMesher::LoraMesherConfig config = LoraMesher::LoraMesherConfig();

//Change some parameters to the configuration
//(TTGO T-Beam v1.1 pins)
config.loraCS = 18
config.loraRst = 23
config.loraIrq = 26
config.loraIo1 = 33

config.module = LoraMesher::LoraModules::SX1276_MOD;

//Initialize the LoraMesher. You can specify the LoRa parameters here or later with their respective functions
radio.begin(config);

//After initializing you need to start the radio with
radio.start();

//You can pause and resume at any moment with
radio.standby();
//And then
radio.start();
```

*Be aware of the local laws that apply to radio frequencies*

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
            Serial.println("ReceivedUserData_TaskHandle notify received");
            Serial.printf("Queue receiveUserData size: %d\n", radio.getReceivedQueueSize());

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
        Serial.printf("Error: Receive App Task creation gave error: %d\n", res);
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
  Serial.printf("Hello Counter received n %d\n", data.counter);
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

## More information on the design and evaluation of LoRaMesher
Please see our open access paper ["Implementation of a LoRa Mesh Library"](https://ieeexplore.ieee.org/document/9930341) for a detailed description. If you use the LoRaMesher library, in academic work, please cite the following:
```
@ARTICLE{9930341,
  author={Solé, Joan Miquel and Centelles, Roger Pueyo and Freitag, Felix and Meseguer, Roc},
  journal={IEEE Access}, 
  title={Implementation of a LoRa Mesh Library}, 
  year={2022},
  volume={10},
  number={},
  pages={113158-113171},
  doi={10.1109/ACCESS.2022.3217215}}
```

