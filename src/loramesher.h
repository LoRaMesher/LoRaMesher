#ifndef __LORAMESHER__
#define __LORAMESHER__

// LoRa libraries
#include "RadioLib.h"

// WiFi libraries
#include <WiFi.h>

// Logger
//#define DISABLE_LOGGING
#include <ArduinoLog.h>


// LoRa transceiver module pins
#define SCK 5
#define MISO 19
#define MOSI 27
#define SS 18
#define RST 14
#define DIO0 26

// LoRa band definition
// 433E6 for Asia
// 866E6 for Europe
// 915E6 for North America
#define BAND 868.0F
#define BANDWIDTH 125.0F
#define LORASF 7U // Spreading factor 6-12 (default 7)

// Comment this line if you want to remove the reliable payload
#define RELIABLE_PAYLOAD

// SSD1306 OLED display pins
// For TTGO T3_V1.6: SDA/SCL/RST 21/22/16
// For Heltec ESP32: SDA/SCL/RST 4/15/16

#ifdef ARDUINO_TTGO_LoRa32_V1
#define HASOLEDSSD1306 true
#define HASGPS false
#define GPS_SDA 0
#define GPS_SCL 0
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RST 16
#endif

#ifdef ARDUINO_HELTEC_WIFI_LORA_32
#define HASOLEDSSD1306 true
#define HASGPS false
#define GPS_SDA 0
#define GPS_SCL 0
#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 16
#endif

#ifdef ARDUINO_TTGO_T1
#define HASOLEDSSD1306 true
#define HASGPS true
#define GPS_SDA 21
#define GPS_SCL 22
#define OLED_RST 0
#define OLED_SDA 21
#define OLED_SCL 22
#endif

// Routing table max size
#define RTMAXSIZE 256
#define MAXPAYLOADSIZE 200 //In bytes

// Packet types
#define HELLO_P 0x04
#define DATA_P 0x03
#define PAYLOADONLY_P 0x02

// Packet configuration
#define BROADCAST_ADDR 0xFFFF
#define DEFAULT_PRIORITY 20

//Time out by default 60s
#define DEFAULT_TIMEOUT 60

class LoraMesher {

public:
  /**
   * @brief Construct a new Lora Mesher object
   *
   * @param receiverFunction Receiver function. It will be notified when data for the user is habailable.
   * Example of usage:
    void processReceivedPackets(void* parameters) {
     for (;;) {
        //Wait for the notification of processReceivedPackets and enter blocking
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);

        // Get the receivedUserPackets and get all the elements
        while (radio->ReceivedUserPackets->Size() > 0) {
          LoraMesher::packetQueue<dataPacket>* packetReceived = radio->ReceivedUserPackets->Pop<dataPacket>();

          //Do something with the packet, ex: print(packetReceived);

        }
      }
    }

    Then initialize:
    LoraMesher radio = new LoraMesher(processReceivedPackets);
   */
  LoraMesher(void (*receiverFunction)(void*));

  /**
   * @brief Destroy the Lora Mesher
   *
   */
  ~LoraMesher();

#pragma pack(push, 1)
  struct networkNode {
    uint16_t address = 0;
    uint8_t metric = 0;
  };

  struct routableNode {
    LoraMesher::networkNode networkNode;
    unsigned long timeout = 0;
    uint16_t via = 0;
  };
#pragma pack(pop)

  /**
   * @brief Routing table array
   *
   */
  routableNode routingTable[RTMAXSIZE];

  /**
   * @brief Prints the actual routing table in the log
   *
   */
  void printRoutingTable();

  /**
   * @brief Returns the routing table size
   *
   * @return int
   */
  int routingTableSize();

  /**
   * @brief Returns if address is inside the routing table
   *
   * @param address Addres you want to check if is inside the routing table
   * @return true If the address is inside the routing table
   * @return false If the addres is not inside the routing table
   */
  bool hasAddresRoutingTable(uint16_t address);

  /**
   * @brief Get the Next Hop address
   *
   * @param dst address of the next hop
   * @return uint16_t address of the next hop
   */
  uint16_t getNextHop(uint16_t dst);

  /**
   * @brief Get the Local Address
   *
   * @return uint16_t Address
   */
  uint16_t getLocalAddress();



#pragma pack(push, 1)
  template <typename T>
  struct packet {
    uint16_t dst;
    uint16_t src;
    uint8_t type;
    uint8_t payloadSize = 0;
    T payload[];
  };

  template <typename T>
  struct dataPacket {
    uint16_t via;
    T payload[];
  };

#pragma pack(pop)

  /**
   * @brief Create a Packet And Send it
   *
   * @tparam T
   * @param dst Destination
   * @param src Source, normally local addres, use getLocalAddress()
   * @param type Type of packet
   * @param payload Payload of type T
   * @param payloadSize Length of the payload in T
   */
  template <typename T>
  void createPacketAndSend(uint16_t dst, T* payload, uint8_t payloadSize);

  /**
   * @brief Get the Payload of the packet
   *
   * @tparam T type of the payload
   * @param packet Packet that you want to get the payload
   * @return T* pointer of the packet payload
   */
  template <typename T>
  T* getPayload(packet<T>* packet);

  /**
   * @brief Create a Packet<T>
   *
   * @tparam T
   * @param dst Destination
   * @param src Source, normally local addres, use getLocalAddress()
   * @param type Type of packet
   * @param payload Payload of type T
   * @param payloadSize Length of the payload in T
   * @return struct LoraMesher::packet<T>*
   */
  template <typename T>
  struct LoraMesher::packet<T>* createPacket(uint16_t dst, uint16_t src, uint8_t type, T* payload, uint8_t payloadSize);

