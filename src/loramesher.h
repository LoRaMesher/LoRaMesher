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
#define BAND 868.0
#define LORASF 7 // Spreading factor 6-12 (default 7)

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

struct routableNode {
  uint16_t address = 0;
  uint8_t metric = 0;
  int lastSeqNo = 0;
  unsigned long timeout = 0;
  uint16_t via = 0;
};

// Metric
#define HOPCOUNT 0
#define RSSISUM 1

// Packet types
#define HELLO_P 0x04
#define DATA_P 0x03

class LoraMesher {

private:
  // Packet definition (BETA)
#pragma pack(1)
  struct packet {
    uint16_t dst;
    uint16_t src;
    uint8_t type;
    uint8_t sizExtra = 0;
    uint16_t address[20];
    uint8_t metric[20];
    uint8_t payloadSize = 0;
    uint32_t payload[];
  };

  routableNode routingTable[RTMAXSIZE];
  const size_t MAXPAYLOAD = MAXPAYLOADSIZE / sizeof(LoraMesher::packet::payload[0]);
  uint16_t localAddress;
  // LoRa packets counter
  int helloCounter;
  int receivedPackets;
  int dataCounter;
  // Duty cycle end
  unsigned long dutyCycleEnd;
  // Time for last HELLO packet
  unsigned long lastSendTime;
  // Routable node timeout (µs)
  unsigned long routeTimeout;
  // LoRa broadcast address
  uint16_t broadcastAddress;
  uint8_t metricType;

  SX1276* radio;

  TaskHandle_t Hello_TaskHandle;
  TaskHandle_t ReceivePacket_TaskHandle;

  void initializeLocalAddress();

  void initializeLoRa();

  void initializeNetwork();

  void sendHelloPacket();

  bool isNodeInRoutingTable(byte address);

  void AddNodeToRoutingTable(uint16_t neighborAddress, int helloID, uint8_t metric, uint16_t via);

  void DataCallback();

  void HelloCallback();

  void ProcessRoute(uint16_t sender, int helloseqnum, int rssi, int snr, uint16_t addr, uint8_t mtrc);

  /**
   * @brief Creates a LoraMesher::packet with a specified payload
   *
   * @param payload Array of the payload you want to sent
   * @param payloadLength length of the payload
   * @return struct LoraMesher::packet*
   */
  struct packet* CreatePacket(uint32_t payload[], uint8_t payloadLength);

  /**
   * @brief Get all the packet Length, including payload and headers
   *
   * @param p packet reference that you want to know the length of it
   * @return size_t
   */
  size_t GetPacketLength(packet* p);

  /**
 * @brief Prints the packet into the Log Verbose
 *
 * @param p The packet you want to print
 * @param received If true is that you received the code, false is that you are sending this packet
 * Used to differentiate between received and sended packets.
 */
  void PrintPacket(packet* p, bool received);

public:
  LoraMesher();
  ~LoraMesher();

  void sendDataPacket();
  void printRoutingTable();
  int routingTableSize();
  void onReceive();
  void receivingRoutine();
  uint8_t getLocalAddress();
};

#endif