/**
 * @file main.cpp
 * @brief Simple example demonstrating LoRaMesher mesh networking
 *
 * This example shows how to set up a LoRa mesh network node that:
 * - Auto-discovers and joins existing networks
 * - Sends periodic "Hello" messages to peers
 * - Displays routing table and network status
 *
 * Uses std::cout for output (consistent with library internals).
 */

#include <iostream>

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
    std::cout << "Received data from: 0x" << std::hex << source << std::dec
              << " (" << data.size() << " bytes)" << std::endl;

    for (size_t i = 0; i < data.size(); ++i) {
        std::cout << std::hex << static_cast<int>(data[i]) << " ";
    }
    std::cout << std::dec << (data.empty() ? "(no data)" : "") << std::endl;
}

// =============================================================================
// Helper Functions
// =============================================================================

/** @brief Prints all entries in the routing table */
void printRoutingTable() {
    auto routes = mesher->GetRoutingTable();
    std::cout << "Routing table has " << routes.size()
              << " entries:" << std::endl;
    for (const auto& route : routes) {
        std::cout << "  Destination: 0x" << std::hex << route.destination
                  << ", Next hop: 0x" << route.next_hop
                  << ", Hops: " << std::dec << static_cast<int>(route.hop_count)
                  << ", Valid: " << (route.is_valid ? "yes" : "no")
                  << std::endl;
    }
}

/** @brief Prints network synchronization status */
void printNetworkStatus() {
    auto status = mesher->GetNetworkStatus();
    std::cout << "Network status: State="
              << static_cast<int>(status.current_state) << ", Manager=0x"
              << std::hex << status.network_manager << ", Slot=" << std::dec
              << status.current_slot << ", Nodes=" << status.connected_nodes
              << std::endl;

    if (status.is_synchronized) {
        std::cout << "Node is synchronized, last sync "
                  << status.time_since_last_sync_ms << " ms ago." << std::endl;
    } else {
        std::cout << "Node is not synchronized." << std::endl;
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

    // Skip sending to self
    if (dest == mesher->GetNodeAddress()) {
        counter_address++;
        return false;
    }

    std::string message = "Hello from node!";
    Result result = mesher->Send(
        dest, std::vector<uint8_t>(message.begin(), message.end()));

    if (result) {
        std::cout << "Sent message to 0x" << std::hex << dest << std::dec
                  << std::endl;
        counter_address++;
        return true;
    }

    std::cerr << "Failed to send message: " << result.GetErrorMessage()
              << std::endl;
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
        std::cerr << "Failed to start LoraMesher: "
                  << init_result.GetErrorMessage() << std::endl;
        return;
    }

    // Step 7: Set up route update notifications (optional)
    auto mesh_protocol = mesher->GetLoRaMeshProtocol();
    if (mesh_protocol) {
        mesh_protocol->SetRouteUpdateCallback(
            [](bool route_updated, AddressType destination,
               AddressType next_hop, uint8_t hop_count) {
                if (route_updated) {
                    std::cout << "Route updated - Destination: 0x" << std::hex
                              << destination << ", Next hop: 0x" << next_hop
                              << ", Hops: " << std::dec
                              << static_cast<int>(hop_count) << std::endl;
                } else {
                    std::cout << "Route removed for destination: 0x" << std::hex
                              << destination << std::endl;
                }
            });
    }
}

// =============================================================================
// Arduino Entry Points
// =============================================================================

#ifdef ARDUINO
void setup() {
    Serial.begin(115200);
    ConfigureAndUseLoraMesher();
}

void loop() {
    printRoutingTable();
    printNetworkStatus();

    bool sent = sendTestMessage();
    auto routes = mesher->GetRoutingTable();

    // Wait before next iteration
    // Longer delay when there are more routes to avoid congestion
    // TODO: Wait until next data slot available
    delay(sent ? 10000 * routes.size() : 10000);
}
#endif