  /**
   * @brief Create a Packet<T>
   *
   * @tparam T
   * @param payload Payload of type T
   * @param payloadSize Length of the payload in T
   * @param hasVia Indicates the function that it need to add the via
   * @return struct LoraMesher::packet<T>*
   */
  template <typename T>
  struct LoraMesher::packet<T>* createPacket(T* payload, uint8_t payloadSize, bool hasVia);

  /**
   * @brief Delete the packet from memory
   *
   * @tparam T Type of packet
   * @param p Packet to delete
   */
  template <typename T>
  void deletePacket(LoraMesher::packet<T>* p);

  /**
   * @brief Sets the packet in a Fifo with priority and will send the packet when needed.
   *
   * @tparam T
   * @param p packet of type T
   * @param priority Priority set DEFAULT_PRIORITY by default. 0 most priority
   * @param timeout Packet timeout, DEFAULT_TIMEOUT
   */
  template <typename T>
  void setPackedForSend(LoraMesher::packet<T>* p, uint8_t priority, uint32_t timeout);

  /**
   * @brief Get the payload in number of elements
   *
   * @tparam T
   * @param p
   * @return size_t
   */
  template<typename T>
  size_t getPayloadLength(LoraMesher::packet<T>* p);

  /**
   * @brief packetQueue template
   *
   * @tparam T
   */
#pragma pack(push, 1)
  template <typename T>
  struct packetQueue {
    uint32_t timeout;
    uint8_t priority = DEFAULT_PRIORITY;
    LoraMesher::packet<T>* packet;
    packetQueue<T>* next;
  };
#pragma pack(pop)

  /**
   * @brief Create a Packet Queue element
   *
   * @tparam T type of the packet queue
   * @tparam I type of the packet
   * @param p packet
   * @param priority priority inside the queue
   * @param timeout timeout in s
   * @return packetQueue<T>*
   */
  template <typename T, typename I>
  packetQueue<T>* createPacketQueue(packet<I>* p, uint8_t priority, uint32_t timeout);

  class PacketQueue {
    packetQueue<uint32_t>* first = nullptr;

  public:
    /**
     * @brief Add pq in order of priority
     *
     * @param pq
     */
    void Add(packetQueue<uint32_t>* pq);

    /**
     * @brief Get the first packetQueue of the list and deletes it from the list
     *
     * @tparam T
     * @return packetQueue<T>*
     */
    template <typename T>
    packetQueue<T>* Pop();

    /**
     * @brief Returns the size of the queue
     *
     * @return size_t
     */
    size_t Size();

    /**
     * @brief Clear the packetQueueFifo, deleting all the packets inside too.
     *
     */
    void Clear();
  };

  LoraMesher::PacketQueue* ReceivedUserPackets = new PacketQueue();

private:

  PacketQueue* ReceivedPackets = new PacketQueue();
  PacketQueue* ToSendPackets = new PacketQueue();

  //Local Address
  uint16_t localAddress;
  // Duty cycle end
  unsigned long dutyCycleEnd;
  // Time for last HELLO packet
  unsigned long lastSendTime;
  // Routable node timeout (µs)
  unsigned long routeTimeout;

  SX1276* radio;

  TaskHandle_t Hello_TaskHandle = NULL;
  TaskHandle_t ReceivePacket_TaskHandle = NULL;

  TaskHandle_t ReceiveData_TaskHandle = NULL;
  TaskHandle_t SendData_TaskHandle = NULL;

  TaskHandle_t ReceivedUserData_TaskHandle = NULL;


  void onReceive();

  void receivingRoutine();

  void initializeLocalAddress();

  void initializeLoRa();

  void initializeScheduler(void (*func)(void*));

  void sendHelloPacket();

  /**
   * @brief Function that process the packets inside Received Packets
   * Task executed every time that a packet arrive.
   *
   */
  void processPackets();

  // void dataCallback();

  // void helloCallback();

  /**
   * @brief process the network node, adds the node in the routing table if can
   *
   * @param via via address
   * @param node networkNode
   */
  void processRoute(uint16_t via, LoraMesher::networkNode* node);

  /**
   * @brief Process the network packet
   *
   * @param p Packet of type networkNode
   */
  void processRoute(LoraMesher::packet<networkNode>* p);

  /**
   * @brief Add node to the routing table
   *
   * @param node Network node that includes the address and the metric
   * @param via Address to next hop to reach the network node address
   */
  void addNodeToRoutingTable(LoraMesher::networkNode* node, uint16_t via);

  /**
   * @brief Create a Routing Packet adding the routing table to the payload
   *
   * @return struct LoraMesher::packet*
   */
  struct LoraMesher::packet<networkNode>* createRoutingPacket();

  /**
   * @brief Send a packet through Lora
   *
   * @tparam T Generic type
   * @param p Packet
   */
  template <typename T>
  void sendPacket(LoraMesher::packet<T>* p);

  /**
   * @brief Proccess that sends the data inside the FIFO
   *
   */
  void sendPackets();

  /**
   * @brief Get the Packet Length
   *
   * @tparam T
   * @param p Packet of Type T
   * @return size_t Packet size in bytes
   */
  template <typename T>
  size_t getPacketLength(LoraMesher::packet<T>* p);

  /**
   * @brief Print the packet in the Log verbose, if the type is not defined it will print the payload in Hexadecimals
   *
   * @tparam T
   * @param p Packet of Type T
   * @param received If true it will print received, else will print Created
   */
  template <typename T>
  void printPacket(LoraMesher::packet<T>* p, bool received);
};

#endif