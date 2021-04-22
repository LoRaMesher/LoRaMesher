#ifndef __LORAMESHER__
#define __LORAMESHER__

// LoRa libraries
//#include <SPI.h>
#include <LoRa.h>

// WiFi libraries
#include <WiFi.h>

// Logger
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
#define BAND 866E6
#define LORASF 7 // Spreading factor 6-12 (default 7)

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


struct routableNode
{
  byte address = 0;
  int metric;
  int lastSeqNo;
  unsigned long timeout;
  byte via;
};

// Metric
#define HOPCOUNT 0
#define RSSISUM 1



class LoraMesher{

    private:

        routableNode routingTable[RTMAXSIZE];

        byte localAddress;
        // LoRa packets counter
        int helloCounter;
        int receivedPackets;
        int dataCounter;
        // Duty cycle end
        unsigned long dutyCycleEnd;
        // Time for last HELLO packet
        unsigned long lastSendTime ;
        // Routable node timeout (µs)
        unsigned long routeTimeout;
        // LoRa broadcast address
        byte broadcastAddress;
        int metric;

        void initializeLocalAddress();
        void initializeLoRa();
        void sendHelloPacket();
        void onReceive(int packetSize);
        bool isNodeInRoutingTable(byte address);
        void addNeighborToRoutingTable(byte neighborAddress, int helloID);
        int knownNodes();
        void DataCallback();
        void HelloCallback();
        void processRoute(byte sender, int helloseqnum, int rssi, int snr, byte addr, int mtrc);

    public:

        LoraMesher();
        ~LoraMesher();

        void sendDataPacket();
        void printRoutingTable();
        int routingTableSize();

        void poll();



};

#endif