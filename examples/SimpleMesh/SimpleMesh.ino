/**
 * @file SimpleMesh.ino
 * @brief Simple example demonstrating LoRaMesher mesh networking on the ESP32
 *
 * This sketch shows how to set up a LoRa mesh network node that:
 * - Auto-discovers and joins existing networks
 * - Sends periodic "Hello" messages to peers
 * - Prints the routing table and network status over Serial
 *
 * Requires the ESP32 Arduino core 3.x or newer (C++20). Install the RadioLib
 * dependency from the Library Manager before compiling.
 */

#include "loramesher.hpp"

using namespace loramesher;

// =============================================================================
// Hardware Pin Configuration
// =============================================================================
// Configure these pins for your board. Common configurations:
//   TTGO T-Beam v1.x:    CS=18, RST=23, IRQ=26, IO1=33  (SX1276)
//   TTGO LoRa32 v1:      CS=18, RST=14, IRQ=26, IO1=33  (SX1278)
//   LILYGO T3 S3 v1.x:   CS=7,  RST=8,  IRQ=9,  IO1=33  (SX1278)
//   Heltec WiFi LoRa:    CS=18, RST=14, IRQ=26, IO1=35  (SX1276)
//   Heltec WiFi LoRa V3: CS=8,  RST=12, IRQ=14, IO1=13  (SX1262)
//     SPI pins: SCK=9, MISO=11, MOSI=10
//     Requires: radioConfig.setTcxoVoltage(1.8F)

#define LORA_CS 18   // SPI Chip Select (NSS)
#define LORA_RST 23  // Radio Reset pin
#define LORA_IRQ 26  // DIO0 - Primary interrupt
#define LORA_IO1 33  // DIO1 - Secondary interrupt

// =============================================================================
// Radio Configuration
// =============================================================================
// Frequency: Use appropriate band for your region (EU868, US915, etc.)
// SF7 + 125kHz: Good balance of range and speed
// Power: Start low (6 dBm) for testing, increase for longer range

#define LORA_RADIO_TYPE RadioType::kSx1276
#define LORA_FREQUENCY 869.900F   // MHz - EU868 band
#define LORA_SPREADING_FACTOR 7U  // SF7-SF12: higher = more range, slower
#define LORA_BANDWITH 125.0       // kHz - 125/250/500
#define LORA_CODING_RATE 7U       // 5-8: higher = more error correction
#define LORA_POWER 6              // dBm - transmit power
#define LORA_SYNC_WORD 20U        // Network identifier (0-255)
#define LORA_CRC true             // Enable CRC checking
#define LORA_PREAMBLE_LENGTH 8U   // Preamble symbols

// =============================================================================
// Global Variables
// =============================================================================

std::unique_ptr<LoraMesher> mesher = nullptr;
uint8_t counter_address = 0;  // Cycles through routing table destinations

// =============================================================================
// Callbacks
// =============================================================================

/**
 * @brief Called when data is received from another node
 *
 * This callback runs in the protocol context. For production code,
 * forward data to a separate task for processing to avoid blocking.
 *
 * @param source Address of the sending node
 * @param data   Received payload bytes
 */
void OnDataReceived(AddressType source, const std::vector<uint8_t>& data) {
    Serial.printf("Received data from: 0x%04X (%u bytes)\n",
                  static_cast<unsigned>(source),
                  static_cast<unsigned>(data.size()));

    for (size_t i = 0; i < data.size(); ++i) {
        Serial.printf("%02X ", data[i]);
    }
    Serial.println(data.empty() ? "(no data)" : "");
}

// =============================================================================
// Helper Functions
// =============================================================================

/** @brief Prints all entries in the routing table */
void printRoutingTable() {
    auto routes = mesher->GetRoutingTable();
    Serial.printf("Routing table has %u entries:\n",
                  static_cast<unsigned>(routes.size()));
    for (const auto& route : routes) {
        Serial.printf("  Destination: 0x%04X, Next hop: 0x%04X, Hops: %d, Valid: %s\n",
                      static_cast<unsigned>(route.destination),
                      static_cast<unsigned>(route.next_hop),
                      static_cast<int>(route.hop_count),
                      route.is_valid ? "yes" : "no");
    }
}

