/**
 * @file main.cpp
 * @warning As said by XPowerLib: ⚠️ Please do not run the example without knowing the external load voltage of the PMU,
 * it may burn your external load, please check the voltage setting before running the example,
 * if there is any loss,please bear it by yourself ⚠️
 * 
 * @brief Battery-optimized LoRaMesher example for TTGO T-Beam
 *
 * Demonstrates LoRa mesh networking with power management:
 * - Automatic PMU detection (AXP192/AXP2101)
 * - Sleep/wake callbacks for power optimization
 * - Periodic message sending with routing table display
 */

#include <Arduino.h>

#include <iostream>

#include "initDevices.h"
#include "loramesher.hpp"

using namespace loramesher;

// ============================================================================
// Configuration
// ============================================================================

// Radio hardware pins (active - not commented out)
#define LORA_CS 18
#define LORA_RST 23
#define LORA_IRQ 26
#define LORA_IO1 33

// Radio parameters
#define LORA_RADIO_TYPE RadioType::kSx1276
#define LORA_FREQUENCY 869.900F
#define LORA_SPREADING_FACTOR 7U
#define LORA_BANDWITH 125.0
#define LORA_CODING_RATE 7U
#define LORA_POWER 6
#define LORA_SYNC_WORD 20U
#define LORA_CRC true
#define LORA_PREAMBLE_LENGTH 8U

// Network configuration
#define NODE_MANAGER_ADDRESS 0x3ADF

// ============================================================================
// Global State
// ============================================================================

std::unique_ptr<LoraMesher> mesher = nullptr;
uint8_t message_counter = 0;

// ============================================================================
// Power Management Callbacks
// ============================================================================

using namespace loramesher::power;

SleepResult OnSleep(const SleepContext& ctx) {
    if (!InitDevices::prepareSleep()) {
        Serial.println("Error: Failed to prepare sleep");
        return power::SleepResult{false};  // veto: peripheral state unknown
    }
    // After returning true, the protocol puts the radio and MCU to sleep.
    // OnWakeUp will be called before the next active slot.
    return power::SleepResult{true};
}

void OnWakeUp(PowerState previous_state) {
    InitDevices::init();
}

// ============================================================================
// Data Callback
// ============================================================================

void OnDataReceived(AddressType source, const std::vector<uint8_t>& data) {
    std::cout << "Received " << data.size() << " bytes from 0x" << std::hex
              << source << std::dec << std::endl;
}

// ============================================================================
// Helper Functions
// ============================================================================

void printRoutingTable() {
    auto routes = mesher->GetRoutingTable();
    std::cout << "Routes: " << routes.size() << std::endl;
    for (const auto& route : routes) {
        std::cout << "  0x" << std::hex << route.destination << " via 0x"
                  << route.next_hop << std::dec << " ("
                  << static_cast<int>(route.hop_count) << " hops)" << std::endl;
    }
}

void printNetworkStatus() {
    auto status = mesher->GetNetworkStatus();
    std::cout << "Network: nodes=" << status.connected_nodes << ", manager=0x"
              << std::hex << status.network_manager << std::dec
              << ", sync=" << (status.is_synchronized ? "yes" : "no")
              << std::endl;
}

void sendTestMessage() {
    auto routes = mesher->GetRoutingTable();
    if (routes.empty()) {
        return;
    }

    AddressType dest = routes[message_counter % routes.size()].destination;
    if (dest == mesher->GetNodeAddress()) {
        message_counter++;
        return;
    }

    std::string msg = "Hello from node!";
    Result result =
        mesher->Send(dest, std::vector<uint8_t>(msg.begin(), msg.end()));

    if (result) {
        std::cout << "Sent to 0x" << std::hex << dest << std::dec << std::endl;
        message_counter++;
    } else {
        std::cerr << "Send failed: " << result.GetErrorMessage() << std::endl;
    }
}

// ============================================================================
// LoraMesher Setup
// ============================================================================

void configureLoraMesher() {
    // 1. Hardware pin configuration
    PinConfig pin_config(LORA_CS, LORA_RST, LORA_IRQ, LORA_IO1);

    // 2. Radio configuration
    RadioConfig radio_config(LORA_RADIO_TYPE, LORA_FREQUENCY,
                             LORA_SPREADING_FACTOR, LORA_BANDWITH,
                             LORA_CODING_RATE, LORA_POWER, LORA_SYNC_WORD,
                             LORA_CRC, LORA_PREAMBLE_LENGTH);

    // 3. Mesh protocol configuration
    AddressType my_address = LoraMesher::GenerateAddressFromHardware();
    LoRaMeshProtocolConfig mesh_config(0, 60000, 180000, 10);

    if (my_address == NODE_MANAGER_ADDRESS) {
        mesh_config.setNodeRole(NodeRole::NETWORK_MANAGER);
    } else {
        mesh_config.setNodeRole(NodeRole::NODE_ONLY);
    }

    // 4. Build LoraMesher with power callbacks
    mesher = LoraMesher::Builder()
                 .withRadioConfig(radio_config)
                 .withPinConfig(pin_config)
                 .withLoRaMeshProtocol(mesh_config)
                 .withPrepareSleepCallback(OnSleep)
                 .withWakeUpCallback(OnWakeUp)
                 .Build();

    // 5. Set data callback
    mesher->SetDataCallback(OnDataReceived);

    // 6. Start the mesh
    Result result = mesher->Start();
    if (!result) {
        std::cerr << "Failed to start: " << result.GetErrorMessage()
                  << std::endl;
        return;
    }

    // 7. Set up route update callback
    auto protocol = mesher->GetLoRaMeshProtocol();
    if (protocol) {
        protocol->SetRouteUpdateCallback([](bool updated, AddressType dest,
                                            AddressType next_hop,
                                            uint8_t hops) {
            if (updated) {
                std::cout << "Route: 0x" << std::hex << dest << " via 0x"
                          << next_hop << std::dec << " ("
                          << static_cast<int>(hops) << " hops)" << std::endl;
            }
        });
    }
}

// ============================================================================
// Arduino Entry Points
// ============================================================================

#ifdef ARDUINO
void setup() {
    Serial.begin(115200);
    esp_log_level_set("*", ESP_LOG_INFO);

    InitDevices::init();
    configureLoraMesher();
}

void loop() {
    printRoutingTable();
    printNetworkStatus();
    sendTestMessage();

    auto routes = mesher->GetRoutingTable();
    uint32_t delay_ms = routes.empty() ? 10000 : 10000 * routes.size();
    delay(delay_ms);
}
#endif
