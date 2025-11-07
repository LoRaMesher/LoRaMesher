#ifndef _LORAMESHER_BUILD_OPTIONS_H
#define _LORAMESHER_BUILD_OPTIONS_H

#ifdef ARDUINO
#include "Arduino.h"
#else
#include <freertos/FreeRTOS.h>
#include <string.h>
#include <string>
#include <cstdint>
#include <math.h>
#include <esp_log.h>
#include <esp_heap_caps.h>

using namespace std;

// adjust SPI pins as needed
#ifndef SPI_SCK
#define SPI_SCK 9
#endif

#ifndef SPI_MOSI
#define SPI_MOSI 10
#endif

#ifndef SPI_MISO
#define SPI_MISO 11
#endif


#define LOW (0x0)
#define HIGH (0x1)
#define INPUT (0x01)
#define OUTPUT (0x03)
#define RISING (0x01)
#define FALLING (0x02)

#define String std::string
#define F(string_literal) (string_literal)

unsigned long millis();
long random(long howsmall, long howbig);

#endif

size_t getFreeHeap();

extern const char* LM_TAG;
extern const char* LM_VERSION;

// LoRa band definition
// 433E6 for Asia
// 866E6 for Europe
// 915E6 for North America
#define LM_BAND 869.900F
#define LM_BANDWIDTH 125.0
#define LM_LORASF 7U
#define LM_CODING_RATE 7U
#define LM_PREAMBLE_LENGTH 8U
#define LM_POWER 6
#define LM_DUTY_CYCLE 100

//Syncronization Word that identifies the mesh network
#define LM_SYNC_WORD 19U

// Comment this line if you want to remove the crc for each packet
#define LM_ADDCRC_PAYLOAD

// Routing table max size
#define RTMAXSIZE 256

//MAX packet size per packet in bytes. It could be changed between 13 and 255 bytes. Recommended 100 or less bytes.
//If exceed it will be automatically separated through multiple packets 
//In bytes (226 bytes [UE max allowed with SF7 and 125khz])
//MAX payload size for hello packets = LM_MAX_PACKET_SIZE - 7 bytes of header
//MAX payload size for data packets = LM_MAX_PACKET_SIZE - 7 bytes of header - 2 bytes of via
//MAX payload size for reliable and large packets = LM_MAX_PACKET_SIZE - 7 bytes of header - 2 bytes of via - 3 of control packet
#define LM_MAX_PACKET_SIZE 100

// Packet types
#define NEED_ACK_P 0b00000011
#define DATA_P     0b00000010
#define HELLO_P    0b00000100
#define ACK_P      0b00001010
#define XL_DATA_P  0b00010010
#define LOST_P     0b00100010
#define SYNC_P     0b01000010

// Packet configuration
#define BROADCAST_ADDR 0xFFFF
#define DEFAULT_PRIORITY 20
#define MAX_PRIORITY 40

//Definition Times in seconds
#define HELLO_PACKETS_DELAY 120
#define DEFAULT_TIMEOUT HELLO_PACKETS_DELAY*5
#define MIN_TIMEOUT 20

// ETX Routing Configuration
#define ETX_SCALE_FACTOR 10                 // Scale ETX by 10x to fit in uint8_t (e.g., ETX 1.5 = value 15)
#define ETX_HYSTERESIS 1.1                  // Require 10% improvement before switching routes (prevents flapping)
#define MIN_ETX_SAMPLES 3                   // Minimum hello packets before using ETX (bootstrap period)
#define ETX_MIN_VALUE 10                    // Minimum ETX value (1.0 scaled)
#define ETX_MAX_VALUE 255                   // Maximum ETX value (25.5 scaled)
#define ETX_BOOTSTRAP_VALUE 15              // Conservative bootstrap ETX for new neighbors (1.5 scaled)
#define ETX_BOOTSTRAP_THRESHOLD 50          // Accept routes up to ETX 5.0 when routing table is empty
#define ETX_UNUSABLE_THRESHOLD 200          // Routes above ETX 20.0 are considered unusable and allowed to timeout
#define ETX_DECAY_THRESHOLD 100             // Apply decay when counters reach this value
#define ETX_DECAY_FACTOR 0.8                // Decay multiplier (80% retention)

// Triggered Update Configuration
#define MIN_TRIGGERED_UPDATE_INTERVAL 5     // Minimum seconds between triggered updates (rate limiting)
#define PER_ROUTE_COOLDOWN 10               // Seconds before same route can trigger update again
#define MAX_TRIGGERED_UPDATE_INTERVAL 60    // Maximum backoff interval in seconds
#define MAX_STORM_BACKOFF_COUNTER 4         // Maximum exponential backoff (2^4 = 16x)

// Loop Prevention Configuration
#define DUPLICATE_CACHE_SIZE 50             // Number of recent packet IDs to track
#define DUPLICATE_CACHE_TIMEOUT 300000      // Packet ID cache timeout in ms (5 minutes)

//Maximum times that a sequence of packets reach the timeout
#define MAX_TIMEOUTS 10
#define MAX_RESEND_PACKET 3
#define MAX_TRY_BEFORE_SEND 5

//Role Types
#define ROLE_DEFAULT 0b00000000
#define ROLE_GATEWAY 0b00000001
//Free Role Types from 0b00000010 to 0b10000000

// Define if is testing
// #define LM_TESTING

#endif