/** @brief Prints network synchronization status */
void printNetworkStatus() {
    auto status = mesher->GetNetworkStatus();
    Serial.printf("Network status: State=%d, Manager=0x%04X, Slot=%u, Nodes=%u\n",
                  static_cast<int>(status.current_state),
                  static_cast<unsigned>(status.network_manager),
                  static_cast<unsigned>(status.current_slot),
                  static_cast<unsigned>(status.connected_nodes));

    if (status.is_synchronized) {
        Serial.printf("Node is synchronized, last sync %u ms ago.\n",
                      static_cast<unsigned>(status.time_since_last_sync_ms));
    } else {
        Serial.println("Node is not synchronized.");
    }
}

/**
 * @brief Sends a test message to the next peer in the routing table
 * @return true if message was sent, false otherwise
 */
bool sendTestMessage() {
    auto routes = mesher->GetRoutingTable();
    if (routes.empty()) {
        return false;
    }

    AddressType dest = routes[counter_address % routes.size()].destination;

    if (dest == mesher->GetNodeAddress()) {
        counter_address++;
        return false;
    }

    Result ready = mesher->IsReadyToSend(dest);
    if (!ready) {
        Serial.printf("Not ready to send to 0x%04X: %s\n",
                      static_cast<unsigned>(dest),
                      ready.GetErrorMessage().c_str());
        return false;
    }

    std::string message = "Hello from node!";
    Result result = mesher->Send(
        dest, std::vector<uint8_t>(message.begin(), message.end()));

    if (result) {
        Serial.printf("Sent message to 0x%04X\n", static_cast<unsigned>(dest));
        counter_address++;
        return true;
    }

    Serial.printf("Failed to send message: %s\n",
                  result.GetErrorMessage().c_str());
    return false;
}

// =============================================================================
// Initialization
// =============================================================================

void ConfigureAndUseLoraMesher() {
    // Step 1: Configure hardware pins
    PinConfig pinConfig(LORA_CS, LORA_RST, LORA_IRQ, LORA_IO1);

    // Step 2: Configure radio parameters
    RadioConfig radioConfig(LORA_RADIO_TYPE, LORA_FREQUENCY,
                            LORA_SPREADING_FACTOR, LORA_BANDWITH,
                            LORA_CODING_RATE, LORA_POWER, LORA_SYNC_WORD,
                            LORA_CRC, LORA_PREAMBLE_LENGTH);

    // --- Heltec WiFi LoRa V3 configuration ---
    // PinConfig pinConfig(8, 12, 14, 13, 9, 11, 10);  // CS, RST, IRQ, IO1, SCK, MISO, MOSI
    // RadioConfig radioConfig(RadioType::kSx1262, 868.0F, 7U, 125.0, 7U, 14, 20U, true, 8U);
    // radioConfig.setTcxoVoltage(1.8F);

    // Step 3: Create protocol configuration (uses defaults)
    LoRaMeshProtocolConfig mesh_config;

    // Step 4: Build and configure LoraMesher instance
    mesher = LoraMesher::Builder()
                 .withRadioConfig(radioConfig)
                 .withPinConfig(pinConfig)
                 .withLoRaMeshProtocol(mesh_config)
                 .Build();

    // Step 5: Register data callback
    mesher->SetDataCallback(OnDataReceived);

    // Step 6: Start the mesh network
    Result init_result = mesher->Start();
    if (!init_result) {
        Serial.printf("Failed to start LoraMesher: %s\n",
                      init_result.GetErrorMessage().c_str());
        return;
    }

    // Step 7: Set up route update notifications (optional)
    auto mesh_protocol = mesher->GetLoRaMeshProtocol();
    if (mesh_protocol) {
        mesh_protocol->SetRouteUpdateCallback(
            [](bool route_updated, AddressType destination,
               AddressType next_hop, uint8_t hop_count) {
                if (route_updated) {
                    Serial.printf(
                        "Route updated - Destination: 0x%04X, Next hop: 0x%04X, Hops: %d\n",
                        static_cast<unsigned>(destination),
                        static_cast<unsigned>(next_hop),
                        static_cast<int>(hop_count));
                } else {
                    Serial.printf("Route removed for destination: 0x%04X\n",
                                  static_cast<unsigned>(destination));
                }
            });
    }
}

// =============================================================================
// Arduino Entry Points
// =============================================================================

void setup() {
    Serial.begin(115200);
    ConfigureAndUseLoraMesher();
}

void loop() {
    printRoutingTable();
    printNetworkStatus();

    bool sent = sendTestMessage();
    auto routes = mesher->GetRoutingTable();

    // Wait before next iteration.
    // Longer delay when there are more routes to avoid congestion.
    delay(sent ? 10000 * routes.size() : 10000);
}
